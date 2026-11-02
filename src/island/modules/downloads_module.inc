// Bunbun: browser download progress, from the Downloads folder.
//
// Browsers don't share their download progress with other apps, but they all
// write an in-progress file next to the final one: Chrome / Edge / Brave /
// Vivaldi ".crdownload", Firefox ".part", Opera ".opdownload". Watching those
// gives the name, size so far and speed; when the partial file turns into the
// real one, the pill says it's done. The total size isn't knowable this way,
// so the bar is indeterminate.
//
// Included in the core's anonymous namespace after SharedState.

HANDLE g_downloadsThread = nullptr;

namespace Downloads {

bool IsPartial(const std::wstring& name, std::wstring* finalName) {
    for (const wchar_t* ext : {L".crdownload", L".part", L".partial", L".opdownload", L".download"}) {
        const size_t n = wcslen(ext);
        if (name.size() > n && _wcsicmp(name.c_str() + name.size() - n, ext) == 0) {
            *finalName = name.substr(0, name.size() - n);
            return true;
        }
    }
    return false;
}

std::wstring DownloadsFolder() {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) out = path;
    CoTaskMemFree(path);
    return out;
}

struct Partial {
    std::wstring file;       // the in-progress file name
    std::wstring finalName;  // what it becomes
    ULONG64 size = 0;
    ULONG64 written = 0;     // last-write time, for picking the newest
};

std::vector<Partial> ScanPartials(const std::wstring& dir) {
    std::vector<Partial> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        Partial p;
        p.file = fd.cFileName;
        if (!IsPartial(p.file, &p.finalName)) continue;
        p.size = (static_cast<ULONG64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        p.written = (static_cast<ULONG64>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
        out.push_back(std::move(p));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// Chrome names a download "Unconfirmed 123456.crdownload" until the user (or the
// browser) settles on a name.
std::wstring DisplayName(const std::wstring& finalName) {
    if (finalName.rfind(L"Unconfirmed ", 0) == 0) return Loc(L"New download");
    return finalName;
}

}  // namespace Downloads

DWORD WINAPI DownloadsThreadProc(void*) {
    using namespace Downloads;
    const std::wstring dir = DownloadsFolder();
    if (dir.empty()) return 0;
    HANDLE change = FindFirstChangeNotificationW(dir.c_str(), FALSE,
                                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
    std::wstring tracking;          // in-progress file we're following
    std::wstring trackingFinal;
    ULONG64 lastSize = 0;
    double lastAt = 0.0;
    double speed = 0.0;
    for (;;) {
        // Poll every second while something downloads; otherwise wait for the folder to change.
        const HANDLE waits[2] = {g_stopEvent, change};
        const DWORD timeout = tracking.empty() ? INFINITE : 1000;
        const DWORD r = WaitForMultipleObjects(change != INVALID_HANDLE_VALUE ? 2 : 1, waits, FALSE,
                                               change != INVALID_HANDLE_VALUE ? timeout : 1000);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
        if (r == WAIT_OBJECT_0 + 1) FindNextChangeNotification(change);
        if (!GetSettingsCopy().downloadsModule) {
            tracking.clear();
            continue;
        }

        const std::vector<Partial> partials = ScanPartials(dir);
        const double now = NowSeconds();
        auto current = std::find_if(partials.begin(), partials.end(), [&](const Partial& p) { return p.file == tracking; });
        if (!tracking.empty() && current == partials.end()) {
            // The partial file is gone: renamed to the real file (done), or cancelled.
            // Chrome renames "Unconfirmed 123.crdownload" to the real name first, so
            // follow a new partial of the same size rather than calling it done.
            std::wstring finalPath = dir + L"\\" + trackingFinal;
            const bool done = GetFileAttributesW(finalPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
                              trackingFinal.rfind(L"Unconfirmed ", 0) != 0;
            const auto renamed = std::find_if(partials.begin(), partials.end(), [&](const Partial& p) { return p.size >= lastSize; });
            if (!done && renamed != partials.end()) {
                tracking = renamed->file;
                trackingFinal = renamed->finalName;
                continue;
            }
            {
                std::lock_guard lock(g_stateMutex);
                DownloadSnapshot& d = g_state.download;
                d.active = done;
                d.finished = done;
                d.path = done ? finalPath : std::wstring();
                d.name = DisplayName(trackingFinal);
                d.bytesPerSecond = 0.0;
                d.expiresAt = now + 6.0;
            }
            g_layoutDirty = true;
            tracking.clear();
            continue;
        }
        if (tracking.empty() && !partials.empty()) {
            // Follow the newest download; the pill counts the others.
            const auto newest = std::max_element(partials.begin(), partials.end(),
                                                 [](const Partial& a, const Partial& b) { return a.written < b.written; });
            tracking = newest->file;
            trackingFinal = newest->finalName;
            lastSize = newest->size;
            lastAt = now;
            speed = 0.0;
            current = newest;
        }
        if (tracking.empty()) continue;
        if (now - lastAt >= 0.5) {
            const double instant = (static_cast<double>(current->size) - static_cast<double>(lastSize)) / (now - lastAt);
            speed = speed <= 0.0 ? std::max(0.0, instant) : speed * 0.6 + std::max(0.0, instant) * 0.4;
            lastSize = current->size;
            lastAt = now;
        }
        trackingFinal = current->finalName;
        {
            std::lock_guard lock(g_stateMutex);
            DownloadSnapshot& d = g_state.download;
            d.active = true;
            d.finished = false;
            d.name = DisplayName(trackingFinal);
            d.bytes = current->size;
            d.bytesPerSecond = speed;
            d.others = static_cast<int>(partials.size()) - 1;
        }
        g_layoutDirty = true;
    }
    if (change != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change);
    return 0;
}

bool DownloadPillWanted(const SharedState& state, const Settings& settings, double now) {
    const DownloadSnapshot& d = state.download;
    return settings.downloadsModule && d.active && (!d.finished || now < d.expiresAt);
}

// "12.4 MB", "1.2 GB".
std::wstring FormatBytes(double bytes) {
    wchar_t buf[32];
    if (bytes >= 1e9) swprintf_s(buf, L"%.1f GB", bytes / 1e9);
    else if (bytes >= 1e6) swprintf_s(buf, L"%.1f MB", bytes / 1e6);
    else swprintf_s(buf, L"%.0f KB", bytes / 1e3);
    return buf;
}

// Clicking a finished download shows it in its folder (opening it straight away
// could run something the user hasn't looked at yet).
bool RevealDownload() {
    std::wstring path;
    {
        std::lock_guard lock(g_stateMutex);
        if (!g_state.download.finished) return false;
        path = g_state.download.path;
        g_state.download.active = false;
    }
    if (!path.empty()) {
        const std::wstring args = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
    g_layoutDirty = true;
    return true;
}
