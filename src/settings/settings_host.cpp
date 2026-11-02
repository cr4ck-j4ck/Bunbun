// bunbun-settings.exe — Bunbun's settings app.
//
// A native window hosting the React UI (settings-ui/, built with Vite) in the
// system WebView2 runtime: the same engine Tauri uses, without a bundled
// browser. It opens on demand and costs nothing while closed.
//
// The UI talks to this host with window.chrome.webview.postMessage:
//   request  {"id": n, "cmd": "...", "args": {...}}
//   reply    {"id": n, "ok": true, "result": ...} | {"id": n, "ok": false, "error": "..."}
// Settings are written straight to settings.ini; the running island watches
// that file and reloads, so every change previews live.
#include <windows.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include "../agents/hook_installer.h"
#include "../common/json.h"
#include "../common/pipe.h"
#include "../common/todos.h"
#include "../common/update.h"

#include <memory>
#include <thread>
#include "../island/wh_shim.h"
#include "../mascot/mascot.h"
#include "../mascot/pack_sheet.h"
#include "WebView2.h"

// MinGW's __uuidof needs these; the GUIDs are the MIDL_INTERFACE ids in WebView2.h.
__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, 0x6c4819f3, 0xc9b7, 0x4260, 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c)
__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, 0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d)
__CRT_UUID_DECL(ICoreWebView2Controller2, 0xc979903e, 0xd4ca, 0x4228, 0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab)
__CRT_UUID_DECL(ICoreWebView2_3, 0xA0D6DF20, 0x3B92, 0x416D, 0xAA, 0x0C, 0x43, 0x7A, 0x9C, 0x72, 0x78, 0x57)
__CRT_UUID_DECL(ICoreWebView2WebMessageReceivedEventHandler, 0x57213f19, 0x00e6, 0x49fa, 0x8e, 0x07, 0x89, 0x8e, 0xa0, 0x1e, 0xcb, 0xd2)
__CRT_UUID_DECL(ICoreWebView2NewWindowRequestedEventHandler, 0xd4c185fe, 0xc81c, 0x4989, 0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51)

using Microsoft::WRL::ComPtr;
using bb::json::Value;

namespace {

constexpr wchar_t kWindowClass[] = L"BunbunSettings";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

HWND g_hwnd = nullptr;
// A reply finished on a worker thread (lParam: new std::wstring of JSON), posted
// back so it reaches WebView2 from the UI thread like every other reply.
constexpr UINT WM_APP_ASYNC_REPLY = WM_APP + 0x30;
std::wstring g_startPage;  // from --page
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

// ── Small helpers ───────────────────────────────────────────────────────────

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L"\\/"));
}

Value Str(const std::wstring& w) { return Value::String(Utf8(w)); }

// A COM completion handler from a lambda (WRL's Callback is not in mingw-w64).
template <typename Iface, typename... Args>
class Handler final : public Iface {
   public:
    explicit Handler(std::function<HRESULT(Args...)> fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(Iface)) {
            *ppv = static_cast<Iface*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

   private:
    std::atomic<ULONG> refs_{1};
    std::function<HRESULT(Args...)> fn_;
};

// type_identity keeps Args fixed to the explicit list, so a lambda converts.
template <typename Iface, typename... Args>
ComPtr<Iface> MakeHandler(std::type_identity_t<std::function<HRESULT(Args...)>> fn) {
    ComPtr<Iface> p;
    p.Attach(new Handler<Iface, Args...>(std::move(fn)));
    return p;
}

// ── Settings ────────────────────────────────────────────────────────────────

bool SplitKey(const std::wstring& name, std::wstring* section, std::wstring* key) {
    const size_t dot = name.find(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size()) return false;
    *section = name.substr(0, dot);
    *key = name.substr(dot + 1);
    return true;
}

bool IsKnownSetting(const std::wstring& name) {
    size_t n = 0;
    const bb::SettingDefault* d = bb::DefaultSettings(&n);
    for (size_t i = 0; i < n; ++i)
        if (_wcsicmp((std::wstring(d[i].section) + L"." + d[i].key).c_str(), name.c_str()) == 0) return true;
    return false;
}

Value GetSettings() {
    bb::EnsureSettingsFile();
    size_t n = 0;
    const bb::SettingDefault* d = bb::DefaultSettings(&n);
    Value values = Value::Object(), defaults = Value::Object(), custom = Value::Object();
    constexpr wchar_t kMissing[] = L"\x01missing\x01";
    for (size_t i = 0; i < n; ++i) {
        const std::string name = Utf8(std::wstring(d[i].section) + L"." + d[i].key);
        wchar_t buf[2048];
        GetPrivateProfileStringW(d[i].section, d[i].key, kMissing, buf, ARRAYSIZE(buf), bb::SettingsPath());
        const bool set = wcscmp(buf, kMissing) != 0;
        values[name] = Str(set ? buf : d[i].value);
        defaults[name] = Str(d[i].value);
        custom[name] = Value::Bool(set);
    }
    Value out = Value::Object();
    out["values"] = std::move(values);
    out["defaults"] = std::move(defaults);
    out["custom"] = std::move(custom);
    out["path"] = Str(bb::SettingsPath());
    return out;
}

bool WriteSetting(const std::string& name, const Value* value, std::string* err) {
    const std::wstring w = Wide(name);
    std::wstring section, key;
    if (!SplitKey(w, &section, &key) || !IsKnownSetting(w)) { *err = "unknown setting " + name; return false; }
    if (!value || value->isNull()) {
        WritePrivateProfileStringW(section.c_str(), key.c_str(), nullptr, bb::SettingsPath());  // back to default
        return true;
    }
    std::wstring text;
    if (value->isString()) text = Wide(value->str);
    else if (value->isBool()) text = value->b ? L"true" : L"false";
    else if (value->isNumber()) text = Wide(bb::json::Serialize(*value));
    else { *err = "unsupported value type"; return false; }
    for (wchar_t c : text)
        if (c == L'\r' || c == L'\n') { *err = "line breaks are not allowed"; return false; }
    WritePrivateProfileStringW(section.c_str(), key.c_str(), text.c_str(), bb::SettingsPath());
    return true;
}

// ── Monitors ────────────────────────────────────────────────────────────────

std::wstring FriendlyMonitorName(const wchar_t* gdiDevice) {
    UINT32 paths = 0, modes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS) return {};
    std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, p.data(), &modes, m.data(), nullptr) != ERROR_SUCCESS) return {};
    for (UINT32 i = 0; i < paths; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof src;
        src.header.adapterId = p[i].sourceInfo.adapterId;
        src.header.id = p[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || wcscmp(src.viewGdiDeviceName, gdiDevice) != 0) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof tgt;
        tgt.header.adapterId = p[i].targetInfo.adapterId;
        tgt.header.id = p[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS) {
            if (tgt.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                tgt.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED)
                return tgt.monitorFriendlyDeviceName[0] ? std::wstring(tgt.monitorFriendlyDeviceName) + L" (built-in)" : L"Built-in display";
            return tgt.monitorFriendlyDeviceName;
        }
    }
    return {};
}

BOOL CALLBACK CollectMonitor(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<Value*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    Value v = Value::Object();
    // Same 1-based order the island uses for "Display N".
    v["index"] = Value::Number(static_cast<double>(list->arr.size() + 1));
    std::wstring name = FriendlyMonitorName(mi.szDevice);
    v["name"] = Str(name.empty() ? std::wstring(L"Display ") + std::to_wstring(list->arr.size() + 1) : name);
    v["primary"] = Value::Bool((mi.dwFlags & MONITORINFOF_PRIMARY) != 0);
    auto rect = [](const RECT& r) {
        Value o = Value::Object();
        o["x"] = Value::Number(r.left);
        o["y"] = Value::Number(r.top);
        o["w"] = Value::Number(r.right - r.left);
        o["h"] = Value::Number(r.bottom - r.top);
        return o;
    };
    v["bounds"] = rect(mi.rcMonitor);
    v["work"] = rect(mi.rcWork);
    list->push(std::move(v));
    return TRUE;
}

// ── Mascot preview ──────────────────────────────────────────────────────────

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64(const BYTE* data, size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const UINT32 v = (data[i] << 16) | ((i + 1 < n ? data[i + 1] : 0) << 8) | (i + 2 < n ? data[i + 2] : 0);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < n ? kB64[v & 63] : '=';
    }
    return out;
}

bb::mascot::Mood MoodFromName(const std::string& m) {
    using bb::mascot::Mood;
    if (m == "working") return Mood::Working;
    if (m == "thinking") return Mood::Thinking;
    if (m == "alert") return Mood::Alert;
    if (m == "happy") return Mood::Happy;
    if (m == "sad") return Mood::Sad;
    if (m == "sleepy") return Mood::Sleepy;
    if (m == "music") return Mood::Music;
    if (m == "snack") return Mood::Snack;
    if (m == "listening") return Mood::Listening;
    if (m == "talking") return Mood::Talking;
    if (m == "confused") return Mood::Confused;
    return Mood::Idle;
}

// Renders one frame with the real mascot code, so the preview is exact.
std::string RenderMascotPng(const Value& a) {
    static ComPtr<IWICImagingFactory> wic;
    static ComPtr<ID2D1Factory> d2d;
    if (!wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (!d2d) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
    if (!wic || !d2d) return {};

    const int px = std::clamp(static_cast<int>(a.getNumber("size", 160)), 32, 512);
    // The preview's colour is this character's own ("spiderman=#17161C"), which
    // is also the only form Spidey's Venom suit accepts.
    const std::string character = a.getString("character", "bunny"), color = a.getString("color");
    const bb::mascot::Style style = bb::mascot::StyleFromSettings(
        Wide(character), Wide(color.empty() || color.find('=') != std::string::npos ? color : character + "=" + color),
        static_cast<int>(a.getNumber("motion", 100)), static_cast<int>(a.getNumber("hop", 100)),
        static_cast<int>(a.getNumber("blink", 4) * 10), a.getBool("eyesFollow", true), a.getBool("celebrate", true));

    ComPtr<IWICBitmap> bmp;
    ComPtr<ID2D1RenderTarget> rt;
    wic->CreateBitmap(px, px, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (!bmp || FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return {};
    const float s = static_cast<float>(px);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    bb::mascot::DrawStyled(rt.Get(), style, D2D1::Point2F(s * 0.5f, s * 0.42f), s * 0.92f,
                           MoodFromName(a.getString("mood", "idle")), a.getNumber("t", 0), a.getNumber("since", 0),
                           D2D1::Point2F(static_cast<float>(a.getNumber("lookX", 0)), static_cast<float>(a.getNumber("lookY", 0))));
    rt->EndDraw();
    bb::mascot::ReleaseThreadResources();  // rt is short-lived; drop the mascot's cached brush

    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0));
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (!stream || FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->WriteSource(bmp.Get(), nullptr)) || FAILED(frame->Commit()) ||
        FAILED(enc->Commit()))
        return {};
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    std::vector<BYTE> bytes(static_cast<size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    return "data:image/png;base64," + Base64(bytes.data(), read);
}

// ── Autostart ───────────────────────────────────────────────────────────────

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof buf;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"Bunbun", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void SetAutostart(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        const std::wstring cmd = L"\"" + ExeDir() + L"\\bunbun.exe\"";
        RegSetValueExW(key, L"Bunbun", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"Bunbun");
    }
    RegCloseKey(key);
}

bool IslandRunning() {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\Bunbun.Instance");
    if (m) CloseHandle(m);
    return m != nullptr;
}

// ── Bridge ──────────────────────────────────────────────────────────────────

// ── Live preview ────────────────────────────────────────────────────────────
// The preview is the real thing: the island's own pixels (fetched from the
// running island over its pipe) composited onto your actual wallpaper, at your
// monitor's real proportions, with the taskbar area shaded. Change a setting,
// the island redraws, and the next frame of the preview shows exactly that.

std::string Base64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int bits = 0, acc = 0;
    for (char c : in) {
        const int v = val(c);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

// Asks the running island for a snapshot. Empty when the island isn't running.
bool RequestIslandSnapshot(Value* reply) {
    const std::wstring name = bb::pipe::Name();
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 20 && h == INVALID_HANDLE_VALUE; ++attempt) {
        h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_PIPE_BUSY) return false;
            WaitNamedPipeW(name.c_str(), 15);
        }
    }
    if (h == INVALID_HANDLE_VALUE) return false;
    if (!bb::pipe::ServerIsSameUser(h)) { CloseHandle(h); return false; }
    std::string line;
    const bool ok = bb::pipe::WriteAll(h, "{\"bunbun_command\":\"snapshot\"}\n") && bb::pipe::ReadLine(h, &line);
    CloseHandle(h);
    return ok && bb::json::Parse(line, reply) && reply->isObject();
}

ComPtr<IWICBitmapSource> DecodeImage(IWICImagingFactory* wic, IWICBitmapDecoder* dec) {
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(dec->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeMedianCut)))
        return nullptr;
    return conv;
}

// The current wallpaper, decoded once per file change.
ComPtr<IWICBitmapSource> Wallpaper(IWICImagingFactory* wic) {
    static std::wstring cachedPath;
    static FILETIME cachedTime{};
    static ComPtr<IWICBitmap> cached;
    wchar_t path[MAX_PATH] = L"";
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return nullptr;
    if (cached && cachedPath == path && CompareFileTime(&cachedTime, &fa.ftLastWriteTime) == 0) return cached;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return nullptr;
    ComPtr<IWICBitmapSource> src = DecodeImage(wic, dec.Get());
    if (!src) return nullptr;
    cached.Reset();
    wic->CreateBitmapFromSource(src.Get(), WICBitmapCacheOnLoad, &cached);  // keep it in memory
    cachedPath = path;
    cachedTime = fa.ftLastWriteTime;
    return cached;
}

std::string EncodePng(IWICImagingFactory* wic, IWICBitmap* bmp) {
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0));
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (!stream || FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->WriteSource(bmp, nullptr)) || FAILED(frame->Commit()) ||
        FAILED(enc->Commit()))
        return {};
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    std::vector<BYTE> bytes(static_cast<size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    return "data:image/png;base64," + Base64(bytes.data(), read);
}

Value CapturePreview(const Value& args) {
    Value out = Value::Object();
    Value snap;
    if (!RequestIslandSnapshot(&snap)) {
        out["running"] = Value::Bool(false);
        return out;
    }
    out["running"] = Value::Bool(true);
    out["shown"] = Value::Bool(snap.getBool("shown"));
    const Value* mon = snap.find("monitor");
    const Value* work = snap.find("work");
    const Value* isl = snap.find("island");
    if (!snap.getBool("shown") || !mon || !work || !isl) return out;

    static ComPtr<IWICImagingFactory> wic;
    static ComPtr<ID2D1Factory> d2d;
    if (!wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (!d2d) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
    if (!wic || !d2d) return out;

    // The island's own pixels.
    const std::string png = Base64Decode(snap.getString("png"));
    ComPtr<IStream> pngStream;
    pngStream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(png.data()), static_cast<UINT>(png.size())));
    ComPtr<IWICBitmapDecoder> pngDec;
    if (!pngStream || FAILED(wic->CreateDecoderFromStream(pngStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &pngDec)))
        return out;
    ComPtr<IWICBitmapSource> islandSrc = DecodeImage(wic.Get(), pngDec.Get());
    if (!islandSrc) return out;

    // Canvas at the monitor's real proportions.
    const double monW = mon->getNumber("w", 1920), monH = mon->getNumber("h", 1080);
    const int W = std::clamp(static_cast<int>(args.getNumber("width", 1280)), 320, 2560);
    const int H = std::max(1, static_cast<int>(W * monH / monW));
    const float k = static_cast<float>(W / monW);

    ComPtr<IWICBitmap> canvas;
    ComPtr<ID2D1RenderTarget> rt;
    wic->CreateBitmap(W, H, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &canvas);
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (!canvas || FAILED(d2d->CreateWicBitmapRenderTarget(canvas.Get(), props, &rt))) return out;

    rt->BeginDraw();
    const DWORD desk = GetSysColor(COLOR_DESKTOP);
    rt->Clear(D2D1::ColorF(GetRValue(desk) / 255.0f, GetGValue(desk) / 255.0f, GetBValue(desk) / 255.0f));
    if (ComPtr<IWICBitmapSource> wall = Wallpaper(wic.Get())) {
        ComPtr<ID2D1Bitmap> wb;
        if (SUCCEEDED(rt->CreateBitmapFromWicBitmap(wall.Get(), nullptr, &wb))) {
            const D2D1_SIZE_F ws = wb->GetSize();
            const float cover = std::max(W / ws.width, H / ws.height);  // "Fill", Windows' default fit
            const float dw = ws.width * cover, dh = ws.height * cover;
            rt->DrawBitmap(wb.Get(), D2D1::RectF((W - dw) / 2, (H - dh) / 2, (W + dw) / 2, (H + dh) / 2), 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }
    // Taskbar: whatever the work area leaves out of the monitor.
    ComPtr<ID2D1SolidColorBrush> bar;
    rt->CreateSolidColorBrush(D2D1::ColorF(0x1C1B22, 0.92f), &bar);
    const float mx = static_cast<float>(mon->getNumber("x")), my = static_cast<float>(mon->getNumber("y"));
    const float wx = static_cast<float>(work->getNumber("x")) - mx, wy = static_cast<float>(work->getNumber("y")) - my;
    const float ww = static_cast<float>(work->getNumber("w")), wh = static_cast<float>(work->getNumber("h"));
    if (wy > 0) rt->FillRectangle(D2D1::RectF(0, 0, W, wy * k), bar.Get());
    if (wy + wh < monH) rt->FillRectangle(D2D1::RectF(0, (wy + wh) * k, W, H), bar.Get());
    if (wx > 0) rt->FillRectangle(D2D1::RectF(0, 0, wx * k, H), bar.Get());
    if (wx + ww < monW) rt->FillRectangle(D2D1::RectF((wx + ww) * k, 0, W, H), bar.Get());
    // The island, exactly where it is.
    ComPtr<ID2D1Bitmap> ib;
    if (SUCCEEDED(rt->CreateBitmapFromWicBitmap(islandSrc.Get(), nullptr, &ib))) {
        const float ix = (static_cast<float>(isl->getNumber("x")) - mx) * k;
        const float iy = (static_cast<float>(isl->getNumber("y")) - my) * k;
        rt->DrawBitmap(ib.Get(), D2D1::RectF(ix, iy, ix + static_cast<float>(isl->getNumber("w")) * k,
                                             iy + static_cast<float>(isl->getNumber("h")) * k),
                       1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    rt->EndDraw();

    out["screen"] = Value::String(EncodePng(wic.Get(), canvas.Get()));
    out["island"] = Value::String("data:image/png;base64," + snap.getString("png"));
    out["monitor"] = *mon;
    out["islandRect"] = *isl;
    return out;
}

// ── To-dos (the island's list, in todos.tsv next to settings.ini) ──────────────
// Each change reads the file, applies one edit and writes it back, so it can't
// undo something the island changed meanwhile (a reminder firing). The island
// watches the file and reloads.
std::wstring TodosPath() {
    std::wstring dir = bb::SettingsPath();
    dir.erase(dir.find_last_of(L"\\/"));
    return dir + L"\\todos.tsv";
}

Value TodoToJson(const bb::todos::Item& t) {
    Value o = Value::Object();
    o["id"] = Value::Number(t.id);
    o["text"] = Str(t.text);
    o["due"] = Value::Number(t.due);
    o["done"] = Value::Bool(t.done);
    o["repeat"] = Str(t.repeat);
    return o;
}

Value TodoCommand(const std::string& cmd, const Value& args, std::string* err) {
    namespace td = bb::todos;
    std::vector<td::Item> items = td::Load(TodosPath());
    if (cmd == "listTodos") {
        Value list = Value::Array();
        for (const td::Item& t : items) list.push(TodoToJson(t));
        return list;
    }
    if (cmd == "saveTodo") {  // id 0 adds a new one
        const int id = static_cast<int>(args.getNumber("id"));
        const std::wstring text = Wide(args.getString("text"));
        if (text.empty()) { *err = "text is empty"; return {}; }
        td::Item* item = nullptr;
        for (td::Item& t : items) if (t.id == id) item = &t;
        if (!item) {
            td::Item fresh;
            fresh.id = td::NextId(items);
            items.push_back(fresh);
            item = &items.back();
        }
        const double due = args.getNumber("due");
        const std::wstring repeat = due > 0 ? Wide(args.getString("repeat")) : std::wstring();
        if (due != item->due || repeat != item->repeat) item->notified = false;  // rescheduled: ring again
        item->text = text;
        item->due = due;
        item->repeat = repeat;
        item->done = args.getBool("done");
        if (item->done && !item->repeat.empty()) {
            // A repeating reminder never finishes: ticking it skips its next occurrence.
            item->done = false;
            item->due = td::NextOccurrence(item->repeat, item->due, std::max(td::Now(), item->due));
            item->notified = false;
        }
        if (!td::Save(TodosPath(), items)) { *err = "couldn't write todos.tsv"; return {}; }
        return TodoToJson(*item);
    }
    if (cmd == "deleteTodo") {
        const int id = static_cast<int>(args.getNumber("id"));
        items.erase(std::remove_if(items.begin(), items.end(), [&](const td::Item& t) { return t.id == id; }), items.end());
        if (!td::Save(TodosPath(), items)) { *err = "couldn't write todos.tsv"; return {}; }
        return Value::Bool(true);
    }
    if (cmd == "clearDoneTodos") {
        items.erase(std::remove_if(items.begin(), items.end(), [](const td::Item& t) { return t.done; }), items.end());
        if (!td::Save(TodosPath(), items)) { *err = "couldn't write todos.tsv"; return {}; }
        return Value::Bool(true);
    }
    *err = "unknown to-do command";
    return {};
}

// The optional local AI: is the helper built, and which models are in the
// models folder (%LOCALAPPDATA%\Bunbun\models; the island picks the first).
std::wstring ModelsFolder() {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) dir = std::wstring(local) + L"\\Bunbun\\models";
    CoTaskMemFree(local);
    return dir;
}

// ── Skin packs from a character sheet ───────────────────────────────────────
// sliceSheet: the page sends the picked image; we find its rows and frames and
// send back previews. savePack: the rows the user mapped to moods are written
// into a pack folder. The sliced frames wait here in between.
std::vector<bb::mascot::SheetRow> g_sheetRows;

Value SheetCommand(const std::string& cmd, const Value& args, std::string* err) {
    namespace bm = bb::mascot;
    if (cmd == "sliceSheet") {
        std::string data = args.getString("data");
        if (const size_t comma = data.find(','); data.rfind("data:", 0) == 0 && comma != std::string::npos) data.erase(0, comma + 1);
        const std::string bytes = Base64Decode(data);
        bm::Image sheet;
        if (!bm::DecodeImage(bytes.data(), bytes.size(), &sheet)) { *err = "couldn't read that image"; return {}; }
        g_sheetRows = bm::SliceSheet(sheet);
        if (g_sheetRows.empty()) { *err = "no frames found: the sheet needs rows of characters on a plain or transparent background"; return {}; }
        Value rows = Value::Array();
        for (const bm::SheetRow& r : g_sheetRows) {
            Value frames = Value::Array();
            for (const bm::Image& f : r.frames) {
                std::string png;
                if (bm::EncodePngBytes(f, &png))
                    frames.push(Value::String("data:image/png;base64," + Base64(reinterpret_cast<const BYTE*>(png.data()), png.size())));
            }
            Value row = Value::Object();
            row["frames"] = std::move(frames);
            rows.push(std::move(row));
        }
        return rows;
    }
    // savePack {name, rows: [{row, mood}]}; mood is a mood's file name, or "look"
    // for a row of look directions (left, up, centre, down, right, as on the sheet).
    std::wstring name = Wide(args.getString("name"));
    name.erase(0, name.find_first_not_of(L' '));
    name.erase(name.find_last_not_of(L' ') + 1);
    if (name.empty() || name.size() > 40 || name.find_first_of(L"\\/:*?\"<>|.") != std::wstring::npos) {
        *err = "pick a name without \\ / : * ? \" < > | or dots";
        return {};
    }
    const Value* rows = args.find("rows");
    if (!rows || !rows->isArray() || g_sheetRows.empty()) { *err = "slice a sheet first"; return {}; }
    const std::wstring dir = bm::PacksFolder() + L"\\" + name;
    static const char* kMoods[] = {"idle", "working", "thinking", "alert", "happy", "sad", "sleepy", "music", "snack",
                                   "listening", "talking", "confused"};
    for (const Value& r : rows->arr) {
        const int index = static_cast<int>(r.getNumber("row", -1));
        const std::string mood = r.getString("mood");
        if (index < 0 || index >= static_cast<int>(g_sheetRows.size()) || mood.empty()) continue;
        const auto& frames = g_sheetRows[static_cast<size_t>(index)].frames;
        std::wstring werr;
        if (mood == "look") {
            static const wchar_t* kLook[] = {L"look_left", L"look_up", L"look_center", L"look_down", L"look_right"};
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
            for (size_t i = 0; i < frames.size() && i < 5; ++i)
                if (!bm::EncodePng(frames[i], dir + L"\\" + kLook[i] + L".png")) { *err = "couldn't write the look frames"; return {}; }
            continue;
        }
        if (std::find_if(std::begin(kMoods), std::end(kMoods), [&](const char* m) { return mood == m; }) == std::end(kMoods)) continue;
        if (!bm::WriteMoodFrames(dir, Wide(mood), frames, &werr)) { *err = Utf8(werr); return {}; }
    }
    // A pack made from a sheet needs its idle frames; the rest falls back to idle.
    if (GetFileAttributesW((dir + L"\\idle_1.png").c_str()) == INVALID_FILE_ATTRIBUTES) {
        *err = "map one row to Idle: every pack needs it";
        return {};
    }
    const std::wstring ini = dir + L"\\pack.ini";
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
        WritePrivateProfileStringW(L"Pack", L"FrameSeconds", L"0.4", ini.c_str());
    return Str(name);
}

Value LocalAiCommand(const std::string& cmd) {
    const std::wstring dir = ModelsFolder();
    if (cmd == "openModelsFolder") {
        SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return Value::Bool(true);
    }
    Value o = Value::Object();
    o["helper"] = Value::Bool(GetFileAttributesW((ExeDir() + L"\\bunbun-brain.exe").c_str()) != INVALID_FILE_ATTRIBUTES);
    o["modelsFolder"] = Str(dir);
    std::vector<std::pair<std::wstring, double>> found;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.gguf").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            found.emplace_back(fd.cFileName, static_cast<double>((static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(found.begin(), found.end());
    Value models = Value::Array();
    for (const auto& [name, size] : found) {
        Value m = Value::Object();
        m["name"] = Str(name);
        m["path"] = Str(dir + L"\\" + name);
        m["size"] = Value::Number(size);
        models.push(std::move(m));
    }
    o["models"] = std::move(models);
    return o;
}

Value Dispatch(const std::string& cmd, const Value& args, std::string* err) {
    if (cmd == "listTodos" || cmd == "saveTodo" || cmd == "deleteTodo" || cmd == "clearDoneTodos") return TodoCommand(cmd, args, err);
    if (cmd == "capturePreview") return CapturePreview(args);
    if (cmd == "getSettings") return GetSettings();
    if (cmd == "setSettings") {
        const Value* values = args.find("values");
        if (!values || !values->isObject()) { *err = "values must be an object"; return {}; }
        for (const auto& [k, v] : values->obj)
            if (!WriteSetting(k, &v, err)) return {};
        return Value::Bool(true);
    }
    if (cmd == "resetSettings") {
        const Value* keys = args.find("keys");
        if (!keys || !keys->isArray()) { *err = "keys must be an array"; return {}; }
        for (const Value& k : keys->arr)
            if (!k.isString() || !WriteSetting(k.str, nullptr, err)) return {};
        return Value::Bool(true);
    }
    if (cmd == "renderMascot") return Value::String(RenderMascotPng(args));
    if (cmd == "listMonitors") {
        Value list = Value::Array();
        EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&list));
        return list;
    }
    if (cmd == "listPacks") {
        Value list = Value::Array();
        for (const auto& p : bb::mascot::ListCustomPacks()) list.push(Str(p));
        return list;
    }
    if (cmd == "openPacksFolder") {
        const std::wstring dir = bb::mascot::PacksFolder();
        SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return Value::Bool(true);
    }
    if (cmd == "localAiStatus" || cmd == "openModelsFolder") return LocalAiCommand(cmd);
    if (cmd == "sliceSheet" || cmd == "savePack") return SheetCommand(cmd, args, err);
    if (cmd == "openSettingsFile") {
        ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + std::wstring(bb::SettingsPath()) + L"\"").c_str(),
                      nullptr, SW_SHOWNORMAL);
        return Value::Bool(true);
    }
    if (cmd == "agentStatus") {
        Value o = Value::Object();
        o["claudeConnected"] = Value::Bool(bb::hooks::ClaudeHooksInstalled());
        o["claudeSettingsPath"] = Str(bb::hooks::ClaudeSettingsPath());
        o["antigravityConnected"] = Value::Bool(bb::hooks::AntigravityHooksInstalled());
        o["antigravitySettingsPath"] = Str(bb::hooks::AntigravitySettingsPath());
        o["cursorConnected"] = Value::Bool(bb::hooks::CursorHooksInstalled());
        o["cursorSettingsPath"] = Str(bb::hooks::CursorSettingsPath());
        o["copilotConnected"] = Value::Bool(bb::hooks::CopilotHooksInstalled());
        o["copilotSettingsPath"] = Str(bb::hooks::CopilotHooksPath());
        o["hookExePath"] = Str(bb::hooks::HookExePath());
        o["hookCmdVscode"] = Value::String(bb::hooks::HookCommandFor("vscode", ""));
        o["hookCmdAntigravity"] = Value::String(bb::hooks::HookCommandFor("antigravity", ""));
        o["hookCmdCursor"] = Value::String(bb::hooks::HookCommandFor("cursor", ""));
        o["hookCmdCli"] = Value::String(bb::hooks::HookCommandFor("cli", ""));
        return o;
    }
    if (cmd == "connectClaude" || cmd == "disconnectClaude") {
        std::wstring report;
        const bool ok = cmd == "connectClaude" ? bb::hooks::InstallClaudeHooks(&report) : bb::hooks::UninstallClaudeHooks(&report);
        Value o = Value::Object();
        o["ok"] = Value::Bool(ok);
        o["report"] = Str(report);
        return o;
    }
    if (cmd == "connectAntigravity" || cmd == "disconnectAntigravity") {
        std::wstring report;
        const bool ok = cmd == "connectAntigravity" ? bb::hooks::InstallAntigravityHooks(&report) : bb::hooks::UninstallAntigravityHooks(&report);
        Value o = Value::Object();
        o["ok"] = Value::Bool(ok);
        o["report"] = Str(report);
        return o;
    }
    if (cmd == "connectCopilot" || cmd == "disconnectCopilot") {
        std::wstring report;
        const bool ok = cmd == "connectCopilot" ? bb::hooks::InstallCopilotHooks(&report) : bb::hooks::UninstallCopilotHooks(&report);
        Value o = Value::Object();
        o["ok"] = Value::Bool(ok);
        o["report"] = Str(report);
        return o;
    }
    if (cmd == "connectCursor" || cmd == "disconnectCursor") {
        std::wstring report;
        const bool ok = cmd == "connectCursor" ? bb::hooks::InstallCursorHooks(&report) : bb::hooks::UninstallCursorHooks(&report);
        Value o = Value::Object();
        o["ok"] = Value::Bool(ok);
        o["report"] = Str(report);
        return o;
    }
    if (cmd == "getSystem") {
        Value o = Value::Object();
        o["autostart"] = Value::Bool(AutostartEnabled());
        o["islandRunning"] = Value::Bool(IslandRunning());
        o["version"] = Str(WH_MOD_VERSION);
        o["packsFolder"] = Str(bb::mascot::PacksFolder());
        // Windows' accent colour, for the "System" accent mode (the app follows it too).
        DWORD argb = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque))) {
            char hex[8];
            snprintf(hex, sizeof hex, "#%06X", static_cast<unsigned>(argb & 0xFFFFFF));
            o["systemAccent"] = Value::String(hex);
        }
        return o;
    }
    if (cmd == "setAutostart") { SetAutostart(args.getBool("on")); return Value::Bool(AutostartEnabled()); }
    if (cmd == "startIsland") {
        if (!IslandRunning())
            ShellExecuteW(nullptr, L"open", (ExeDir() + L"\\bunbun.exe").c_str(), nullptr, ExeDir().c_str(), SW_SHOWNORMAL);
        return Value::Bool(true);
    }
    if (cmd == "openUrl") {
        // Only web links; never arbitrary programs from the page.
        const std::string url = args.getString("url");
        if (url.rfind("https://", 0) != 0) { *err = "only https links"; return {}; }
        ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return Value::Bool(true);
    }
    *err = "unknown command " + cmd;
    return {};
}

void OnWebMessage(const std::wstring& jsonText) {
    Value msg;
    Value reply = Value::Object();
    if (!bb::json::Parse(Utf8(jsonText), &msg) || !msg.isObject()) return;
    reply["id"] = msg.find("id") ? *msg.find("id") : Value::Null();
    std::string err;
    static const Value kEmpty = Value::Object();
    const Value* args = msg.find("args");
    // "Check for updates" talks to GitHub, which can take seconds; it runs on a
    // worker so the window stays responsive, and replies when it's done.
    if (msg.getString("cmd") == "checkUpdate") {
        Value id = reply["id"];
        std::thread([id] {
            const bb::update::Release r = bb::update::FetchLatest(BUNBUN_USER_AGENT);
            const std::string current = Utf8(WH_MOD_VERSION);
            Value o = Value::Object();
            o["current"] = Value::String(current);
            o["checked"] = Value::Bool(r.ok);
            if (r.ok) {
                o["latest"] = Value::String(r.version);
                o["newer"] = Value::Bool(bb::update::CompareVersions(r.version, current) > 0);
                o["url"] = Value::String(r.url);
                o["name"] = Value::String(r.name);
            } else {
                o["error"] = Value::String(r.error);
            }
            Value out = Value::Object();
            out["id"] = id;
            out["ok"] = Value::Bool(true);
            out["result"] = std::move(o);
            auto* text = new std::wstring(Wide(bb::json::Serialize(out)));
            if (!g_hwnd || !PostMessageW(g_hwnd, WM_APP_ASYNC_REPLY, 0, reinterpret_cast<LPARAM>(text))) delete text;
        }).detach();
        return;
    }
    Value result = Dispatch(msg.getString("cmd"), args && args->isObject() ? *args : kEmpty, &err);
    reply["ok"] = Value::Bool(err.empty());
    if (err.empty()) reply["result"] = std::move(result);
    else reply["error"] = Value::String(err);
    if (g_webview) g_webview->PostWebMessageAsJson(Wide(bb::json::Serialize(reply)).c_str());
}

// ── WebView2 setup ──────────────────────────────────────────────────────────

using CreateEnvFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                             ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

void ShowError(const wchar_t* text) {
    MessageBoxW(g_hwnd, text, L"Bunbun Settings", MB_OK | MB_ICONERROR);
}

void ResizeWebView() {
    if (!g_controller) return;
    RECT r;
    GetClientRect(g_hwnd, &r);
    g_controller->put_Bounds(r);
}

bool StartWebView() {
    HMODULE loader = LoadLibraryW((ExeDir() + L"\\WebView2Loader.dll").c_str());
    auto create = loader ? reinterpret_cast<CreateEnvFn>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")) : nullptr;
    if (!create) {
        ShowError(L"WebView2Loader.dll is missing next to bunbun-settings.exe.");
        return false;
    }
    PWSTR local = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
    const std::wstring dataDir = std::wstring(local ? local : L".") + L"\\Bunbun\\settings-webview";
    CoTaskMemFree(local);

    auto onController = MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                                    ICoreWebView2Controller*>([](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
        if (FAILED(hr) || !controller) { ShowError(L"Could not start WebView2."); return S_OK; }
        g_controller = controller;
        g_controller->get_CoreWebView2(&g_webview);

        // Transparent page background, so the window's Mica shows through.
        ComPtr<ICoreWebView2Controller2> c2;
        if (SUCCEEDED(g_controller.As(&c2))) c2->put_DefaultBackgroundColor({0, 0, 0, 0});

        ComPtr<ICoreWebView2Settings> settings;
        g_webview->get_Settings(&settings);
        settings->put_AreDevToolsEnabled(GetEnvironmentVariableW(L"BUNBUN_DEVTOOLS", nullptr, 0) > 0);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);

        EventRegistrationToken token{};
        g_webview->add_WebMessageReceived(
            MakeHandler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*,
                        ICoreWebView2WebMessageReceivedEventArgs*>(
                [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    PWSTR json = nullptr;
                    if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                        OnWebMessage(json);
                        CoTaskMemFree(json);
                    }
                    return S_OK;
                }).Get(),
            &token);

        // New windows (target=_blank) open in the default browser instead.
        g_webview->add_NewWindowRequested(
            MakeHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*,
                        ICoreWebView2NewWindowRequestedEventArgs*>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    return S_OK;
                }).Get(),
            &token);

        // The UI is served from settings-ui/ next to the exe, under a private host name.
        const std::wstring ui = ExeDir() + L"\\settings-ui";
        ComPtr<ICoreWebView2_3> w3;
        if (FAILED(g_webview.As(&w3)) ||
            FAILED(w3->SetVirtualHostNameToFolderMapping(L"bunbun.ui", ui.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY))) {
            ShowError(L"This WebView2 runtime is too old. Update Microsoft Edge WebView2.");
            return S_OK;
        }
        const wchar_t* dev = _wgetenv(L"BUNBUN_UI_DEV");  // e.g. http://localhost:5173 while developing the UI
        // `--page <id>` opens straight onto a page (overview, appearance, displays, ...).
        const std::wstring base = dev && *dev ? dev : L"https://bunbun.ui/index.html";
        g_webview->Navigate((g_startPage.empty() ? base : base + L"#" + g_startPage).c_str());
        ResizeWebView();
        return S_OK;
    });

    auto onEnv = MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        [onController](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) {
                ShowError(L"Microsoft Edge WebView2 Runtime is not installed.");
                return S_OK;
            }
            env->CreateCoreWebView2Controller(g_hwnd, onController.Get());
            return S_OK;
        });
    return SUCCEEDED(create(nullptr, dataDir.c_str(), nullptr, onEnv.Get()));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_ASYNC_REPLY: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            if (g_webview) g_webview->PostWebMessageAsJson(text->c_str());
            return 0;
        }
        case WM_SIZE:
            ResizeWebView();
            return 0;
        case WM_GETMINMAXINFO: {
            const UINT dpi = GetDpiForWindow(hwnd);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = {MulDiv(900, dpi, 96), MulDiv(620, dpi, 96)};
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DESTROY:
            g_controller.Reset();
            g_webview.Reset();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int show) {
    if (cmdLine) {
        const std::wstring args = cmdLine;
        const size_t at = args.find(L"--page ");
        if (at != std::wstring::npos) {
            for (wchar_t c : args.substr(at + 7)) {
                if (!iswalnum(c)) break;  // a page id is a plain word
                g_startPage += c;
            }
        }
    }
    // One settings window: a second launch brings the first to the front.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\Bunbun.Settings");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));  // black = see-through to Mica
    // Bun (res/bunbun.ico): a large icon for Alt+Tab and the taskbar, a small one for the title bar.
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    const int w = MulDiv(1180, dpi, 96), h = MulDiv(780, dpi, 96);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g_hwnd = CreateWindowExW(0, kWindowClass, L"Bunbun Settings", WS_OVERLAPPEDWINDOW,
                             work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, w, h,
                             nullptr, nullptr, instance, nullptr);

    // Dark title bar + Mica, like the Windows 11 Settings app.
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    const int mica = 2;  // DWMSBT_MAINWINDOW
    DwmSetWindowAttribute(g_hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &mica, sizeof mica);
    const MARGINS sheet = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(g_hwnd, &sheet);

    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);
    if (!StartWebView()) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ReleaseMutex(single);
    return 0;
}
