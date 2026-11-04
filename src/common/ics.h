// Bunbun — reading a calendar's private iCal link (.ics): the meetings in it,
// repeating ones expanded, and the next one with its join link. Shared by the
// Windows island (calendar_module.inc) and the Linux island (calendar_linux.cpp);
// each gives the time-zone conversion its system has (ICU on Windows, GLib on Linux).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace bb::ics {

struct WallTime {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
};

// Wall-clock time in `tzid` ("" = local time, "Z" = UTC) to Unix seconds.
using ToEpochFn = std::function<double(const WallTime&, const std::wstring& tzid)>;

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

struct Meeting {
    bool valid = false;
    std::wstring title, joinUrl;
    double startsAt = 0.0, endsAt = 0.0;  // Unix seconds
};

// Days since 1970-01-01 for a civil date, and back (Howard Hinnant's algorithms).
inline int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
inline void CivilFromDays(int64_t z, int* y, int* m, int* d) {
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

// UTC wall time to Unix seconds (no time zone needed).
inline double UtcEpoch(const WallTime& w) {
    return static_cast<double>(DaysFromCivil(w.y, w.mo, w.d)) * 86400.0 + w.h * 3600.0 + w.mi * 60.0 + w.s;
}

inline int ToInt(const std::wstring& s) { return static_cast<int>(wcstol(s.c_str(), nullptr, 10)); }

inline bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix) {
    for (size_t i = 0; prefix[i]; ++i)
        if (i >= s.size() || towupper(s[i]) != towupper(prefix[i])) return false;
    return true;
}

inline std::wstring Unescape(const std::wstring& v) {
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
inline bool ParseStamp(const std::wstring& v, WallTime* w, bool* utc, bool* dateOnly) {
    if (v.size() < 8) return false;
    *w = {};
    w->y = ToInt(v.substr(0, 4));
    w->mo = ToInt(v.substr(4, 2));
    w->d = ToInt(v.substr(6, 2));
    *dateOnly = v.size() < 15;
    if (!*dateOnly) {
        w->h = ToInt(v.substr(9, 2));
        w->mi = ToInt(v.substr(11, 2));
        w->s = ToInt(v.substr(13, 2));
    }
    *utc = !v.empty() && v.back() == L'Z';
    return w->y > 1900 && w->mo >= 1 && w->mo <= 12;
}

// A property line "NAME;PARAM=x;TZID=y:value" split into its parts.
struct Prop {
    std::wstring name, tzid, value;
    bool dateValue = false;
};
inline Prop SplitProp(const std::wstring& line) {
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
        if (StartsWithNoCase(param, L"TZID=")) {
            p.tzid = param.substr(5);
            if (p.tzid.size() >= 2 && p.tzid.front() == L'"') p.tzid = p.tzid.substr(1, p.tzid.size() - 2);
        } else if (param.size() == 10 && StartsWithNoCase(param, L"VALUE=DATE")) {
            p.dateValue = true;
        }
        semi = next;
    }
    return p;
}

inline double PropEpoch(const Prop& p, const ToEpochFn& toEpoch, bool* dateOnly, WallTime* wall = nullptr, std::wstring* zone = nullptr) {
    WallTime w;
    bool utc = false;
    if (!ParseStamp(p.value, &w, &utc, dateOnly)) return 0.0;
    *dateOnly = *dateOnly || p.dateValue;
    const std::wstring tz = utc ? L"Z" : p.tzid;
    if (wall) *wall = w;
    if (zone) *zone = tz;
    return tz == L"Z" ? UtcEpoch(w) : toEpoch(w, tz);
}

// The first meeting link in any of the event's text fields.
inline std::wstring FindJoinLink(const std::wstring& text) {
    static const std::wregex re(
        LR"((https://[^\s"'<>\\]*(meet\.google\.com/|zoom\.us/(j|my|w)/|teams\.microsoft\.com/l/meetup-join|teams\.live\.com/meet|webex\.com/)[^\s"'<>\\]*))",
        std::regex::icase);
    std::wsmatch m;
    return std::regex_search(text, m, re) ? m[1].str() : std::wstring();
}

inline std::vector<Event> Parse(const std::wstring& ics, const ToEpochFn& toEpoch) {
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
            ev.startEpoch = PropEpoch(p, toEpoch, &dateOnly, &ev.start, &ev.tzid);
            ev.allDay = dateOnly;
        } else if (p.name == L"DTEND") {
            endEpoch = PropEpoch(p, toEpoch, &dateOnly);
        } else if (p.name == L"SUMMARY") {
            ev.summary = Unescape(p.value);
        } else if (p.name == L"UID") {
            ev.uid = p.value;
        } else if (p.name == L"STATUS") {
            ev.cancelled = p.value == L"CANCELLED";
        } else if (p.name == L"RECURRENCE-ID") {
            ev.isOverride = true;
            ev.recurrenceId = PropEpoch(p, toEpoch, &dateOnly);
        } else if (p.name == L"EXDATE") {
            // Comma-separated list, all in the same time zone.
            size_t a = 0;
            while (a <= p.value.size()) {
                const size_t b = std::min(p.value.find(L',', a), p.value.size());
                Prop one = p;
                one.value = p.value.substr(a, b - a);
                if (const double e = PropEpoch(one, toEpoch, &dateOnly); e > 0.0) ev.exdates.push_back(e);
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
                else if (key == L"INTERVAL") ev.interval = std::max(1, ToInt(val));
                else if (key == L"COUNT") ev.count = ToInt(val);
                else if (key == L"UNTIL") { Prop u; u.value = val; u.tzid = ev.tzid; ev.until = PropEpoch(u, toEpoch, &dateOnly); }
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

// Every meeting instance overlapping [from, to), repeating ones expanded, in start order.
inline std::vector<Meeting> MeetingsBetween(const std::vector<Event>& events, double from, double to, const ToEpochFn& toEpoch) {
    std::vector<Meeting> out;
    // Instances moved or cancelled one by one carry a RECURRENCE-ID; the series
    // must skip their original times.
    std::vector<std::pair<std::wstring, double>> overridden;
    for (const Event& e : events)
        if (e.isOverride) overridden.emplace_back(e.uid, e.recurrenceId);

    auto consider = [&](const Event& e, double start) {
        const double end = start + e.duration;
        if (end <= from || start >= to) return;
        out.push_back({true, e.summary.empty() ? std::wstring(L"Meeting") : e.summary, e.join, start, end});
    };
    for (const Event& e : events) {
        if (e.isOverride || e.freq.empty() || (e.freq != L"DAILY" && e.freq != L"WEEKLY")) {
            consider(e, e.startEpoch);  // single meetings; other rules: just the first instance
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
            const double start = e.tzid == L"Z" ? UtcEpoch(w) : toEpoch(w, e.tzid);
            if (e.until > 0.0 && start > e.until) break;
            if (e.count > 0 && produced >= e.count) break;
            ++produced;
            if (start + e.duration <= from) continue;
            if (start >= to) break;
            const bool skipped =
                std::any_of(e.exdates.begin(), e.exdates.end(), [&](double x) { return std::fabs(x - start) < 60.0; }) ||
                std::any_of(overridden.begin(), overridden.end(),
                            [&](const auto& o) { return o.first == e.uid && std::fabs(o.second - start) < 60.0; });
            if (!skipped) consider(e, start);
        }
    }
    std::sort(out.begin(), out.end(), [](const Meeting& a, const Meeting& b) { return a.startsAt < b.startsAt; });
    return out;
}

// The earliest meeting that hasn't ended yet, within the next two days.
inline Meeting NextMeeting(const std::vector<Event>& events, double now, const ToEpochFn& toEpoch) {
    const std::vector<Meeting> all = MeetingsBetween(events, now, now + 2.0 * 86400.0, toEpoch);
    return all.empty() ? Meeting{} : all.front();
}

}  // namespace bb::ics
