// Bunbun — one-click updates.
//
//   bunbun.exe --update        (settings app, tray, island) copies itself to
//                              %TEMP%\Bunbun-update and starts that copy, so the
//                              installer can replace every file in the app folder
//   bunbun-updater.exe --run-update "<app folder>"
//                              the copy: a small window with Bun, which closes
//                              Bunbun and its settings, downloads the new setup,
//                              checks it against the release's SHA256SUMS.txt,
//                              installs it silently and starts the new Bunbun
//
// Only installed copies update themselves (the installer leaves unins000.exe next
// to bunbun.exe); a copy run from a build folder or a zip opens the release page.
#include "updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>

#include "common/update.h"
#include "island/wh_shim.h"
#include "mascot/mascot.h"

using Microsoft::WRL::ComPtr;

namespace {

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir = path;
    dir.resize(dir.find_last_of(L'\\'));
    return dir;
}

bool FileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ── What the window shows (worker thread writes, window reads) ──────────────

enum class Phase { Preparing, Checking, Downloading, Verifying, Installing, Starting, Done, Failed };

struct UpdateState {
    std::mutex mutex;
    Phase phase = Phase::Preparing;
    std::wstring version;    // "0.3.1" once known
    std::wstring error;      // shown when Failed
    std::wstring pageUrl;    // release page, opened on a click after a failure
    unsigned long long got = 0, total = 0;  // download progress, bytes
};
UpdateState g_update;
HWND g_window = nullptr;

void SetPhase(Phase p) {
    std::lock_guard lock(g_update.mutex);
    g_update.phase = p;
}

void Fail(const std::wstring& why) {
    std::lock_guard lock(g_update.mutex);
    g_update.phase = Phase::Failed;
    g_update.error = why;
}

// ── Closing what's running ──────────────────────────────────────────────────

// The same request as `bunbun.exe --quit`: the island's tray window takes it and
// shuts down through its normal path.
void QuitIsland() {
    HWND tray = FindWindowExW(HWND_MESSAGE, nullptr, L"BunbunTray", nullptr);
    if (!tray) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    HANDLE proc = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    PostMessageW(tray, WM_APP + 0x501, 0, 0);
    if (proc) {
        WaitForSingleObject(proc, 15000);
        CloseHandle(proc);
    }
}

// The settings window, if open, is asked to close like a click on its X.
void CloseSettings() {
    for (int tries = 0; tries < 20; ++tries) {
        HWND w = FindWindowW(nullptr, L"Bunbun Settings");
        if (!w) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        HANDLE proc = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
        PostMessageW(w, WM_CLOSE, 0, 0);
        if (proc) {
            WaitForSingleObject(proc, 3000);
            CloseHandle(proc);
        } else {
            Sleep(150);
        }
    }
}

// ── Downloads ───────────────────────────────────────────────────────────────

// GET an https URL into `body` (small files) or straight to `file`. GitHub's asset
// links redirect to its download host; WinHTTP follows that by itself.
bool Download(const std::wstring& url, std::string* body, HANDLE file, bool progress) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;

    HINTERNET session = WinHttpOpen(BUNBUN_USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    bool ok = false;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        wchar_t lengthText[32] = {};
        DWORD lengthSize = sizeof(lengthText);
        unsigned long long total = 0;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
                                &lengthSize, WINHTTP_NO_HEADER_INDEX))
            total = _wcstoui64(lengthText, nullptr, 10);
        if (progress) {
            std::lock_guard lock(g_update.mutex);
            g_update.got = 0;
            g_update.total = total;
        }
        ok = status == 200;
        std::string chunk(64 * 1024, '\0');
        unsigned long long got = 0;
        while (ok) {
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) { ok = false; break; }
            if (read == 0) break;
            if (body) {
                if (body->size() + read > 1024 * 1024) { ok = false; break; }  // checksums are a few lines
                body->append(chunk.data(), read);
            } else {
                DWORD written = 0;
                if (!WriteFile(file, chunk.data(), read, &written, nullptr) || written != read) { ok = false; break; }
            }
            got += read;
            if (progress) {
                std::lock_guard lock(g_update.mutex);
                g_update.got = got;
            }
        }
        if (ok && total && got != total) ok = false;
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

std::string Sha256Hex(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::string buf(256 * 1024, '\0');
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) && read > 0)
            if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), read, 0) != 0) { ok = false; break; }
        UCHAR digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
            static const char* kHex = "0123456789abcdef";
            for (UCHAR b : digest) { hex += kHex[b >> 4]; hex += kHex[b & 15]; }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return hex;
}

// The hash for `fileName` in a SHA256SUMS.txt ("<hex> *<name>" or "<hex>  <name>").
std::string ExpectedHash(const std::string& sums, const std::string& fileName) {
    size_t at = 0;
    while (at < sums.size()) {
        size_t end = sums.find('\n', at);
        if (end == std::string::npos) end = sums.size();
        std::string line = sums.substr(at, end - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t space = line.find(' ');
        if (space == 64) {
            std::string name = line.substr(space);
            name.erase(0, name.find_first_not_of(" *"));
            if (name == fileName) {
                std::string h = line.substr(0, 64);
                for (char& c : h) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                return h;
            }
        }
        at = end + 1;
    }
    return {};
}

// ── The work, on its own thread ─────────────────────────────────────────────

void UpdateWorker(std::wstring appDir) {
    SetPhase(Phase::Preparing);
    CloseSettings();
    QuitIsland();

    SetPhase(Phase::Checking);
    const bb::update::Release r = bb::update::FetchLatest(BUNBUN_USER_AGENT);
    {
        std::lock_guard lock(g_update.mutex);
        g_update.pageUrl = bb::update::Widen(r.url.empty() ? "https://github.com/" BUNBUN_REPO_STR "/releases/latest" : r.url);
        g_update.version = bb::update::Widen(r.version);
    }
    if (!r.ok) return Fail(L"Couldn't check for the new version: " + bb::update::Widen(r.error) + L".");
    if (r.setupUrl.empty() || r.sumsUrl.empty())
        return Fail(L"This release has no installer to download.");

    std::string sums;
    if (!Download(bb::update::Widen(r.sumsUrl), &sums, nullptr, false))
        return Fail(L"Couldn't download the update. Check your connection and try again.");
    const std::string setupName = "Bunbun-" + r.version + "-setup.exe";
    const std::string expected = ExpectedHash(sums, setupName);
    if (expected.empty()) return Fail(L"The release's checksum list doesn't include the installer.");

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    const std::wstring dir = std::wstring(tempDir) + L"Bunbun-update";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring setup = dir + L"\\" + bb::update::Widen(setupName);

    SetPhase(Phase::Downloading);
    HANDLE f = CreateFileW(setup.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return Fail(L"Couldn't save the update to the temp folder.");
    const bool got = Download(bb::update::Widen(r.setupUrl), nullptr, f, true);
    CloseHandle(f);
    if (!got) {
        DeleteFileW(setup.c_str());
        return Fail(L"Couldn't download the update. Check your connection and try again.");
    }

    SetPhase(Phase::Verifying);
    if (Sha256Hex(setup) != expected) {
        DeleteFileW(setup.c_str());
        return Fail(L"The download didn't match its checksum, so it wasn't installed.");
    }

    // Silent and per-user: no prompts, no admin. It keeps the choices made the first
    // time (start with Windows, Local AI) and never starts Bunbun itself when silent.
    SetPhase(Phase::Installing);
    std::wstring cmd = L"\"" + setup + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCANCEL";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
        return Fail(L"Couldn't start the installer.");
    WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DeleteFileW(setup.c_str());
    if (code != 0) return Fail(L"The installer didn't finish (code " + std::to_wstring(code) + L").");

    SetPhase(Phase::Starting);
    const std::wstring exe = appDir + L"\\bunbun.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, appDir.c_str(), SW_SHOWNORMAL);
    Sleep(900);
    SetPhase(Phase::Done);
    if (g_window) PostMessageW(g_window, WM_CLOSE, 0, 0);
}

// ── The window ──────────────────────────────────────────────────────────────

constexpr int kW = 300, kH = 340;

struct Painter {
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<IDWriteTextFormat> title, small;
    double start = 0.0;

    void Ensure(HWND hwnd) {
        if (!factory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
        if (!dwrite)
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
        if (!title && dwrite) {
            dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", &title);
            dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"", &small);
            for (IDWriteTextFormat* f : {title.Get(), small.Get()})
                if (f) { f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); }
        }
        if (!rt && factory) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
            factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi),
                D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &rt);
        }
    }

    void Paint(HWND hwnd) {
        Ensure(hwnd);
        if (!rt) return;
        Phase phase;
        std::wstring version, error;
        unsigned long long got, total;
        {
            std::lock_guard lock(g_update.mutex);
            phase = g_update.phase;
            version = g_update.version;
            error = g_update.error;
            got = g_update.got;
            total = g_update.total;
        }
        const double now = GetTickCount64() / 1000.0;
        if (start == 0.0) start = now;

        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0x1e1f22));
        ComPtr<ID2D1SolidColorBrush> white, muted, track, fill;
        rt->CreateSolidColorBrush(D2D1::ColorF(0xf2f3f5), &white);
        rt->CreateSolidColorBrush(D2D1::ColorF(0xb5bac1), &muted);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x3a3c42), &track);
        rt->CreateSolidColorBrush(D2D1::ColorF(0xb69cff), &fill);

        // Bun, working while it downloads, happy at the end, sad on a failure.
        const bb::mascot::Mood mood = phase == Phase::Failed ? bb::mascot::Mood::Sad
                                     : phase == Phase::Done || phase == Phase::Starting ? bb::mascot::Mood::Happy
                                     : phase == Phase::Installing ? bb::mascot::Mood::Thinking
                                                                  : bb::mascot::Mood::Working;
        D2D1_POINT_2F mc{};
        float msize = 0.0f;
        bb::mascot::FitToBox(D2D1::RectF(kW * 0.5f - 62, 52, kW * 0.5f + 62, 176), &mc, &msize);
        bb::mascot::Draw(rt.Get(), mc, msize, mood, now - start, 0.0, D2D1::Point2F(0, 0));

        std::wstring line, sub;
        const std::wstring name = version.empty() ? L"Bunbun" : L"Bunbun " + version;
        switch (phase) {
            case Phase::Preparing: line = L"Closing Bunbun\x2026"; break;
            case Phase::Checking: line = L"Checking for updates\x2026"; break;
            case Phase::Downloading: line = L"Downloading " + name + L"\x2026"; break;
            case Phase::Verifying: line = L"Checking the download\x2026"; break;
            case Phase::Installing: line = L"Installing " + name + L"\x2026"; break;
            case Phase::Starting:
            case Phase::Done: line = L"Starting " + name + L"\x2026"; break;
            case Phase::Failed: line = L"The update didn't work"; sub = error + L"\nClick to open the download page."; break;
        }
        if (phase == Phase::Downloading && total > 0) {
            wchar_t buf[64];
            swprintf_s(buf, L"%.1f of %.1f MB", got / 1048576.0, total / 1048576.0);
            sub = buf;
        }
        if (title) rt->DrawTextW(line.c_str(), static_cast<UINT32>(line.size()), title.Get(), D2D1::RectF(16, 200, kW - 16, 222), white.Get());

        if (phase != Phase::Failed) {
            // A determinate bar while downloading; otherwise a short segment sweeping back and forth.
            const D2D1_RECT_F bar = D2D1::RectF(kW * 0.5f - 60, 232, kW * 0.5f + 60, 238);
            rt->FillRoundedRectangle(D2D1::RoundedRect(bar, 3, 3), track.Get());
            float a = 0.0f, b = 0.0f;
            if (phase == Phase::Downloading && total > 0) {
                b = static_cast<float>(static_cast<double>(got) / static_cast<double>(total));
            } else {
                const float t = static_cast<float>(std::fmod((now - start) * 0.7, 1.0));
                const float s = 0.5f - 0.5f * std::cos(t * 6.2831853f);
                a = s * 0.7f;
                b = a + 0.3f;
            }
            const float w = bar.right - bar.left;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bar.left + w * a, bar.top, bar.left + w * std::min(1.0f, b), bar.bottom), 3, 3),
                                     fill.Get());
        }
        if (!sub.empty() && small)
            rt->DrawTextW(sub.c_str(), static_cast<UINT32>(sub.size()), small.Get(),
                          D2D1::RectF(20, phase == Phase::Failed ? 230 : 246, kW - 20, kH - 12), muted.Get());
        if (rt->EndDraw() == D2DERR_RECREATE_TARGET) rt.Reset();
        bb::mascot::ReleaseThreadResources();
    }
};
Painter g_painter;

LRESULT CALLBACK UpdateWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            g_painter.Paint(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCHITTEST: {
            // Drag it anywhere, except after a failure, when a click opens the release page.
            std::lock_guard lock(g_update.mutex);
            if (g_update.phase != Phase::Failed) return HTCAPTION;
            break;
        }
        case WM_LBUTTONUP: {
            std::wstring url;
            {
                std::lock_guard lock(g_update.mutex);
                if (g_update.phase == Phase::Failed) url = g_update.pageUrl;
            }
            if (!url.empty()) {
                ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                std::lock_guard lock(g_update.mutex);
                if (g_update.phase == Phase::Failed) DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE: {
            // Only once it's finished or failed; closing mid-install would leave no Bunbun at all.
            Phase p;
            {
                std::lock_guard lock(g_update.mutex);
                p = g_update.phase;
            }
            if (p == Phase::Done || p == Phase::Failed) DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

namespace bb::updater {

bool CanSelfUpdate() { return FileExists(ExeDir() + L"\\unins000.exe"); }

int Start() {
    if (!CanSelfUpdate()) return 1;
    wchar_t tempDir[MAX_PATH], self[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring dir = std::wstring(tempDir) + L"Bunbun-update";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring copy = dir + L"\\bunbun-updater.exe";
    if (!CopyFileW(self, copy.c_str(), FALSE)) return 1;
    const std::wstring params = L"--run-update \"" + ExeDir() + L"\"";
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", copy.c_str(), params.c_str(), dir.c_str(), SW_SHOWNORMAL)) > 32
               ? 0 : 1;
}

int Run(HINSTANCE instance, const std::wstring& args) {
    // The app folder comes quoted after --run-update.
    std::wstring appDir;
    const size_t q1 = args.find(L'"');
    const size_t q2 = q1 == std::wstring::npos ? q1 : args.find(L'"', q1 + 1);
    if (q2 != std::wstring::npos) appDir = args.substr(q1 + 1, q2 - q1 - 1);
    if (appDir.empty() || !FileExists(appDir + L"\\bunbun.exe")) return 1;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Bun as you set it up (character and colour), like on the island.
    wchar_t character[260] = {}, color[32] = {};
    GetPrivateProfileStringW(L"Bunbun", L"Character", L"bunny", character, ARRAYSIZE(character), bb::SettingsPath());
    GetPrivateProfileStringW(L"Bunbun", L"MascotColor", L"", color, ARRAYSIZE(color), bb::SettingsPath());
    bb::mascot::SetStyle(bb::mascot::StyleFromSettings(character, color, 100, 100, 37, false, true));

    WNDCLASSW wc{};
    wc.lpfnWndProc = UpdateWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"BunbunUpdater";
    RegisterClassW(&wc);

    const UINT dpi = GetDpiForSystem();
    const int w = MulDiv(kW, static_cast<int>(dpi), 96), h = MulDiv(kH, static_cast<int>(dpi), 96);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g_window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Updating Bunbun", WS_POPUP | WS_SYSMENU,
                               (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2, w, h, nullptr, nullptr,
                               instance, nullptr);
    if (!g_window) return 1;
    const DWORD round = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(g_window, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(g_window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    ShowWindow(g_window, SW_SHOWNORMAL);
    SetForegroundWindow(g_window);
    SetTimer(g_window, 1, 16, nullptr);

    std::thread(UpdateWorker, appDir).detach();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

}  // namespace bb::updater
