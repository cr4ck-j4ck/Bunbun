// Bunbun: privacy indicators. Which apps are using the microphone, the camera
// or the screen right now, and a short pill naming an app when it starts.
//
// Microphone: the live audio capture sessions on every recording device. Any
// app actually recording shows here (Discord, OBS, games, browsers), which the
// ConsentStore records below miss for many desktop apps. ConsentStore still
// adds apps that record without an audio session.
// Camera: Media Foundation's sensor activity monitor (Windows 10 2004+), which
// reports live which processes stream from each camera. ConsentStore is only
// a fallback: on recent Windows builds it stops recording desktop apps.
// Screen: Windows has no live API for "who is capturing the screen", so this
// combines what can be read live: OBS's own log (recording / streaming /
// replay buffer), the "… is sharing your screen" bar Chromium browsers open
// while sharing, and ConsentStore as a fallback (stale on recent builds).
//
// Runs on its own thread every 1.5 s, so it keeps working while the render loop
// sleeps. Included in the core's anonymous namespace after the ConsentStore
// helpers.

HANDLE g_privacyThread = nullptr;
HANDLE g_privacyWake = nullptr;  // the camera monitor reports a change

namespace Privacy {

// ── Camera: the sensor activity monitor ─────────────────────────────────────
// mfidl.h's IMFSensorActivity* interfaces, declared here because the MinGW
// headers don't have them. MFCreateSensorActivityMonitor lives in
// mfsensorgroup.dll and needs MFStartup first.
struct IBbSensorProcessActivity : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetProcessId(ULONG* pid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStreamingState(BOOL* streaming) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStreamingMode(int* mode) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetReportTime(FILETIME* time) = 0;
};
struct IBbSensorActivityReport : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetFriendlyName(LPWSTR name, ULONG cch, ULONG* written) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSymbolicLink(LPWSTR link, ULONG cch, ULONG* written) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessCount(ULONG* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessActivity(ULONG index, IBbSensorProcessActivity** activity) = 0;
};
struct IBbSensorActivitiesReport : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(ULONG* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetActivityReport(ULONG index, IBbSensorActivityReport** report) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetActivityReportByDeviceName(LPCWSTR name, IBbSensorActivityReport** report) = 0;
};
struct IBbSensorActivitiesReportCallback : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE OnActivitiesReport(IBbSensorActivitiesReport* report) = 0;
};
struct IBbSensorActivityMonitor : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Start() = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
};
constexpr IID kIID_SensorReportCallback = {0xDE5072EE, 0xDBE3, 0x46DC, {0x8A, 0x87, 0xB6, 0xF6, 0x31, 0x19, 0x47, 0x51}};

std::mutex g_cameraMutex;
std::vector<DWORD> g_cameraPids;  // processes streaming from any camera, from the last report

// Lives for the whole run (a static), so its reference count is decorative.
struct CameraReportCallback final : IBbSensorActivitiesReportCallback {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == kIID_SensorReportCallback) {
            *out = static_cast<IBbSensorActivitiesReportCallback*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE OnActivitiesReport(IBbSensorActivitiesReport* report) override {
        std::vector<DWORD> pids;
        ULONG devices = 0;
        if (report && SUCCEEDED(report->GetCount(&devices))) {
            for (ULONG i = 0; i < devices; ++i) {
                ComPtr<IBbSensorActivityReport> device;
                ULONG processes = 0;
                if (FAILED(report->GetActivityReport(i, &device)) || !device || FAILED(device->GetProcessCount(&processes))) continue;
                for (ULONG k = 0; k < processes; ++k) {
                    ComPtr<IBbSensorProcessActivity> activity;
                    ULONG pid = 0;
                    BOOL streaming = FALSE;
                    if (SUCCEEDED(device->GetProcessActivity(k, &activity)) && activity && SUCCEEDED(activity->GetProcessId(&pid)) &&
                        SUCCEEDED(activity->GetStreamingState(&streaming)) && streaming &&
                        std::find(pids.begin(), pids.end(), pid) == pids.end()) {
                        pids.push_back(pid);
                    }
                }
            }
        }
        {
            std::lock_guard lock(g_cameraMutex);
            g_cameraPids = std::move(pids);
        }
        if (g_privacyWake) SetEvent(g_privacyWake);
        return S_OK;
    }
};

ComPtr<IBbSensorActivityMonitor> StartCameraMonitor() {
    static CameraReportCallback callback;
    ComPtr<IBbSensorActivityMonitor> monitor;
    HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE group = LoadLibraryExW(L"mfsensorgroup.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using FnStartup = HRESULT(WINAPI*)(ULONG, DWORD);
    using FnCreate = HRESULT(WINAPI*)(IBbSensorActivitiesReportCallback*, IBbSensorActivityMonitor**);
    const auto startup = plat ? reinterpret_cast<FnStartup>(GetProcAddress(plat, "MFStartup")) : nullptr;
    const auto create = group ? reinterpret_cast<FnCreate>(GetProcAddress(group, "MFCreateSensorActivityMonitor")) : nullptr;
    constexpr ULONG kMfVersion = 0x00020070;  // MF_VERSION
    static const bool started = startup && SUCCEEDED(startup(kMfVersion, 0));  // once per run; monitors are re-created
    if (!started || !create || FAILED(create(&callback, &monitor)) || !monitor || FAILED(monitor->Start())) {
        Wh_Log(L"Privacy: camera activity monitor unavailable; using ConsentStore only.");
        return nullptr;
    }
    return monitor;
}

void AddUnique(std::vector<std::wstring>* list, const std::wstring& name) {
    if (!name.empty() && std::find(list->begin(), list->end(), name) == list->end()) list->push_back(name);
}

// "C:\Program Files\Discord\Discord.exe" -> "Discord", through the same name
// cleaner the ConsentStore keys use (they spell paths with '#').
std::wstring ProcessName(DWORD pid) {
    std::wstring name;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2];
        DWORD size = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(p, 0, path, &size)) {
            std::wstring hashed(path, size);
            std::replace(hashed.begin(), hashed.end(), L'\\', L'#');
            name = CleanActiveAppName(hashed);
        }
        CloseHandle(p);
    }
    return name;
}

// Apps with an active capture session on any microphone.
void CaptureSessionApps(std::vector<std::wstring>* out) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return;
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices))) return;
    UINT count = 0;
    devices->GetCount(&count);
    const DWORD self = GetCurrentProcessId();
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(devices->Item(i, &device)) ||
            FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.GetAddressOf()))) ||
            FAILED(manager->GetSessionEnumerator(&sessions))) {
            continue;
        }
        int n = 0;
        sessions->GetCount(&n);
        for (int k = 0; k < n; ++k) {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> control2;
            if (FAILED(sessions->GetSession(k, &control)) || FAILED(control.As(&control2))) continue;
            AudioSessionState state = AudioSessionStateInactive;
            if (FAILED(control2->GetState(&state)) || state != AudioSessionStateActive) continue;
            if (control2->IsSystemSoundsSession() == S_OK) continue;
            DWORD pid = 0;
            if (FAILED(control2->GetProcessId(&pid)) || pid == 0 || pid == self) continue;
            AddUnique(out, ProcessName(pid));
        }
    }
}

// Every app ConsentStore marks as using `capability` right now (HKCU and HKLM,
// packaged and desktop apps).
void ConsentStoreApps(const wchar_t* capability, std::vector<std::wstring>* out) {
    const std::wstring base = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\") + capability;
    constexpr REGSAM kSam = KEY_READ | KEY_WOW64_64KEY;
    auto scan = [&](HKEY parent, bool nonPackaged, auto& self) -> void {
        wchar_t name[256];
        for (DWORD i = 0;; ++i) {
            DWORD len = ARRAYSIZE(name);
            if (RegEnumKeyExW(parent, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            HKEY sub;
            if (RegOpenKeyExW(parent, name, 0, kSam, &sub) != ERROR_SUCCESS) continue;
            if (!nonPackaged && _wcsicmp(name, L"NonPackaged") == 0) self(sub, true, self);
            else if (ConsentStoreEntryInUse(sub)) AddUnique(out, CleanActiveAppName(name));
            RegCloseKey(sub);
        }
    };
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        HKEY key;
        if (RegOpenKeyExW(root, base.c_str(), 0, kSam, &key) == ERROR_SUCCESS) {
            scan(key, false, scan);
            RegCloseKey(key);
        }
    }
}

// ── Screen: OBS ─────────────────────────────────────────────────────────────
// OBS logs every output start and stop ("==== Recording Start ===="). The newest
// log is the running session's; OBS keeps it open while it runs, so a file we
// can open exclusively means OBS isn't running and nothing is live.
void ObsOutputs(std::vector<std::wstring>* out) {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return;
    const std::wstring dir = std::wstring(roaming) + L"\\obs-studio\\logs\\";
    CoTaskMemFree(roaming);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"*.txt").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    std::wstring newest;
    ULONG64 newestTime = 0;
    do {
        const ULONG64 t = (static_cast<ULONG64>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
        if (t > newestTime) { newestTime = t; newest = fd.cFileName; }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    if (newest.empty()) return;
    const std::wstring path = dir + newest;
    HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {  // nobody has it open: OBS has quit
        CloseHandle(probe);
        return;
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    constexpr LONGLONG kTail = 512 * 1024;  // a session's markers are near the end
    LARGE_INTEGER from{};
    from.QuadPart = std::max<LONGLONG>(0, size.QuadPart - kTail);
    SetFilePointerEx(f, from, nullptr, FILE_BEGIN);
    std::string text(static_cast<size_t>(size.QuadPart - from.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(f, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(f);
    text.resize(read);
    // Each output's state is whichever marker came last.
    struct Output { const char* name; const wchar_t* label; };
    const Output outputs[] = {{"Recording", L"recording"}, {"Streaming", L"streaming"}, {"Replay Buffer", L"recording"}};
    for (const Output& o : outputs) {
        const size_t start = text.rfind(std::string("==== ") + o.name + " Start");
        const size_t stop = text.rfind(std::string("==== ") + o.name + " Stop");
        if (start != std::string::npos && (stop == std::string::npos || stop < start))
            AddUnique(out, std::wstring(L"OBS Studio (") + o.label + L")");
    }
}

// ── Screen: browsers sharing a screen, window or tab ────────────────────────
// Chromium browsers (Google Meet in Chrome or Edge, and the like) open a small
// "<site> is sharing your screen." bar for as long as they share; its window title
// says so. Firefox shows a "Sharing Indicator" window instead.
void BrowserSharingBars(std::vector<std::wstring>* out) {
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        wchar_t title[256];
        const int n = GetWindowTextW(hwnd, title, ARRAYSIZE(title));
        if (n <= 0) return TRUE;
        const std::wstring t(title, static_cast<size_t>(n));
        size_t at = std::wstring::npos;
        for (const wchar_t* marker : {L" is sharing your screen", L" is sharing a window", L" is sharing an application", L" is sharing this tab"}) {
            at = t.find(marker);
            if (at != std::wstring::npos) break;
        }
        if (at == std::wstring::npos) {
            if (t.find(L"Sharing Indicator") == std::wstring::npos) return TRUE;
            DWORD fpid = 0;
            GetWindowThreadProcessId(hwnd, &fpid);
            if (_wcsnicmp(ProcessName(fpid).c_str(), L"Firefox", 7) == 0)
                AddUnique(reinterpret_cast<std::vector<std::wstring>*>(param), L"Firefox");
            return TRUE;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        std::wstring app = ProcessName(pid);
        const std::wstring site = t.substr(0, at);
        if (!site.empty() && site.size() < 60) app += L" (" + site + L")";
        AddUnique(reinterpret_cast<std::vector<std::wstring>*>(param), app);
        return TRUE;
    }, reinterpret_cast<LPARAM>(out));
}

// ── Screen: dedicated screen recorders ──────────────────────────────────────
// Recorders that capture the screen their own way and only run while recording:
// Xbox Game Bar's capture server, Camtasia's recorder, and the ffmpeg ShareX
// starts for a screen recording. Sharing and recording light the same blue dot.
void ScreenRecorders(std::vector<std::wstring>* out) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    std::vector<std::pair<DWORD, std::wstring>> procs;  // pid, exe
    std::vector<DWORD> parents;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        procs.emplace_back(pe.th32ProcessID, pe.szExeFile);
        parents.push_back(pe.th32ParentProcessID);
    }
    CloseHandle(snap);
    for (size_t i = 0; i < procs.size(); ++i) {
        const std::wstring& exe = procs[i].second;
        if (_wcsicmp(exe.c_str(), L"bcastdvr.exe") == 0) AddUnique(out, L"Xbox Game Bar (recording)");
        else if (_wcsicmp(exe.c_str(), L"CamRecorder.exe") == 0) AddUnique(out, L"Camtasia (recording)");
        else if (_wcsicmp(exe.c_str(), L"ffmpeg.exe") == 0) {
            for (const auto& [pid, name] : procs)
                if (pid == parents[i] && _wcsicmp(name.c_str(), L"ShareX.exe") == 0) AddUnique(out, L"ShareX (recording)");
        }
    }
}

// "Discord", "Discord & OBS Studio", "Discord, OBS Studio +1".
std::wstring JoinNames(const std::vector<std::wstring>& names) {
    if (names.empty()) return {};
    if (names.size() == 1) return names[0];
    if (names.size() == 2) return names[0] + L" & " + names[1];
    return names[0] + L", " + names[1] + L" +" + std::to_wstring(names.size() - 2);
}

}  // namespace Privacy

// Room the privacy dots need at a pill's right edge, in content pixels; 0 with
// none lit. DrawPrivacyDots puts the first dot's centre 20 px in and each next
// one 13 px further left; pills keep ~14 px of their own padding, so this is
// what's left to clear the dots with a small gap.
float PrivacyDotsReserve(const SharedState& state, const Settings& settings) {
    if (!settings.privacyDots) return 0.0f;
    const int lit = (state.system.micActive && settings.privacyDotsMic ? 1 : 0) +
                    (state.system.cameraActive && settings.privacyDotsCam ? 1 : 0) +
                    (state.system.screenActive && settings.privacyDotsScreen ? 1 : 0);
    return lit ? 6.0f + 13.0f * static_cast<float>(lit) : 0.0f;
}

void UpdatePrivacyIndicators() {
    using namespace Privacy;
    const Settings settings = GetSettingsCopy();
    std::vector<std::wstring> mic, cam, screen;
    if (settings.privacyDots && settings.privacyDotsMic) {
        CaptureSessionApps(&mic);
        ConsentStoreApps(L"microphone", &mic);
    }
    if (settings.privacyDots && settings.privacyDotsCam) {
        std::vector<DWORD> pids;
        {
            std::lock_guard lock(g_cameraMutex);
            pids = g_cameraPids;
        }
        for (DWORD pid : pids) AddUnique(&cam, ProcessName(pid));
        ConsentStoreApps(L"webcam", &cam);
    }
    // Screen capture, shared or recorded, lights one blue dot: Windows' capture
    // API (Snipping Tool's recorder, browsers sharing a screen, Teams), OBS's
    // outputs, and the dedicated recorders above.
    if (settings.privacyDots && settings.privacyDotsScreen) {
        ObsOutputs(&screen);
        BrowserSharingBars(&screen);
        ScreenRecorders(&screen);
        ConsentStoreApps(L"graphicsCaptureProgrammatic", &screen);
        ConsentStoreApps(L"graphicsCaptureWithoutBorder", &screen);
    }

    // Name an app the moment it starts using something: "Discord is using your microphone".
    static std::vector<std::wstring> s_mic, s_cam, s_screen;
    std::wstring alert;
    int alertKind = 0;
    // Bunbun: no alert for Bunbun's own use (the Mirror page's camera); the dot still lights.
    static const std::wstring s_self = ProcessName(GetCurrentProcessId());
    auto started = [&](const std::vector<std::wstring>& now, const std::vector<std::wstring>& before, int kind, const wchar_t* what) {
        for (const std::wstring& app : now) {
            if (std::find(before.begin(), before.end(), app) != before.end() || !alert.empty()) continue;
            if (_wcsicmp(app.c_str(), s_self.c_str()) == 0 || _wcsnicmp(app.c_str(), L"bunbun", 6) == 0) continue;
            alert = app + L" " + Loc(L"is using your") + L" " + Loc(what);
            if (kind == 2) {
                // Bunbun: the screen says how: streaming (OBS live), recording, or sharing.
                const std::wstring how = app.find(L"(streaming)") != std::wstring::npos ? L"is streaming your screen"
                                         : app.find(L"(recording)") != std::wstring::npos ? L"is recording your screen"
                                                                                          : L"is sharing your screen";
                std::wstring name = app;
                if (const size_t paren = name.find(L" ("); paren != std::wstring::npos &&
                    (name.find(L"(streaming)") != std::wstring::npos || name.find(L"(recording)") != std::wstring::npos))
                    name = name.substr(0, paren);
                alert = name + L" " + Loc(how.c_str());
            }
            alertKind = kind;
        }
    };
    started(cam, s_cam, 1, L"camera");
    started(mic, s_mic, 0, L"microphone");
    started(screen, s_screen, 2, L"screen");
    const bool changed = mic != s_mic || cam != s_cam || screen != s_screen;
    s_mic = mic;
    s_cam = cam;
    s_screen = screen;

    {
        std::lock_guard lock(g_stateMutex);
        g_state.system.micActive = !mic.empty();
        g_state.system.cameraActive = !cam.empty();
        g_state.system.screenActive = !screen.empty();
        g_state.system.micApp = JoinNames(mic);
        g_state.system.cameraApp = JoinNames(cam);
        g_state.system.screenApp = JoinNames(screen);
        if (!alert.empty()) {
            g_state.privacyAlert.active = true;
            g_state.privacyAlert.text = alert;
            g_state.privacyAlert.kind = alertKind;
            g_state.privacyAlert.expiresAt = NowSeconds() + 3.5;
        }
    }
    if (changed) {
        g_layoutDirty = true;
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);  // wake a sleeping render loop
    }
}

DWORD WINAPI PrivacyThreadProc(void*) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    g_privacyWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<Privacy::IBbSensorActivityMonitor> camera = Privacy::StartCameraMonitor();
    double cameraRefreshedAt = NowSeconds();
    const HANDLE waits[2] = {g_stopEvent, g_privacyWake};
    for (;;) {
        // Windows reports a camera starting, but not always one stopping, which left
        // the dot lit after the camera was turned off. While any use is recorded, a
        // fresh monitor every few seconds re-reads the real state: a new monitor
        // always starts with a full report (an empty one when nothing streams).
        bool cameraInUse = false;
        {
            std::lock_guard lock(Privacy::g_cameraMutex);
            cameraInUse = !Privacy::g_cameraPids.empty();
        }
        if (camera && cameraInUse && NowSeconds() - cameraRefreshedAt > 3.0) {
            camera->Stop();
            camera = Privacy::StartCameraMonitor();
            cameraRefreshedAt = NowSeconds();
        }
        UpdatePrivacyIndicators();
        const DWORD r = WaitForMultipleObjects(g_privacyWake ? 2 : 1, waits, FALSE, 1500);
        if (r != WAIT_TIMEOUT && r != WAIT_OBJECT_0 + 1) break;  // stop, or a failed wait
    }
    if (camera) camera->Stop();
    camera.Reset();
    if (g_privacyWake) {
        CloseHandle(g_privacyWake);
        g_privacyWake = nullptr;
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}
