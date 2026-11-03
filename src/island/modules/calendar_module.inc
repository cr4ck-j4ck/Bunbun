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

struct WallTime {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
};

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

// Days since 1970-01-01 for a civil date, and back (Howard Hinnant's algorithms).
int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void CivilFromDays(int64_t z, int* y, int* m, int* d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    *d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    *m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    *y = static_cast<int>(yoe + era * 400 + (*m <= 2));
}

// ── Parsing ─────────────────────────────────────────────────────────────────
struct Event {
    std::wstring uid, summary, join, tzid;
    WallTime start;
    double startEpoch = 0.0;
    double duration = 0.0;
    bool allDay = false;
    bool cancelled = false;
    bool isOverride = false;  // carries a RECURRENCE-ID
    double recurrenceId = 0.0;
    // RRULE (DAILY / WEEKLY only; enough for meetings)
    std::wstring freq;
    int interval = 1;
    int count = 0;
    double until = 0.0;
    int byDayMask = 0;  // bit 0 = Sunday
    std::vector<double> exdates;
};

std::wstring Unescape(const std::wstring& v) {
    std::wstring out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == L'\\' && i + 1 < v.size()) {
            const wchar_t n = v[++i];
            out += (n == L'n' || n == L'N') ? L'\n' : n;
        } else {
            out += v[i];
        }
    }
    return out;
}

// "20261102T093000Z", "20261102T093000" or "20261102".
bool ParseStamp(const std::wstring& v, WallTime* w, bool* utc, bool* dateOnly) {
    if (v.size() < 8) return false;
    *w = {};
    w->y = _wtoi(v.substr(0, 4).c_str());
    w->mo = _wtoi(v.substr(4, 2).c_str());
    w->d = _wtoi(v.substr(6, 2).c_str());
    *dateOnly = v.size() < 15;
    if (!*dateOnly) {
        w->h = _wtoi(v.substr(9, 2).c_str());
        w->mi = _wtoi(v.substr(11, 2).c_str());
        w->s = _wtoi(v.substr(13, 2).c_str());
    }
    *utc = !v.empty() && v.back() == L'Z';
    return w->y > 1900 && w->mo >= 1 && w->mo <= 12;
}

// A property line "NAME;PARAM=x;TZID=y:value" split into its parts.
struct Prop {
    std::wstring name, tzid, value;
    bool dateValue = false;
};
Prop SplitProp(const std::wstring& line) {
    Prop p;
    size_t colon = std::wstring::npos;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {  // the first colon outside quotes ends the parameters
        if (line[i] == L'"') quoted = !quoted;
        if (line[i] == L':' && !quoted) { colon = i; break; }
    }
    if (colon == std::wstring::npos) return p;
    const std::wstring head = line.substr(0, colon);
    p.value = line.substr(colon + 1);
    size_t semi = head.find(L';');
    p.name = head.substr(0, semi);
    while (semi != std::wstring::npos) {
        const size_t next = head.find(L';', semi + 1);
        const std::wstring param = head.substr(semi + 1, next == std::wstring::npos ? std::wstring::npos : next - semi - 1);
        if (_wcsnicmp(param.c_str(), L"TZID=", 5) == 0) {
            p.tzid = param.substr(5);
            if (p.tzid.size() >= 2 && p.tzid.front() == L'"') p.tzid = p.tzid.substr(1, p.tzid.size() - 2);
        } else if (_wcsicmp(param.c_str(), L"VALUE=DATE") == 0) {
            p.dateValue = true;
        }
        semi = next;
    }
    return p;
}

double PropEpoch(const Prop& p, bool* dateOnly, WallTime* wall = nullptr, std::wstring* zone = nullptr) {
    WallTime w;
    bool utc = false;
    if (!ParseStamp(p.value, &w, &utc, dateOnly)) return 0.0;
    *dateOnly = *dateOnly || p.dateValue;
    const std::wstring tz = utc ? L"Z" : p.tzid;
    if (wall) *wall = w;
    if (zone) *zone = tz;
    return ToEpoch(w, tz);
}

// The first meeting link in any of the event's text fields.
std::wstring FindJoinLink(const std::wstring& text) {
    static const std::wregex re(
        LR"((https://[^\s"'<>\\]*(meet\.google\.com/|zoom\.us/(j|my|w)/|teams\.microsoft\.com/l/meetup-join|teams\.live\.com/meet|webex\.com/)[^\s"'<>\\]*))",
        std::regex::icase);
    std::wsmatch m;
    return std::regex_search(text, m, re) ? m[1].str() : std::wstring();
}

std::vector<Event> Parse(const std::wstring& ics) {
    // Unfold: a line starting with a space or tab continues the previous one.
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos < ics.size()) {
        size_t end = ics.find(L'\n', pos);
        if (end == std::wstring::npos) end = ics.size();
        std::wstring line = ics.substr(pos, end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty() && (line[0] == L' ' || line[0] == L'\t') && !lines.empty()) lines.back() += line.substr(1);
        else lines.push_back(std::move(line));
        pos = end + 1;
    }

    std::vector<Event> events;
    Event ev;
    bool in = false;
    double endEpoch = 0.0;
    std::wstring text;  // everything a join link could hide in
    for (const std::wstring& line : lines) {
        if (line == L"BEGIN:VEVENT") { ev = Event{}; in = true; endEpoch = 0.0; text.clear(); continue; }
        if (!in) continue;
        if (line == L"END:VEVENT") {
            in = false;
            if (ev.startEpoch <= 0.0 || ev.allDay || ev.cancelled) continue;
            ev.duration = endEpoch > ev.startEpoch ? endEpoch - ev.startEpoch : 30.0 * 60.0;
            ev.join = FindJoinLink(text);
            events.push_back(std::move(ev));
            continue;
        }
        const Prop p = SplitProp(line);
        bool dateOnly = false;
        if (p.name == L"DTSTART") {
            ev.startEpoch = PropEpoch(p, &dateOnly, &ev.start, &ev.tzid);
            ev.allDay = dateOnly;
        } else if (p.name == L"DTEND") {
            endEpoch = PropEpoch(p, &dateOnly);
        } else if (p.name == L"SUMMARY") {
            ev.summary = Unescape(p.value);
        } else if (p.name == L"UID") {
            ev.uid = p.value;
        } else if (p.name == L"STATUS") {
            ev.cancelled = p.value == L"CANCELLED";
        } else if (p.name == L"RECURRENCE-ID") {
            ev.isOverride = true;
            ev.recurrenceId = PropEpoch(p, &dateOnly);
        } else if (p.name == L"EXDATE") {
            // Comma-separated list, all in the same time zone.
            size_t a = 0;
            while (a <= p.value.size()) {
                const size_t b = std::min(p.value.find(L',', a), p.value.size());
                Prop one = p;
                one.value = p.value.substr(a, b - a);
                if (const double e = PropEpoch(one, &dateOnly); e > 0.0) ev.exdates.push_back(e);
                a = b + 1;
            }
        } else if (p.name == L"RRULE") {
            size_t a = 0;
            while (a <= p.value.size()) {
                const size_t b = std::min(p.value.find(L';', a), p.value.size());
                const std::wstring part = p.value.substr(a, b - a);
                const size_t eq = part.find(L'=');
                const std::wstring key = part.substr(0, eq), val = eq == std::wstring::npos ? L"" : part.substr(eq + 1);
                if (key == L"FREQ") ev.freq = val;
                else if (key == L"INTERVAL") ev.interval = std::max(1, _wtoi(val.c_str()));
                else if (key == L"COUNT") ev.count = _wtoi(val.c_str());
                else if (key == L"UNTIL") { Prop u; u.value = val; u.tzid = ev.tzid; ev.until = PropEpoch(u, &dateOnly); }
                else if (key == L"BYDAY") {
                    static const wchar_t* days[] = {L"SU", L"MO", L"TU", L"WE", L"TH", L"FR", L"SA"};
                    for (int d = 0; d < 7; ++d)
                        if (val.find(days[d]) != std::wstring::npos) ev.byDayMask |= 1 << d;
                }
                a = b + 1;
            }
        }
        if (p.name == L"LOCATION" || p.name == L"DESCRIPTION" || p.name == L"URL" ||
            p.name.rfind(L"X-GOOGLE-CONFERENCE", 0) == 0 || p.name.rfind(L"X-MICROSOFT-SKYPETEAMSMEETINGURL", 0) == 0) {
            text += Unescape(p.value);
            text += L'\n';
        }
    }
    return events;
}

// The earliest meeting that hasn't ended yet, within the next two days.
MeetingSnapshot NextMeeting(const std::vector<Event>& events, double now) {
    MeetingSnapshot best;
    const double horizon = now + 2.0 * 86400.0;
    // Instances moved or cancelled one by one carry a RECURRENCE-ID; the series
    // must skip their original times.
    std::vector<std::pair<std::wstring, double>> overridden;
    for (const Event& e : events)
        if (e.isOverride) overridden.emplace_back(e.uid, e.recurrenceId);

    auto consider = [&](const Event& e, double start) {
        const double end = start + e.duration;
        if (end <= now || start > horizon) return;
        if (!best.valid || start < best.startsAt) {
            best.valid = true;
            best.title = e.summary.empty() ? std::wstring(L"Meeting") : e.summary;
            best.joinUrl = e.join;
            best.startsAt = start;
            best.endsAt = end;
        }
    };
    for (const Event& e : events) {
        if (e.isOverride || e.freq.empty()) {
            consider(e, e.startEpoch);
            continue;
        }
        if (e.freq != L"DAILY" && e.freq != L"WEEKLY") {
            consider(e, e.startEpoch);  // other rules: just the first instance
            continue;
        }
        // Walk the days of the series in its own time zone, so 9:00 stays 9:00
        // across daylight-saving changes.
        const int64_t first = DaysFromCivil(e.start.y, e.start.mo, e.start.d);
        const int64_t firstWeek = (first + 4) / 7;  // weeks start on Sunday; 1970-01-01 was a Thursday
        const int startDow = static_cast<int>((first + 4) % 7);
        const int mask = e.byDayMask ? e.byDayMask : (1 << startDow);
        int produced = 0;
        for (int64_t day = first; day <= first + 3660; ++day) {
            const int dow = static_cast<int>((day + 4) % 7);
            if (e.freq == L"DAILY") {
                if ((day - first) % e.interval != 0) continue;
            } else {
                if (((day + 4) / 7 - firstWeek) % e.interval != 0 || !(mask & (1 << dow))) continue;
            }
            WallTime w = e.start;
            CivilFromDays(day, &w.y, &w.mo, &w.d);
            const double start = ToEpoch(w, e.tzid);
            if (e.until > 0.0 && start > e.until) break;
            if (e.count > 0 && produced >= e.count) break;
            ++produced;
            if (start + e.duration <= now) continue;
            if (start > horizon) break;
            const bool skipped =
                std::any_of(e.exdates.begin(), e.exdates.end(), [&](double x) { return std::fabs(x - start) < 60.0; }) ||
                std::any_of(overridden.begin(), overridden.end(),
                            [&](const auto& o) { return o.first == e.uid && std::fabs(o.second - start) < 60.0; });
            if (!skipped) consider(e, start);
        }
    }
    return best;
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
