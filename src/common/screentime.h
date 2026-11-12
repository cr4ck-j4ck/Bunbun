// Bunbun — Screen Time's bookkeeping: seconds per app per day, kept in
// screentime.tsv next to settings.ini, and what the page shows from it (today,
// the apps used, the last seven days). Pure, so it's unit-tested
// (tests/test_core.cpp). The Windows island measures who's in front
// (screentime_module.inc) and finds each app's real name and icon.
//
// screentime.tsv, UTF-8, one line per app per day, tab-separated:
//   yyyymmdd  seconds  key  name  path
// key is "aumid:<id>" for packaged apps (Store, MSIX) and the exe's path in
// lower case for the rest; path is the exe (to find its icon again later).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

namespace bb::screentime {

constexpr int kKeepDays = 8;  // today and the week before it

struct Entry {
    std::wstring name;  // what the page shows ("Google Chrome", "WhatsApp")
    std::wstring path;  // the exe, for its icon
    long long seconds = 0;
};

struct Log {
    std::map<int, std::map<std::wstring, Entry>> days;  // yyyymmdd -> key -> entry
};

// Days since 1970-01-01 for yyyymmdd, and back (Howard Hinnant's algorithms).
inline int64_t DayNumber(int ymd) {
    int y = ymd / 10000, m = ymd / 100 % 100;
    const int d = ymd % 100;
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
inline int FromDayNumber(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int y = static_cast<int>(yoe + era * 400 + (m <= 2));
    return y * 10000 + m * 100 + d;
}
// 0 = Sunday.
inline int Weekday(int ymd) { return static_cast<int>(((DayNumber(ymd) % 7) + 11) % 7); }

inline void Add(Log* log, int day, const std::wstring& key, const std::wstring& name, const std::wstring& path, long long seconds) {
    Entry& e = log->days[day][key];
    if (!name.empty()) e.name = name;
    if (!path.empty()) e.path = path;
    e.seconds += seconds;
}

// Drops days older than the last kKeepDays (today included).
inline void Prune(Log* log, int today) {
    const int64_t oldest = DayNumber(today) - (kKeepDays - 1);
    for (auto it = log->days.begin(); it != log->days.end();)
        it = DayNumber(it->first) < oldest ? log->days.erase(it) : std::next(it);
}

inline std::wstring Clean(std::wstring s) {
    for (wchar_t& c : s)
        if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    return s;
}

inline std::wstring Serialize(const Log& log) {
    std::wstring out;
    for (const auto& [day, apps] : log.days)
        for (const auto& [key, e] : apps)
            out += std::to_wstring(day) + L"\t" + std::to_wstring(e.seconds) + L"\t" + Clean(key) + L"\t" + Clean(e.name) + L"\t" +
                   Clean(e.path) + L"\n";
    return out;
}

inline Log Parse(const std::wstring& text) {
    Log log;
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find(L'\n', at);
        if (nl == std::wstring::npos) nl = text.size();
        std::wstring line = text.substr(at, nl - at);
        at = nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::vector<std::wstring> f;
        size_t s = 0;
        for (;;) {
            const size_t tab = line.find(L'\t', s);
            f.push_back(line.substr(s, tab == std::wstring::npos ? std::wstring::npos : tab - s));
            if (tab == std::wstring::npos) break;
            s = tab + 1;
        }
        if (f.size() < 3) continue;
        const int day = static_cast<int>(wcstol(f[0].c_str(), nullptr, 10));
        const long long secs = wcstoll(f[1].c_str(), nullptr, 10);
        if (day < 19700101 || secs <= 0 || f[2].empty()) continue;
        Add(&log, day, f[2], f.size() > 3 ? f[3] : L"", f.size() > 4 ? f[4] : L"", secs);
    }
    return log;
}

// ── What the page shows ──

struct AppTime {
    std::wstring key, name, path;
    long long seconds = 0;
    int percent = 0;  // of today's total
};

struct DayTotal {
    int day = 0;           // yyyymmdd
    int weekday = 0;       // 0 = Sunday
    long long seconds = 0;
    bool today = false;
};

struct Summary {
    long long todaySeconds = 0;
    int appsUsed = 0;              // apps used a minute or more today
    std::vector<AppTime> apps;     // today (or the day shown), most used first
    std::vector<DayTotal> week;    // the seven days ending today, oldest first
    long long averageSeconds = 0;  // over the days of the week with any use
};

// `shown`: the day whose apps and total to give (a click on the week's bars); 0 = today.
// The week always ends today.
inline Summary Summarize(const Log& log, int today, int shown = 0) {
    Summary s;
    if (auto it = log.days.find(shown ? shown : today); it != log.days.end()) {
        for (const auto& [key, e] : it->second) {
            s.todaySeconds += e.seconds;
            if (e.seconds >= 60) ++s.appsUsed;
            s.apps.push_back({key, e.name.empty() ? key : e.name, e.path, e.seconds, 0});
        }
    }
    std::sort(s.apps.begin(), s.apps.end(), [](const AppTime& a, const AppTime& b) { return a.seconds > b.seconds; });
    for (AppTime& a : s.apps)
        a.percent = s.todaySeconds > 0 ? static_cast<int>((a.seconds * 100 + s.todaySeconds / 2) / s.todaySeconds) : 0;
    long long used = 0;
    int usedDays = 0;
    const int64_t t = DayNumber(today);
    for (int64_t n = t - 6; n <= t; ++n) {
        DayTotal d;
        d.day = FromDayNumber(n);
        d.weekday = Weekday(d.day);
        d.today = n == t;
        if (auto it = log.days.find(d.day); it != log.days.end())
            for (const auto& [key, e] : it->second) d.seconds += e.seconds;
        if (d.seconds > 0) {
            used += d.seconds;
            ++usedDays;
        }
        s.week.push_back(d);
    }
    s.averageSeconds = usedDays ? used / usedDays : 0;
    return s;
}

// "2h 5m", "45m", "<1m".
inline std::wstring Duration(long long seconds) {
    const long long m = seconds / 60;
    if (m < 1) return seconds > 0 ? L"<1m" : L"0m";
    if (m < 60) return std::to_wstring(m) + L"m";
    return std::to_wstring(m / 60) + L"h" + (m % 60 ? L" " + std::to_wstring(m % 60) + L"m" : L"");
}

}  // namespace bb::screentime
