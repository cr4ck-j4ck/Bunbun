// Bunbun: Media transport, window / app focusing and the media source menu items.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

void ToggleEndpointMute() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> volume;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if (SUCCEEDED(hr)) {
        hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(volume.GetAddressOf()));
    }
    if (SUCCEEDED(hr)) {
        BOOL muted = FALSE;
        volume->GetMute(&muted);
        volume->SetMute(!muted, nullptr);
        std::lock_guard lock(g_stateMutex);
        g_state.muted = !muted;
        g_state.system.volumeMuted = !muted;
        g_state.volume.active = false;
    }
}

// Shared by the scrubber's live drag updates and its on-release commit.
void SeekMediaToTicks(int64_t targetTicks) {
    std::thread([targetTicks]() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
            auto manager = Manager::RequestAsync().get();
            if (manager) {
                auto sessions = manager.GetSessions();
                std::wstring currentAumid;
                {
                    std::lock_guard lock(g_stateMutex);
                    currentAumid = g_state.media.sourceAppUserModelId;
                }
                winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session = nullptr;
                for (auto const& s : sessions) {
                    if (s.SourceAppUserModelId().c_str() == currentAumid) {
                        session = s;
                        break;
                    }
                }
                if (!session) session = manager.GetCurrentSession();

                if (session) {
                    session.TryChangePlaybackPositionAsync(targetTicks).get();
                }
            }
        } catch (...) {}
    }).detach();
}

// Resolves the SMTC session matching the island's current media source, falling
// back to whatever Windows reports as the current session. Must be called on a
// thread with an initialised apartment.
winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession ResolveMediaSession() {
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    auto manager = Manager::RequestAsync().get();
    if (!manager) {
        return nullptr;
    }

    std::wstring currentAumid;
    {
        std::lock_guard lock(g_stateMutex);
        currentAumid = g_state.media.sourceAppUserModelId;
    }

    if (!currentAumid.empty()) {
        for (auto const& s : manager.GetSessions()) {
            if (s.SourceAppUserModelId().c_str() == currentAumid) {
                return s;
            }
        }
    }
    return manager.GetCurrentSession();
}

// Sends a transport command: 0 = previous, 1 = play/pause, 2 = next.
//
// SMTC is the only mechanism used when a session exists. The global media keys
// are deliberately NOT used as a fallback for a session that merely *reports*
// failure: the shell routes those keys to whatever it considers the current
// session, which is not necessarily the session the island is showing (see
// SelectActiveMediaSession), and a player that acts on the request while still
// answering false would then be driven twice -- skipping two tracks, or toggling
// play/pause straight back. The keys are only synthesized when SMTC could not
// act at all, where there is nothing to double-drive and no better option.
void SendMediaTransportCommand(int cmd) {
    std::thread([cmd]() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        bool sessionAttempted = false;
        bool reportedSuccess = false;
        try {
            auto session = ResolveMediaSession();
            if (session) {
                sessionAttempted = true;
                if (cmd == 0) {
                    reportedSuccess = session.TrySkipPreviousAsync().get();
                } else if (cmd == 1) {
                    reportedSuccess = session.TryTogglePlayPauseAsync().get();
                } else if (cmd == 2) {
                    reportedSuccess = session.TrySkipNextAsync().get();
                }
            }
        } catch (...) {
            sessionAttempted = false;
        }

        if (sessionAttempted) {
            if (!reportedSuccess) {
                // Not escalated to a media key on purpose (see above). Logged so
                // a player that genuinely refuses transport control is
                // diagnosable from the mod log.
                Wh_Log(L"Media transport command %d was refused by the session.", cmd);
            }
            return;
        }

        BYTE vk = 0;
        if (cmd == 0) {
            vk = VK_MEDIA_PREV_TRACK;
        } else if (cmd == 1) {
            vk = VK_MEDIA_PLAY_PAUSE;
        } else if (cmd == 2) {
            vk = VK_MEDIA_NEXT_TRACK;
        }
        if (vk != 0) {
            keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY, 0);
            keybd_event(vk, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
        }
    }).detach();
}

// Bunbun: finding the player's own window, so clicking the island brings it back
// instead of launching it again. The media session names its app by an app ID,
// which is an exe name for most desktop players ("Spotify.exe", "chrome") but a
// hash for Firefox and "Package_hash!App" for Store apps (Spotify from the Store,
// Apple Music), whose windows belong to ApplicationFrameHost. So a window counts
// as the app's when its own app ID, its process's app ID or package, or its exe
// matches. Windows hidden in the tray (closed to the notification area) count
// too, after visible ones: they're shown again rather than started twice.
struct WindowSearch {
    std::wstring targetTitle;
    std::wstring targetApp;
    HWND foundHwnd = nullptr;
    HWND fallbackHwnd = nullptr;   // visible (or minimised) window of the app
    HWND hiddenHwnd = nullptr;     // the app's main window, hidden in the tray
};

std::wstring WindowAppId(HWND hwnd) {
    // PKEY_AppUserModel_ID, spelled out so no extra import library is needed.
    static const PROPERTYKEY kAppIdKey = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
    std::wstring id;
    IPropertyStore* store = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) && store) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(store->GetValue(kAppIdKey, &v)) && v.vt == VT_LPWSTR && v.pwszVal) id = v.pwszVal;
        PropVariantClear(&v);
        store->Release();
    }
    return id;
}

// The process's own app ID ("Package_hash!App") and package family, for Store apps.
void ProcessAppIds(DWORD pid, std::wstring* appId, std::wstring* family) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return;
    wchar_t buf[256] = {};  // app IDs are at most 130 characters
    UINT32 len = ARRAYSIZE(buf);
    if (GetApplicationUserModelId(process, &len, buf) == ERROR_SUCCESS) *appId = buf;
    wchar_t fam[128] = {};  // family names are at most 64
    len = ARRAYSIZE(fam);
    if (GetPackageFamilyName(process, &len, fam) == ERROR_SUCCESS) *family = fam;
    CloseHandle(process);
}

bool WindowBelongsToApp(HWND hwnd, const std::wstring& targetLowerIn) {
    std::wstring target = targetLowerIn;
    if (target.size() >= 2 && target.front() == L'"' && target.back() == L'"') target = target.substr(1, target.size() - 2);
    if (target.empty()) return false;
    if (ToLowerCopy(WindowAppId(hwnd)) == target) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;
    std::wstring appId, family;
    ProcessAppIds(pid, &appId, &family);
    if (!appId.empty() && ToLowerCopy(appId) == target) return true;
    // "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify": its package family is the part before "!".
    const size_t bang = target.find(L'!');
    if (!family.empty() && bang != std::wstring::npos && ToLowerCopy(family) == target.substr(0, bang)) return true;
    std::wstring exePath;
    if (!ProcessImageNameForPid(pid, &exePath)) return false;
    const std::wstring exeLower = ToLowerCopy(exePath);
    if (exeLower == target) return true;
    const size_t slash = exeLower.find_last_of(L"\\/");
    const std::wstring exeName = slash == std::wstring::npos ? exeLower : exeLower.substr(slash + 1);
    return exeName == target || exeName == target + L".exe";
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* search = reinterpret_cast<WindowSearch*>(lParam);
    // Only real app windows: top level, unowned, not tool windows, with a title.
    if (GetWindow(hwnd, GW_OWNER) || (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    wchar_t title[512];
    const int titleLen = GetWindowTextW(hwnd, title, ARRAYSIZE(title));
    const bool visible = IsWindowVisible(hwnd) != FALSE;

    if (visible && titleLen > 0 && !search->targetTitle.empty()) {
        std::wstring wTitle(title);
        // Case-insensitive check if window title contains currently playing media title
        auto it = std::search(
            wTitle.begin(), wTitle.end(),
            search->targetTitle.begin(), search->targetTitle.end(),
            [](wchar_t ch1, wchar_t ch2) { return towlower(ch1) == towlower(ch2); }
        );
        if (it != wTitle.end()) {
            search->foundHwnd = hwnd;
            return FALSE; // found exact title, stop enumerating
        }
    }

    if (search->targetApp.empty() || titleLen == 0) return TRUE;
    if (visible) {
        if (!search->fallbackHwnd && WindowBelongsToApp(hwnd, ToLowerCopy(search->targetApp))) search->fallbackHwnd = hwnd;
    } else if (!search->hiddenHwnd && (GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
        // A hidden main window is big; hidden helper windows of the same app usually aren't.
        RECT r{};
        GetWindowRect(hwnd, &r);
        if (r.right - r.left >= 200 && r.bottom - r.top >= 150 && WindowBelongsToApp(hwnd, ToLowerCopy(search->targetApp)))
            search->hiddenHwnd = hwnd;
    }
    return TRUE;
}

// Bunbun: robustly focus a window bypassing Windows foreground lock
bool FocusWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) hwnd = root;
    // Joining the current foreground window's input queue lets SetForegroundWindow
    // through Windows' foreground lock (attaching to the target's thread doesn't).
    const DWORD curThread = GetCurrentThreadId();
    HWND foreground = GetForegroundWindow();
    const DWORD targetThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : curThread;
    if (curThread != targetThread) {
        AttachThreadInput(curThread, targetThread, TRUE);
    }
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    if (curThread != targetThread) {
        AttachThreadInput(curThread, targetThread, FALSE);
    }
    return true;
}

// Bunbun: focus an active coding agent's window (Antigravity, Cursor, VS Code, Claude, terminal).
// Best match first: the agent's own app with the project in its title, then the
// app alone, then any window titled with the project. Bunbun's own windows never
// count (the settings window is titled "Bunbun", which is also a project name).
bool FocusAgentApp(const bb::AgentView& agent) {
    struct AgentSearch {
        std::wstring project;
        std::wstring agentName;
        DWORD self = 0;
        HWND both = nullptr;
        HWND exeOnly = nullptr;
        HWND titleOnly = nullptr;
    };
    AgentSearch as;
    as.project = ToLowerCopy(agent.project);
    as.agentName = ToLowerCopy(agent.agent);
    as.self = GetCurrentProcessId();

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
        auto* s = reinterpret_cast<AgentSearch*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || pid == s->self) return TRUE;
        wchar_t titleBuf[512];
        const int len = GetWindowTextW(hwnd, titleBuf, ARRAYSIZE(titleBuf));
        const std::wstring t = len > 0 ? ToLowerCopy(std::wstring(titleBuf, static_cast<size_t>(len))) : std::wstring();
        const bool titled = !s->project.empty() && t.find(s->project) != std::wstring::npos;

        std::wstring exePath, exeName;
        if (ProcessImageNameForPid(pid, &exePath)) {
            exeName = ToLowerCopy(exePath);
            const size_t slash = exeName.find_last_of(L"\\/");
            if (slash != std::wstring::npos) exeName = exeName.substr(slash + 1);
        }
        const bool terminal = exeName == L"windowsterminal.exe" || exeName == L"powershell.exe" || exeName == L"pwsh.exe" ||
                              exeName == L"cmd.exe" || exeName == L"wezterm-gui.exe" || exeName == L"alacritty.exe";
        // "Claude Code" contains "code", so Claude is checked before VS Code.
        bool app = false;
        if (s->agentName.find(L"claude") != std::wstring::npos) app = exeName.find(L"claude") != std::wstring::npos || terminal;
        else if (s->agentName.find(L"antigravity") != std::wstring::npos) app = exeName.find(L"antigravity") != std::wstring::npos;
        else if (s->agentName.find(L"cursor") != std::wstring::npos) app = exeName.find(L"cursor") != std::wstring::npos;
        else if (s->agentName.find(L"code") != std::wstring::npos || s->agentName.find(L"copilot") != std::wstring::npos)
            app = exeName == L"code.exe" || exeName == L"code - insiders.exe";
        else if (s->agentName.find(L"terminal") != std::wstring::npos || s->agentName.find(L"cli") != std::wstring::npos ||
                 s->agentName.find(L"codex") != std::wstring::npos || s->agentName.find(L"gemini") != std::wstring::npos)
            app = terminal;

        if (app && titled) {
            s->both = hwnd;
            return FALSE;  // can't do better
        }
        if (app && !s->exeOnly) s->exeOnly = hwnd;
        if (titled && !s->titleOnly) s->titleOnly = hwnd;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&as));

    HWND target = as.both ? as.both : as.exeOnly ? as.exeOnly : as.titleOnly;
    return target ? FocusWindow(target) : false;
}

// Bunbun: focus an app from a toast/notification
bool FocusNotificationApp(const std::wstring& appName, const std::wstring& notifTitle) {
    if (appName.empty() && notifTitle.empty()) return false;
    struct NotifSearch {
        std::wstring app;
        std::wstring title;
        HWND foundHwnd = nullptr;
    };
    NotifSearch ns;
    ns.app = ToLowerCopy(appName);
    ns.title = ToLowerCopy(notifTitle);

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        auto* s = reinterpret_cast<NotifSearch*>(lp);
        DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner == GetCurrentProcessId()) return TRUE;  // Bunbun's own windows
        wchar_t titleBuf[512];
        if (GetWindowTextW(hwnd, titleBuf, ARRAYSIZE(titleBuf)) > 0) {
            std::wstring t = ToLowerCopy(std::wstring(titleBuf));
            if (!s->app.empty() && t.find(s->app) != std::wstring::npos) {
                s->foundHwnd = hwnd;
                return FALSE;
            }
        }
        if (!s->app.empty()) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != 0) {
                std::wstring exePath;
                if (ProcessImageNameForPid(pid, &exePath)) {
                    std::wstring exeLower = ToLowerCopy(exePath);
                    size_t slash = exeLower.find_last_of(L"\\/");
                    std::wstring exeName = (slash != std::wstring::npos) ? exeLower.substr(slash + 1) : exeLower;
                    if (exeName.find(s->app) != std::wstring::npos || s->app.find(exeName) != std::wstring::npos) {
                        s->foundHwnd = hwnd;
                        return FALSE;
                    }
                }
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ns));

    if (ns.foundHwnd) {
        return FocusWindow(ns.foundHwnd);
    }
    return false;
}

void OpenRelevantApp() {
    std::wstring title;
    std::wstring app;
    {
        std::lock_guard lock(g_stateMutex);
        title = g_state.media.title;
        app = g_state.media.sourceAppUserModelId;
    }

    // Try to find and focus window containing track title (ideal for browser playing YouTube/Spotify)
    if (!title.empty() || !app.empty()) {
        WindowSearch search;
        search.targetTitle = title;
        search.targetApp = app;
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));

        // Bunbun: a window hidden in the tray is shown again rather than the app started twice.
        HWND hwndToFocus = search.foundHwnd ? search.foundHwnd : search.fallbackHwnd ? search.fallbackHwnd : search.hiddenHwnd;

        if (hwndToFocus) {
            FocusWindow(hwndToFocus);
            return;
        }
    }

    // Fallback: Launch or focus via AppUserModelId or Path
    if (!app.empty()) {
        std::wstring executePath = app;

        // Remove surrounding quotes if any
        if (executePath.size() >= 2 && executePath.front() == L'"' && executePath.back() == L'"') {
            executePath = executePath.substr(1, executePath.size() - 2);
        }

        bool isFilePath = (executePath.find(L":\\") != std::wstring::npos ||
                           (executePath.size() >= 4 && executePath.substr(executePath.size() - 4) == L".exe"));

        if (isFilePath) {
            if (GetFileAttributesW(executePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                // Path doesn't exist. Try 64-bit Program Files if it was in x86
                size_t x86Pos = executePath.find(L" (x86)");
                if (x86Pos != std::wstring::npos) {
                    std::wstring altPath = executePath;
                    altPath.erase(x86Pos, 6);
                    if (GetFileAttributesW(altPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        executePath = altPath;
                    }
                }

                // If it STILL doesn't exist after trying alternatives, just gracefully abort!
                // Trying to guess 'brave.exe' triggers broken Windows Registry App Paths.
                if (GetFileAttributesW(executePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    return;
                }
            }

            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask = SEE_MASK_FLAG_NO_UI;
            sei.lpFile = executePath.c_str();
            sei.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&sei);
        } else {
            // It's a UWP/Desktop AppUserModelId, launch via AppsFolder
            std::wstring shellPath = L"shell:AppsFolder\\" + executePath;
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask = SEE_MASK_FLAG_NO_UI;
            sei.lpFile = shellPath.c_str();
            sei.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&sei);
        }
        return;
    }

    ShellExecuteW(nullptr, L"open", L"ms-settings:", nullptr, nullptr, SW_SHOWNORMAL);
}

void DismissTransientState() {
    std::lock_guard lock(g_stateMutex);
    g_state.clipboard.active = false;
    g_state.notification.active = false;
    g_state.volume.active = false;
    g_state.brightness.active = false;
    g_state.progress.active = false;
    g_state.capsLock.active = false;
    g_state.device.active = false;
    g_state.bluetoothDevice.active = false;
    g_state.battery.active = false;
    Wh_SetIntValue(L"ProgressPercent", -1);
}

// ── Media source menu (right-click submenu and the dock's "+N" list) ─────────
static constexpr UINT kMediaSourceMenuIdBase = 100;  // 100 = Auto, 101.. = sources

// Shape style submenu: +0 = iPhone pill, +1 = Windows 11 flyout, +2 = macOS notch.
static constexpr UINT kShapeMenuIdBase = 50;

// Lyrics sync submenu: +0 header (disabled), +1..+3 earlier 10/50/250, +4..+6 later 10/50/250, +7 reset.
static constexpr UINT kLyricsSyncMenuIdBase = 60;

// Right-click nudge for the profile that is active right now (Bluetooth or speakers).
// Stored separately from the Mod Settings value; effective offset = setting + nudge.
// Deliberately does not call LoadSettings(), which also resets the pinned state.
void AdjustLyricsNudge(int deltaMs) {
    const bool bt = g_defaultOutputIsBluetooth.load(std::memory_order_relaxed);
    const wchar_t* key = bt ? L"LyricsNudgeBtMs" : L"LyricsNudgeMs";
    const Settings s = GetSettingsCopy();
    const int setting = bt ? s.lyricsOffsetBtMs : s.lyricsOffsetMs;
    // Clamp the nudge so setting + nudge stays in range; repeated presses never pile up invisibly.
    const int nudge = ClampInt(Wh_GetIntValue(key, 0) + deltaMs,
                               -kLyricsOffsetLimitMs - setting, kLyricsOffsetLimitMs - setting);
    Wh_SetIntValue(key, nudge);
    PublishLyricsOffsets(s.lyricsOffsetMs, s.lyricsOffsetBtMs);
    g_layoutDirty = true;
}

void ResetLyricsNudge() {
    const bool bt = g_defaultOutputIsBluetooth.load(std::memory_order_relaxed);
    Wh_SetIntValue(bt ? L"LyricsNudgeBtMs" : L"LyricsNudgeMs", 0);
    const Settings s = GetSettingsCopy();
    PublishLyricsOffsets(s.lyricsOffsetMs, s.lyricsOffsetBtMs);
    g_layoutDirty = true;
}

struct MediaSourceMenuItem {
    std::wstring aumid;
    std::wstring name;
};

// Ticks: "Auto" = automatic mode, an app = the one currently being controlled.
std::vector<MediaSourceMenuItem> AppendMediaSourceItems(HMENU menu) {
    std::vector<MediaSourceMenuItem> items;
    std::wstring active;
    {
        std::lock_guard lock(g_stateMutex);
        active = g_state.media.sourceAppUserModelId;
        for (const MediaSourceInfo& source : g_state.mediaSources) {
            items.push_back(MediaSourceMenuItem{source.aumid, source.name});
        }
    }

    const bool autoMode = GetPreferredMediaSource().empty();
    AppendMenuW(menu, MF_STRING | (autoMode ? MF_CHECKED : 0), kMediaSourceMenuIdBase, Loc(L"Auto"));
    if (!items.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    for (size_t i = 0; i < items.size(); ++i) {
        AppendMenuW(menu, MF_STRING | (items[i].aumid == active ? MF_CHECKED : 0),
                    kMediaSourceMenuIdBase + 1 + static_cast<UINT>(i), items[i].name.c_str());
    }
    return items;
}

