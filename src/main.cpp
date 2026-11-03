// Bunbun — a Dynamic Island for Windows with a bunny who watches your agents.
//
// Process layout:
//   main thread    tray icon, settings-file watcher, single-instance guard
//   island core    render thread + module threads (island/island_core.cpp)
//   agent bridge   named-pipe server for bunbun-hook.exe (agents/agent_bridge.cpp)
//
//   bunbun.exe                     run the island
//   bunbun.exe --install-hooks     connect Claude Code, then exit
//   bunbun.exe --uninstall-hooks   disconnect Claude Code, then exit
//   bunbun.exe --connect <agent> [--quiet]     claude, antigravity, cursor, copilot
//   bunbun.exe --disconnect <agent> [--quiet] [--if-ours]   (--if-ours: only if its hooks call this copy)
//   bunbun.exe --quit              ask a running Bunbun to quit cleanly, then exit
//   bunbun.exe --replace           quit a running Bunbun, then start this one
//                                  (scripts/dev.sh uses it to push a fresh build live)
//   bunbun.exe --enable-local-ai   turn Local AI on in settings.ini, then exit (the
//                                  installer uses it after downloading a model)
#include <windows.h>
#include <d2d1.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <ctime>
#include <string>
#include <thread>

#include "agents/agent_view.h"
#include "common/update.h"
#include "agents/hook_installer.h"
#include "island/island.h"
#include "island/wh_shim.h"
#include "mascot/mascot.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WM_TRAY = WM_APP + 0x500;
constexpr UINT WM_BUNBUN_QUIT = WM_APP + 0x501;  // sent by `bunbun.exe --quit`
constexpr UINT WM_BUNBUN_UPDATE = WM_APP + 0x502;  // lParam: UpdateNews*, wParam: 1 = asked by the user
constexpr wchar_t kTrayClass[] = L"BunbunTray";
constexpr UINT kTrayId = 1;
enum MenuId : UINT {
    kMenuAsk = 99,
    kMenuHooks = 100,
    kMenuSettings,
    kMenuFolder,
    kMenuAutostart,
    kMenuUpdate,
    kMenuQuit,
};
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kAppName[] = L"Bunbun";

HWND g_trayWnd = nullptr;
HICON g_icon = nullptr;
UINT g_taskbarCreated = 0;

// ── Update check ────────────────────────────────────────────────────────────
// A worker asks GitHub for the latest release (90 s after start, then daily
// while Bunbun.UpdateCheck is on) and posts the answer here; the tray thread
// owns everything below. Nothing is downloaded: the user gets a notification
// and the release page.
void Balloon(const wchar_t* title, const wchar_t* text);  // below

struct UpdateNews {
    bb::update::Release release;
};
std::wstring g_updateUrl;      // release page of a newer version, empty when up to date
std::wstring g_updateVersion;  // "0.2.0"
bool g_balloonIsUpdate = false;
HANDLE g_updateStop = nullptr;
std::thread g_updateThread;

void PostUpdateCheck(bool manual) {
    std::thread([manual] {
        auto* news = new UpdateNews{bb::update::FetchLatest(BUNBUN_USER_AGENT)};
        if (!g_trayWnd || !PostMessageW(g_trayWnd, WM_BUNBUN_UPDATE, manual ? 1 : 0, reinterpret_cast<LPARAM>(news)))
            delete news;
    }).detach();
}

void UpdateLoop() {
    // Not at startup: give the island its first minute, and don't hit the network
    // at sign-in when Wi-Fi may not be up yet.
    DWORD wait = 90 * 1000;
    while (WaitForSingleObject(g_updateStop, wait) == WAIT_TIMEOUT) {
        wait = 60 * 60 * 1000;  // then look once an hour whether a day has passed
        if (Wh_GetIntSetting(L"Bunbun.UpdateCheck") == 0) continue;
        const int nowMin = static_cast<int>(std::time(nullptr) / 60);
        if (nowMin - Wh_GetIntValue(L"UpdateLastCheck", 0) < 24 * 60) continue;
        Wh_SetIntValue(L"UpdateLastCheck", nowMin);
        PostUpdateCheck(false);
    }
}

void OnUpdateNews(UpdateNews* news, bool manual) {
    const bb::update::Release& r = news->release;
    const std::string current = bb::update::Narrow(WH_MOD_VERSION);
    if (r.ok && bb::update::CompareVersions(r.version, current) > 0) {
        g_updateUrl = bb::update::Widen(r.url);
        g_updateVersion = bb::update::Widen(r.version);
        // Automatic checks announce each new version once; asking always answers.
        const int packed = bb::update::PackVersion(r.version);
        if (manual || Wh_GetIntValue(L"UpdateNotified", 0) != packed) {
            Wh_SetIntValue(L"UpdateNotified", packed);
            g_balloonIsUpdate = true;
            IslandAnnounceUpdate(g_updateVersion.c_str(), g_updateUrl.c_str());  // on the island too
            Balloon((L"Bunbun " + g_updateVersion + L" is available").c_str(),
                    (L"You have " + std::wstring(WH_MOD_VERSION) + L". Click to open the download page.").c_str());
        }
    } else if (r.ok) {
        g_updateUrl.clear();
        g_updateVersion.clear();
        if (manual) {
            g_balloonIsUpdate = false;
            Balloon(L"You're up to date", (L"Bunbun " + std::wstring(WH_MOD_VERSION) + L" is the latest version.").c_str());
        }
    } else if (manual) {
        g_balloonIsUpdate = false;
        Balloon(L"Couldn't check for updates", bb::update::Widen(r.error).c_str());
    }
}

// Renders Bun into a 32x32 icon, so the tray matches the island exactly.
HICON MakeBunIcon(int px) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())))
        return LoadIconW(nullptr, IDI_APPLICATION);

    ComPtr<IWICBitmap> bmp;
    wic->CreateBitmap(px, px, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
    ComPtr<ID2D1RenderTarget> rt;
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (!bmp || FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt)))
        return LoadIconW(nullptr, IDI_APPLICATION);

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    const float s = static_cast<float>(px);
    bb::mascot::Draw(rt.Get(), D2D1::Point2F(s * 0.5f, s * 0.47f), s * 1.05f, bb::mascot::Mood::Idle, 0.5, 0,
                     D2D1::Point2F(0, 0));
    rt->EndDraw();
    bb::mascot::ReleaseThreadResources();  // rt is short-lived; drop the mascot's cached brush

    // WIC pixels -> top-down 32bpp DIB -> icon.
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi;
    bi.bV5Width = px;
    bi.bV5Height = -px;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return LoadIconW(nullptr, IDI_APPLICATION);
    bmp->CopyPixels(nullptr, px * 4, px * px * 4, static_cast<BYTE*>(bits));
    HBITMAP mask = CreateBitmap(px, px, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void AddTrayIcon() {
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = g_trayWnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_icon;
    lstrcpynW(nid.szTip, L"Bunbun", ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void Balloon(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = g_trayWnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = g_icon;
    lstrcpynW(nid.szInfoTitle, title, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

std::wstring SelfPath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof buf;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kAppName, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void SetAutostart(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        const std::wstring cmd = L"\"" + SelfPath() + L"\"";
        RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
}

// Asks first: this edits a file that belongs to Claude Code.
void ToggleHooks(HWND owner) {
    const bool installed = bb::hooks::ClaudeHooksInstalled();
    const std::wstring path = bb::hooks::ClaudeSettingsPath();
    const std::wstring ask = installed
        ? L"Remove Bunbun's hooks from\n" + path + L" ?\n\nA backup is made first."
        : L"Bunbun will add hook entries to\n" + path +
              L"\n\nso Claude Code sessions, approvals and questions show up on the island. "
              L"Your other settings are kept, and a backup is made first.\n\nContinue?";
    if (MessageBoxW(owner, ask.c_str(), L"Bunbun", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    std::wstring report;
    const bool ok = installed ? bb::hooks::UninstallClaudeHooks(&report) : bb::hooks::InstallClaudeHooks(&report);
    MessageBoxW(owner, report.c_str(), L"Bunbun", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void ShowMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Bunbun " WH_MOD_VERSION);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // The shortcut is only advertised when Bunbun actually owns it.
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, kMenuAsk,
                IslandAskHotkeyActive() ? L"Ask Bun…\tCtrl+Alt+B" : L"Ask Bun…");
    AppendMenuW(menu, MF_STRING, kMenuHooks,
                bb::hooks::ClaudeHooksInstalled() ? L"Disconnect Claude Code…" : L"Connect Claude Code…");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings…");
    AppendMenuW(menu, MF_STRING, kMenuFolder, L"Open data folder");
    AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, L"Start with Windows");
    AppendMenuW(menu, MF_STRING, kMenuUpdate,
                g_updateUrl.empty() ? L"Check for updates…" : (L"Update available: Bunbun " + g_updateVersion + L"…").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit Bunbun");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);  // required for the menu to close on click-away
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case kMenuAsk: IslandOpenAsk(); break;
        case kMenuHooks: ToggleHooks(hwnd); break;
        case kMenuSettings: {
            // The settings app sits next to bunbun.exe; without it, fall back to the INI file.
            std::wstring dir = SelfPath();
            dir.resize(dir.find_last_of(L'\\'));
            const std::wstring app = dir + L"\\bunbun-settings.exe";
            bb::EnsureSettingsFile();
            if (GetFileAttributesW(app.c_str()) != INVALID_FILE_ATTRIBUTES)
                ShellExecuteW(nullptr, L"open", app.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
            else
                ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + std::wstring(bb::SettingsPath()) + L"\"").c_str(),
                              nullptr, SW_SHOWNORMAL);
            break;
        }
        case kMenuFolder: {
            std::wstring dir = bb::SettingsPath();
            dir.resize(dir.find_last_of(L'\\'));
            ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case kMenuAutostart: SetAutostart(!AutostartEnabled()); break;
        case kMenuUpdate:
            if (!g_updateUrl.empty()) ShellExecuteW(nullptr, L"open", g_updateUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else PostUpdateCheck(true);
            break;
        case kMenuQuit: PostQuitMessage(0); break;
    }
}

LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAY) {
        const UINT event = LOWORD(lp);
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT)
            ShowMenu(hwnd);
        if (event == NIN_BALLOONUSERCLICK && g_balloonIsUpdate && !g_updateUrl.empty())
            ShellExecuteW(nullptr, L"open", g_updateUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    if (msg == WM_BUNBUN_UPDATE) {
        auto* news = reinterpret_cast<UpdateNews*>(lp);
        OnUpdateNews(news, wp == 1);
        delete news;
        return 0;
    }
    if (msg == g_taskbarCreated && g_taskbarCreated) {  // Explorer restarted
        AddTrayIcon();
        return 0;
    }
    if (msg == WM_BUNBUN_QUIT) {  // same path as "Quit Bunbun": tray icon removed, threads joined
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Asks a running Bunbun to quit through the normal shutdown path, so its tray
// icon disappears and the agent pipe closes. Returns true once it has exited
// (or nothing was running); false if it is still alive after the timeout.
bool QuitRunningInstance(DWORD timeoutMs) {
    HWND tray = FindWindowExW(HWND_MESSAGE, nullptr, kTrayClass, nullptr);
    if (!tray) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    HANDLE proc = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    PostMessageW(tray, WM_BUNBUN_QUIT, 0, 0);
    if (!proc) return true;
    const bool exited = WaitForSingleObject(proc, timeoutMs) == WAIT_OBJECT_0;
    CloseHandle(proc);
    return exited;
}

// `--connect <agent>` / `--disconnect <agent>`: claude, antigravity, cursor or
// copilot. A message box reports the result unless `--quiet` is given (the
// installer and scripts use that; the exit code says whether it worked).
int RunConnect(bool connect, const std::wstring& agent, bool quiet) {
    std::wstring report;
    bool ok = false;
    if (agent == L"claude") ok = connect ? bb::hooks::InstallClaudeHooks(&report) : bb::hooks::UninstallClaudeHooks(&report);
    else if (agent == L"antigravity") ok = connect ? bb::hooks::InstallAntigravityHooks(&report) : bb::hooks::UninstallAntigravityHooks(&report);
    else if (agent == L"cursor") ok = connect ? bb::hooks::InstallCursorHooks(&report) : bb::hooks::UninstallCursorHooks(&report);
    else if (agent == L"copilot" || agent == L"vscode") ok = connect ? bb::hooks::InstallCopilotHooks(&report) : bb::hooks::UninstallCopilotHooks(&report);
    else report = L"Unknown agent \"" + agent + L"\". Use claude, antigravity, cursor or copilot.";
    if (!quiet) MessageBoxW(nullptr, report.c_str(), L"Bunbun", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
    return ok ? 0 : 1;
}

// True if the agent's config calls this copy's bunbun-hook.exe, in any of the
// ways a path can be written there (JSON-escaped backslashes, forward slashes).
// The uninstaller disconnects with --if-ours, so removing the installed copy
// never unhooks another one (a portable or development build).
bool HooksPointHere(const std::wstring& agent) {
    std::wstring config;
    if (agent == L"claude") config = bb::hooks::ClaudeSettingsPath();
    else if (agent == L"antigravity") config = bb::hooks::AntigravitySettingsPath();
    else if (agent == L"cursor") config = bb::hooks::CursorSettingsPath();
    else if (agent == L"copilot" || agent == L"vscode") config = bb::hooks::CopilotHooksPath();
    HANDLE f = CreateFileW(config.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::string text;
    char buf[65536];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) text.append(buf, got);
    CloseHandle(f);
    const std::wstring hookW = bb::hooks::HookExePath();
    std::string hook(WideCharToMultiByte(CP_UTF8, 0, hookW.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, hookW.c_str(), -1, hook.data(), static_cast<int>(hook.size()), nullptr, nullptr);
    hook.resize(hook.size() - 1);
    auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return s; };
    text = lower(text);
    hook = lower(hook);
    std::string escaped, forward = hook;
    for (char c : hook) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
    for (char& c : forward) if (c == '\\') c = '/';
    return text.find(hook) != std::string::npos || text.find(escaped) != std::string::npos ||
           text.find(forward) != std::string::npos;
}

// The word after `flag` in the command line, lower-cased ("" if absent).
std::wstring ArgAfter(const std::wstring& args, const wchar_t* flag) {
    size_t at = args.find(flag);
    if (at == std::wstring::npos) return {};
    at += wcslen(flag);
    while (at < args.size() && args[at] == L' ') ++at;
    std::wstring word;
    while (at < args.size() && args[at] != L' ') word += static_cast<wchar_t>(towlower(args[at++]));
    return word;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring args = cmdLine ? cmdLine : L"";
    const bool quiet = args.find(L"--quiet") != std::wstring::npos;
    if (args.find(L"--install-hooks") != std::wstring::npos) return RunConnect(true, L"claude", quiet);
    if (args.find(L"--uninstall-hooks") != std::wstring::npos) return RunConnect(false, L"claude", quiet);
    if (args.find(L"--connect") != std::wstring::npos) return RunConnect(true, ArgAfter(args, L"--connect"), quiet);
    if (args.find(L"--disconnect") != std::wstring::npos) {
        const std::wstring agent = ArgAfter(args, L"--disconnect");
        if (args.find(L"--if-ours") != std::wstring::npos && !HooksPointHere(agent)) return 0;
        return RunConnect(false, agent, quiet);
    }
    if (args.find(L"--quit") != std::wstring::npos) return QuitRunningInstance(10000) ? 0 : 1;
    if (args.find(L"--enable-local-ai") != std::wstring::npos) {
        // Only this one key is written; every other setting keeps its live default.
        bb::EnsureSettingsFile();
        return WritePrivateProfileStringW(L"Bunbun", L"LocalAi", L"true", bb::SettingsPath()) ? 0 : 1;
    }
    if (args.find(L"--replace") != std::wstring::npos && !QuitRunningInstance(10000)) return 1;

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\Bunbun.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Bunbun is already running — look for the bunny in the tray.", L"Bunbun",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bb::EnsureSettingsFile();

    WNDCLASSW wc{};
    wc.lpfnWndProc = TrayProc;
    wc.hInstance = instance;
    wc.lpszClassName = kTrayClass;
    RegisterClassW(&wc);
    g_trayWnd = CreateWindowW(wc.lpszClassName, L"Bunbun", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_trayWnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);
    g_icon = MakeBunIcon(GetSystemMetrics(SM_CXSMICON) * 2);
    AddTrayIcon();

    if (!IslandStart()) {
        MessageBoxW(nullptr, L"The island could not start.", L"Bunbun", MB_OK | MB_ICONERROR);
        return 1;
    }
    bb::agents::Start();
    g_updateStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_updateThread = std::thread(UpdateLoop);
    if (!bb::hooks::ClaudeHooksInstalled()) {
        Balloon(L"Hi, I'm Bun!", L"Right-click me in the tray and choose “Connect Claude Code” to see your agents on the island.");
    }

    // Live settings: reload when settings.ini changes (debounced; editors
    // often write a file several times in a row).
    std::wstring dir = bb::SettingsPath();
    dir.resize(dir.find_last_of(L'\\'));
    HANDLE watch = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    FILETIME lastWrite{};
    ULONGLONG reloadAt = 0;

    MSG msg;
    bool quit = false;
    while (!quit) {
        const DWORD timeout = reloadAt ? 300 : INFINITE;
        const DWORD r = MsgWaitForMultipleObjects(watch != INVALID_HANDLE_VALUE ? 1 : 0, &watch, FALSE, timeout, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0 && watch != INVALID_HANDLE_VALUE) {
            WIN32_FILE_ATTRIBUTE_DATA fa{};
            if (GetFileAttributesExW(bb::SettingsPath(), GetFileExInfoStandard, &fa) &&
                CompareFileTime(&fa.ftLastWriteTime, &lastWrite) != 0) {
                lastWrite = fa.ftLastWriteTime;
                reloadAt = GetTickCount64() + 250;
            }
            FindNextChangeNotification(watch);
        }
        if (reloadAt && GetTickCount64() >= reloadAt) {
            reloadAt = 0;
            IslandReloadSettings();
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    SetEvent(g_updateStop);
    if (g_updateThread.joinable()) g_updateThread.join();
    bb::agents::Stop();
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = g_trayWnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    IslandStop();
    if (watch != INVALID_HANDLE_VALUE) FindCloseChangeNotification(watch);
    ReleaseMutex(single);
    return 0;
}
