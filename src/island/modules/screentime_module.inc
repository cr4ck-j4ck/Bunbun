// Bunbun: Screen Time. How long each app has been in front today and over the
// last week, with every app's real name and its own icon.
//
// The render loop calls Tick about once a second. The window in front counts
// while you're at the PC: not while the screen is locked, not on the desktop or
// taskbar, and not after two idle minutes, unless the app is full screen (a film,
// a game). A Store app's frame (ApplicationFrameHost) counts as the app inside it.
//
// Each app is a key: "aumid:<app id>" for packaged apps (Store, MSIX: WhatsApp,
// the Windows apps), else the exe's path in lower case. Its name and icon are
// looked up once, on a worker thread, and cached:
//   packaged  Windows' own name and logo for that app id (AppInfo),
//   other     the exe's file description ("Google Chrome") and the exe's icon.
// Both work for apps that aren't running any more, so the week's apps keep theirs.
//
// The seconds live in screentime.tsv next to settings.ini (common/screentime.h),
// saved every minute and on exit. Included in the core's anonymous namespace.

namespace screentime {

namespace st = bb::screentime;

std::mutex g_mutex;          // guards g_log, g_names, g_icons and g_queue
st::Log g_log;
bool g_loaded = false;
double g_lastTick = 0.0;
double g_savedAt = 0.0;
bool g_dirty = false;

struct Resolved {
    std::wstring name;
    PixelsRef icon;
    bool done = false;  // looked up (the icon can still be missing)
};
std::map<std::wstring, Resolved> g_resolved;  // key -> name and icon
struct Lookup { std::wstring key, aumid, path; };
std::vector<Lookup> g_queue;
std::condition_variable g_queueCv;
std::thread g_worker;
uint64_t g_iconGeneration = 1;  // bumps when an icon arrives, so the page redraws its bitmaps

std::wstring FilePath() {
    std::wstring dir = bb::SettingsPath();
    dir.erase(dir.find_last_of(L"\\/"));
    return dir + L"\\screentime.tsv";
}

int Today() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return t.wYear * 10000 + t.wMonth * 100 + t.wDay;
}

// ── Saving ──

void LoadLocked() {
    if (g_loaded) return;
    g_loaded = true;
    HANDLE f = CreateFileW(FilePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string bytes;
    char buf[8192];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) bytes.append(buf, got);
    CloseHandle(f);
    const int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), n);
    g_log = st::Parse(text);
    st::Prune(&g_log, Today());
}

void SaveLocked() {
    const std::wstring text = st::Serialize(g_log);
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), n, nullptr, nullptr);
    const std::wstring path = FilePath(), tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    const bool ok = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) && wrote == bytes.size();
    CloseHandle(f);
    if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    g_dirty = false;
}

void Save() {
    std::lock_guard lock(g_mutex);
    if (g_dirty) SaveLocked();
}

// ── Names and icons (worker thread) ──

// The exe's own description ("Google Chrome", "Discord"), else its product name.
std::wstring ExeDescription(const std::wstring& path) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    struct Lang { WORD lang, codepage; }* langs = nullptr;
    UINT len = 0;
    std::vector<std::pair<WORD, WORD>> tries;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &len))
        for (UINT i = 0; i < len / sizeof(Lang); ++i) tries.emplace_back(langs[i].lang, langs[i].codepage);
    tries.emplace_back(0x0409, 0x04b0);
    tries.emplace_back(0x0409, 0x04e4);
    for (const wchar_t* field : {L"FileDescription", L"ProductName"}) {
        for (const auto& [lang, cp] : tries) {
            wchar_t query[96];
            swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", lang, cp, field);
            wchar_t* value = nullptr;
            UINT vlen = 0;
            if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &vlen) && value && vlen > 1) {
                std::wstring v(value);
                while (!v.empty() && iswspace(v.back())) v.pop_back();
                if (!v.empty()) return v;
            }
        }
    }
    return {};
}

// The exe's icon at 64 px (sharp at the page's 20 px on a high-DPI screen).
PixelsRef ExeIcon(const std::wstring& path) {
    HICON icon = nullptr;
    UINT id = 0;
    using PrivateExtractIconsW_t = UINT(WINAPI*)(LPCWSTR, int, int, int, HICON*, UINT*, UINT, UINT);
    static auto extract = reinterpret_cast<PrivateExtractIconsW_t>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "PrivateExtractIconsW"));
    if (!(extract && extract(path.c_str(), 0, 64, 64, &icon, &id, 1, 0) == 1 && icon)) {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) icon = sfi.hIcon;
    }
    if (!icon) return {};
    PixelsRef px = IconToPixelsRef(icon, 64);
    DestroyIcon(icon);
    return px;
}

void Resolve(const Lookup& job) {
    Resolved r;
    r.done = true;
    if (!job.aumid.empty()) {
        // A packaged app: Windows knows its name and logo by its app id.
        try {
            const auto info = winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(job.aumid);
            if (info) {
                const auto display = info.DisplayInfo();
                r.name = display.DisplayName().c_str();
                const std::vector<uint8_t> bytes = ReadWinRtStreamBytes(display.GetLogo({64.0f, 64.0f}));
                BitmapPixels decoded;
                if (!bytes.empty() && DecodeImageBytesToPixels(bytes, &decoded) && TrimTransparentMargin(&decoded))
                    r.icon = PixelsRef(std::move(decoded));
            }
        } catch (...) {
        }
    }
    if (!job.path.empty()) {
        if (r.name.empty()) r.name = ExeDescription(job.path);
        if (!r.icon) r.icon = ExeIcon(job.path);
    }
    if (r.name.empty()) r.name = StripExtension(BaseNameFromPath(job.path.empty() ? job.key : job.path));
    std::lock_guard lock(g_mutex);
    if (r.icon) ++g_iconGeneration;
    g_resolved[job.key] = r;
    // The name goes into the log too, so the week shows it before a lookup.
    for (auto& [day, apps] : g_log.days)
        if (auto it = apps.find(job.key); it != apps.end() && it->second.name != r.name) {
            it->second.name = r.name;
            g_dirty = true;
        }
    g_layoutDirty = true;
}

void WorkerLoop() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    for (;;) {
        Lookup job;
        {
            std::unique_lock lock(g_mutex);
            g_queueCv.wait(lock, [] { return !g_queue.empty(); });
            job = g_queue.front();
            g_queue.erase(g_queue.begin());
        }
        Resolve(job);
    }
}

// Asks for a key's name and icon unless they're known or already asked for. Locked.
void WantLocked(const std::wstring& key, const std::wstring& path) {
    if (g_resolved.count(key)) return;
    g_resolved[key] = Resolved{};  // asked for
    Lookup job{key, key.rfind(L"aumid:", 0) == 0 ? key.substr(6) : L"", path};
    g_queue.push_back(job);
    if (!g_worker.joinable()) g_worker = std::thread(WorkerLoop), g_worker.detach();
    g_queueCv.notify_one();
}

// ── Who's in front ──

// A Store app's frame window belongs to ApplicationFrameHost; the app itself is
// the child window from another process.
struct FrameChild { DWORD host; DWORD found; };
BOOL CALLBACK FindFrameChild(HWND child, LPARAM param) {
    auto* fc = reinterpret_cast<FrameChild*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(child, &pid);
    if (pid && pid != fc->host) {
        fc->found = pid;
        return FALSE;
    }
    return TRUE;
}

// The app in front as a key, plus its exe; false when nothing should count.
bool FrontApp(std::wstring* key, std::wstring* path) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_hwnd || !IsWindowVisible(fg)) return false;
    wchar_t title[256] = {};
    GetWindowTextW(fg, title, ARRAYSIZE(title));
    if (IsIgnorableForegroundWindow(fg, title)) return false;  // desktop, taskbar, Bunbun
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    std::wstring exe;
    if (!pid || !ProcessImageNameForPid(pid, &exe)) return false;
    std::wstring base = ToLowerCopy(BaseNameFromPath(exe));
    if (base == L"applicationframehost.exe") {
        FrameChild fc{pid, 0};
        EnumChildWindows(fg, FindFrameChild, reinterpret_cast<LPARAM>(&fc));
        if (!fc.found || !ProcessImageNameForPid(fc.found, &exe)) return false;
        pid = fc.found;
        base = ToLowerCopy(BaseNameFromPath(exe));
    }
    if (base == L"lockapp.exe" || base == L"logonui.exe") return false;  // the lock screen
    std::wstring aumid, family;
    ProcessAppIds(pid, &aumid, &family);
    *key = aumid.empty() ? ToLowerCopy(exe) : L"aumid:" + aumid;
    *path = exe;
    return true;
}

bool UserAway() {
    LASTINPUTINFO li{sizeof(li)};
    if (!GetLastInputInfo(&li)) return false;
    return GetTickCount() - li.dwTime > 2 * 60 * 1000;
}

// About once a second, from the render loop.
void Tick(double now) {
    const double dt = g_lastTick > 0.0 ? now - g_lastTick : 0.0;
    g_lastTick = now;
    if (!g_settings.screenTime) return;
    std::wstring key, path;
    const bool counting = dt > 0.0 && dt < 5.0 && FrontApp(&key, &path) && (!UserAway() || IsForegroundFullscreen(g_hwnd));
    std::lock_guard lock(g_mutex);
    LoadLocked();
    if (counting) {
        const int today = Today();
        long long& acc = g_log.days[today][key].seconds;  // whole seconds, the rest carried
        static double carry = 0.0;
        carry += dt;
        const long long whole = static_cast<long long>(carry);
        carry -= static_cast<double>(whole);
        acc += whole;
        st::Entry& e = g_log.days[today][key];
        if (e.path.empty()) e.path = path;
        if (e.name.empty())
            if (auto it = g_resolved.find(key); it != g_resolved.end() && it->second.done) e.name = it->second.name;
        WantLocked(key, path);
        st::Prune(&g_log, today);
        g_dirty = true;
    }
    if (g_dirty && now - g_savedAt > 60.0) {
        g_savedAt = now;
        SaveLocked();
    }
}

// ── For the page ──

struct View {
    st::Summary summary;
    std::vector<PixelsRef> icons;  // one per summary.apps entry (may be empty)
    uint64_t iconGeneration = 0;
    int shownDay = 0;  // 0 = today, else the yyyymmdd the page shows
};

// The day the page shows (a click on one of the week's bars); 0 = today. Render thread.
// It goes back to today two minutes after the last click, so the page doesn't open on an old day.
int g_shownDay = 0;
double g_shownAt = 0.0;

void ShowDay(int day) {
    g_shownDay = day == Today() || day == g_shownDay ? 0 : day;  // the same bar again: back to today
    g_shownAt = NowSeconds();
    g_layoutDirty = true;
}

View GetView() {
    std::lock_guard lock(g_mutex);
    LoadLocked();
    View v;
    if (g_shownDay && NowSeconds() - g_shownAt > 120.0) g_shownDay = 0;
    v.summary = st::Summarize(g_log, Today(), g_shownDay);
    bool inWeek = false;  // a day that has left the week (midnight passed) shows today again
    for (const st::DayTotal& d : v.summary.week) inWeek |= d.day == g_shownDay;
    if (g_shownDay && !inWeek) {
        g_shownDay = 0;
        v.summary = st::Summarize(g_log, Today());
    }
    v.shownDay = g_shownDay;
    for (const st::AppTime& a : v.summary.apps) {
        WantLocked(a.key, a.path);
        auto it = g_resolved.find(a.key);
        v.icons.push_back(it != g_resolved.end() ? it->second.icon : PixelsRef());
    }
    v.iconGeneration = g_iconGeneration;
    return v;
}

}  // namespace screentime
