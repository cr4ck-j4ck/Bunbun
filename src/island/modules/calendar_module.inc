// Bunbun: the next meeting from the user's calendar link(s).
//
// Google Calendar and Outlook.com both give each calendar a private iCal link
// ("Secret address in iCal format" / "Publish calendar → ICS"). The user pastes
// it in Settings → Features; this thread fetches it every few minutes over
// HTTPS, expands repeating meetings and publishes the next one with its join
// link. No sign-in and nothing else leaves the PC.
//
// Included in the core's anonymous namespace after SharedState.

HANDLE g_calendarThread = nullptr;

// Unix seconds, from the system clock.
double EpochNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const ULONGLONG t = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<double>(t) / 1e7 - 11644473600.0;
}

namespace Ics {

// ── Time zones ──────────────────────────────────────────────────────────────
// Calendars name time zones the IANA way (Google: "Europe/London") or the
// Windows way (Outlook: "GMT Standard Time"). Windows 10 1903+ ships ICU as
// icu.dll, which understands both; without it, times fall back to local time.
using UCal = void*;
using FnOpen = UCal(__cdecl*)(const wchar_t*, int32_t, const char*, int32_t, int32_t*);
using FnSet = void(__cdecl*)(UCal, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t*);
using FnMillis = double(__cdecl*)(UCal, int32_t*);
using FnClose = void(__cdecl*)(UCal);
using FnWinId = int32_t(__cdecl*)(const wchar_t*, int32_t, const char*, wchar_t*, int32_t, int32_t*);
using FnCanon = int32_t(__cdecl*)(const wchar_t*, int32_t, wchar_t*, int32_t, int8_t*, int32_t*);
struct IcuApi {
    FnOpen open = nullptr;
    FnSet set = nullptr;
    FnMillis millis = nullptr;
    FnClose close = nullptr;
    FnWinId fromWindows = nullptr;
    FnCanon canonical = nullptr;
};
const IcuApi& Icu() {
    static const IcuApi api = [] {
        IcuApi a;
        if (HMODULE m = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
            a.open = reinterpret_cast<FnOpen>(GetProcAddress(m, "ucal_open"));
            a.set = reinterpret_cast<FnSet>(GetProcAddress(m, "ucal_setDateTime"));
            a.millis = reinterpret_cast<FnMillis>(GetProcAddress(m, "ucal_getMillis"));
            a.close = reinterpret_cast<FnClose>(GetProcAddress(m, "ucal_close"));
            a.fromWindows = reinterpret_cast<FnWinId>(GetProcAddress(m, "ucal_getTimeZoneIDForWindowsID"));
            a.canonical = reinterpret_cast<FnCanon>(GetProcAddress(m, "ucal_getCanonicalTimeZoneID"));
        }
        return a;
    }();
    return api;
}

using WallTime = bb::ics::WallTime;  // the parsing itself lives in common/ics.h, shared with Linux

// Wall-clock time in `tzid` ("" = the PC's local time, "Z" = UTC) to Unix seconds.
double ToEpoch(const WallTime& w, const std::wstring& tzid) {
    if (tzid == L"Z") {
        SYSTEMTIME st{static_cast<WORD>(w.y), static_cast<WORD>(w.mo), 0, static_cast<WORD>(w.d),
                      static_cast<WORD>(w.h), static_cast<WORD>(w.mi), static_cast<WORD>(w.s), 0};
        FILETIME ft;
        if (!SystemTimeToFileTime(&st, &ft)) return 0.0;
        const ULONGLONG t = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        return static_cast<double>(t) / 1e7 - 11644473600.0;
    }
    const IcuApi& icu = Icu();
    if (!tzid.empty() && icu.open && icu.set && icu.millis && icu.close) {
        std::wstring zone = tzid;
        int32_t err = 0;
        int8_t isSystem = 0;
        wchar_t buf[96];
        if (icu.canonical && (icu.canonical(zone.c_str(), static_cast<int32_t>(zone.size()), buf, 96, &isSystem, &err), err > 0 || !isSystem)) {
            // Not an IANA name; maybe a Windows one.
            err = 0;
            const int32_t n = icu.fromWindows ? icu.fromWindows(zone.c_str(), static_cast<int32_t>(zone.size()), nullptr, buf, 96, &err) : 0;
            if (err <= 0 && n > 0) zone.assign(buf, static_cast<size_t>(n));
            else zone.clear();
        }
        if (!zone.empty()) {
            err = 0;
            if (UCal cal = icu.open(zone.c_str(), static_cast<int32_t>(zone.size()), nullptr, 0, &err); cal && err <= 0) {
                icu.set(cal, w.y, w.mo - 1, w.d, w.h, w.mi, w.s, &err);
                const double ms = icu.millis(cal, &err);
                icu.close(cal);
                if (err <= 0) return ms / 1000.0;
            }
        }
    }
    // The PC's own time zone.
    SYSTEMTIME local{static_cast<WORD>(w.y), static_cast<WORD>(w.mo), 0, static_cast<WORD>(w.d),
                     static_cast<WORD>(w.h), static_cast<WORD>(w.mi), static_cast<WORD>(w.s), 0};
    SYSTEMTIME utc;
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) return 0.0;
    WallTime u{utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond};
    return ToEpoch(u, L"Z");
}

// ── Parsing (common/ics.h), with the ICU time zones above ──────────────────
using Event = bb::ics::Event;
using bb::ics::CivilFromDays;  // also used by todos_module.inc
using bb::ics::DaysFromCivil;

std::vector<Event> Parse(const std::wstring& ics) { return bb::ics::Parse(ics, ToEpoch); }

// The earliest meeting that hasn't ended yet, within the next two days.
MeetingSnapshot NextMeeting(const std::vector<Event>& events, double now) {
    const bb::ics::Meeting m = bb::ics::NextMeeting(events, now, ToEpoch);
    MeetingSnapshot s;
    s.valid = m.valid;
    s.title = m.title;
    s.joinUrl = m.joinUrl;
    s.startsAt = m.startsAt;
    s.endsAt = m.endsAt;
    return s;
}

// ── Fetching ────────────────────────────────────────────────────────────────
// GET over HTTPS (webcal:// is the same thing). Returns false on any failure.
bool Fetch(std::wstring url, std::wstring* out) {
    if (url.rfind(L"webcal://", 0) == 0) url = L"https://" + url.substr(9);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = ARRAYSIZE(extra);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    const std::wstring object = std::wstring(path) + extra;

    std::string body;
    bool ok = false;
    HINTERNET session = WinHttpOpen(BUNBUN_USER_AGENT L" Calendar", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
        WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);
        if (HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0)) {
            if (HINTERNET req = WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
                DWORD status = 0, size = sizeof(status);
                if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(req, nullptr) &&
                    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                        &status, &size, WINHTTP_NO_HEADER_INDEX) &&
                    status == 200) {
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0 && body.size() < 32u * 1024 * 1024) {
                        std::string chunk(avail, '\0');
                        DWORD read = 0;
                        if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
                        body.append(chunk.data(), read);
                    }
                    ok = body.find("BEGIN:VCALENDAR") != std::string::npos;
                }
                WinHttpCloseHandle(req);
            }
            WinHttpCloseHandle(connect);
        }
        WinHttpCloseHandle(session);
    }
    if (!ok) return false;
    const int n = MultiByteToWideChar(CP_UTF8, 0, body.data(), static_cast<int>(body.size()), nullptr, 0);
    out->resize(static_cast<size_t>(n));
    MultiByteToWideChar(CP_UTF8, 0, body.data(), static_cast<int>(body.size()), out->data(), n);
    return true;
}

}  // namespace Ics

// Fetches every few minutes, re-picks the next meeting every 20 s (so it moves
// on when one ends) and publishes it to g_state.meeting.
DWORD WINAPI CalendarThreadProc(void*) {
    std::vector<Ics::Event> events;
    std::wstring fetchedFor;
    double nextFetch = 0.0;
    bool failed = false;
    for (;;) {
        const std::wstring urls = GetSettingsCopy().calendarIcsUrl;
        const double now = EpochNow();
        if (urls != fetchedFor || now >= nextFetch) {
            std::vector<Ics::Event> fresh;
            bool anyOk = false;
            failed = false;
            std::wstring u;
            for (size_t i = 0; i <= urls.size(); ++i) {  // several links, separated by spaces or new lines
                if (i == urls.size() || iswspace(urls[i]) || urls[i] == L',') {
                    if (!u.empty()) {
                        std::wstring ics;
                        if (Ics::Fetch(u, &ics)) {
                            std::vector<Ics::Event> parsed = Ics::Parse(ics);
                            fresh.insert(fresh.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
                            anyOk = true;
                        } else {
                            failed = true;
                            Wh_Log(L"Calendar: a calendar link couldn't be fetched.");  // the link itself is private; not logged
                        }
                    }
                    u.clear();
                } else {
                    u += urls[i];
                }
            }
            if (anyOk || urls.empty()) events = std::move(fresh);
            fetchedFor = urls;
            nextFetch = now + (failed && !anyOk ? 60.0 : 300.0);
        }
        MeetingSnapshot next = Ics::NextMeeting(events, EpochNow());
        next.fetchFailed = failed;
        {
            std::lock_guard lock(g_stateMutex);
            const bool changed = next.valid != g_state.meeting.valid || next.startsAt != g_state.meeting.startsAt ||
                                 next.title != g_state.meeting.title;
            g_state.meeting = std::move(next);
            if (changed) g_layoutDirty = true;
        }
        if (WaitForSingleObject(g_stopEvent, 20000) != WAIT_TIMEOUT) break;
    }
    return 0;
}

// The calendar pill shows from this many seconds before a meeting until 5 min
// after it started. Clicking it joins (or, without a link, dismisses it).
double g_meetingDismissedStart = 0.0;
bool MeetingPillWanted(const SharedState& state, const Settings& settings, double epochNow) {
    const MeetingSnapshot& m = state.meeting;
    if (!m.valid || settings.meetingAlertMinutes <= 0 || m.startsAt == g_meetingDismissedStart) return false;
    return epochNow >= m.startsAt - settings.meetingAlertMinutes * 60.0 && epochNow < std::min(m.endsAt, m.startsAt + 300.0);
}

// "in 4:12", "in 38 min", "now", "in 2 h".
std::wstring MeetingCountdown(double startsAt, double epochNow) {
    const double left = startsAt - epochNow;
    wchar_t buf[48];
    if (left <= 0.0) return Loc(L"now");
    if (left < 10 * 60) swprintf_s(buf, L"%s %d:%02d", Loc(L"in"), static_cast<int>(left) / 60, static_cast<int>(left) % 60);
    else if (left < 90 * 60) swprintf_s(buf, L"%s %d min", Loc(L"in"), static_cast<int>(left / 60.0 + 0.5));
    else swprintf_s(buf, L"%s %.0f h", Loc(L"in"), left / 3600.0);
    return buf;
}

// Where the calendar tab sits; the page order is user-set (Bunbun.PageOrder).
int CalendarTabIndex(const Settings& settings) {
    return DashboardPageIndex(StudioPage::Calendar, settings);
}

// The "Up next" panel's Join button on the calendar tab, in content pixels. The
// panel is the hero column (x 22-115, y 16 to the bottom - 20); the button is
// its bottom 30-8 px (see DrawCalendarUpNext).
bool CalendarUpNextJoinHit(const MediaContentPoint& pt) {
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) return false;
    bool hasLink = false;
    {
        std::lock_guard lock(g_stateMutex);
        hasLink = g_state.meeting.valid && !g_state.meeting.joinUrl.empty();
    }
    const float bottom = pt.height - 20.0f;
    return hasLink && pt.x >= 28.0f && pt.x <= 109.0f && pt.y >= bottom - 30.0f && pt.y <= bottom - 8.0f;
}

void JoinOrDismissMeeting() {
    MeetingSnapshot m;
    {
        std::lock_guard lock(g_stateMutex);
        m = g_state.meeting;
    }
    if (!m.valid) return;
    if (!m.joinUrl.empty()) ShellExecuteW(nullptr, L"open", m.joinUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    g_meetingDismissedStart = m.startsAt;  // either way, the pill has done its job
    g_layoutDirty = true;
}
