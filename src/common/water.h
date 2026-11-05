// Bunbun — the water reminder's rules, shared by the Windows and Linux islands.
//
// The day's goal is paced across your active hours: by any minute you'd have drunk
// your share of it. Fall a glass behind that pace and it's time to drink (at least
// kMinGapMinutes after the last reminder, and not while snoozed). With a bottle size
// set, an empty bottle asks for a refill instead. The day starts again at midnight;
// the last kHistoryDays days are kept for the Water page. Pure: no clock, no files.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace bb::water {

constexpr int kMinGapMinutes = 20;
constexpr int kSnoozeMinutes = 15;
constexpr int kHistoryDays = 7;

struct Settings {
    bool reminders = false;  // Bunbun.WaterReminders
    int goalMl = 2000;       // Bunbun.WaterGoalMl
    int glassMl = 250;       // Bunbun.WaterGlassMl
    int bottleMl = 0;        // Bunbun.WaterBottleMl; 0 = no bottle, no refills
    int startHour = 9;       // Bunbun.WaterStartHour
    int endHour = 21;        // Bunbun.WaterEndHour (exclusive)
};

struct Day {
    int date = 0;            // yyyymmdd
    int takenMl = 0;
    int glasses = 0;
    int inBottleMl = -1;     // what's left in the bottle; -1 = full (not refilled yet today)
    int lastRemindMinute = -1000;
    int snoozeUntilMinute = -1;
};

struct HistoryDay {
    int date = 0;
    int ml = 0;
};

struct Log {
    Day today;
    std::vector<HistoryDay> history;  // oldest first, today not included
};

inline int StartMinute(const Settings& s) { return std::clamp(s.startHour, 0, 23) * 60; }
inline int EndMinute(const Settings& s) { return std::max(StartMinute(s) + 60, std::clamp(s.endHour, 1, 24) * 60); }

// What you'd have drunk by `minute` (minutes since midnight) to be on pace.
inline int ExpectedBy(const Settings& s, int minute) {
    const int a = StartMinute(s), b = EndMinute(s);
    if (minute <= a) return 0;
    if (minute >= b) return s.goalMl;
    return static_cast<int>(static_cast<long long>(s.goalMl) * (minute - a) / (b - a));
}

inline int BottleLeft(const Settings& s, const Day& d) {
    if (s.bottleMl <= 0) return 0;
    return d.inBottleMl < 0 ? s.bottleMl : d.inBottleMl;
}

// A new day: yesterday goes into the history (kept to kHistoryDays).
inline void RollOver(Log& log, int date) {
    if (log.today.date == date) return;
    if (log.today.date != 0) {
        log.history.push_back({log.today.date, log.today.takenMl});
        if (log.history.size() > static_cast<size_t>(kHistoryDays)) log.history.erase(log.history.begin());
    }
    log.today = Day{};
    log.today.date = date;
}

inline void Drink(const Settings& s, Day& d, int ml) {
    ml = std::max(0, ml);
    d.takenMl += ml;
    ++d.glasses;
    if (s.bottleMl > 0) d.inBottleMl = std::max(0, BottleLeft(s, d) - ml);
    d.snoozeUntilMinute = -1;
}

// Takes the last glass back (a mis-click).
inline void Undo(const Settings& s, Day& d) {
    if (d.glasses <= 0) return;
    const int ml = std::min(d.takenMl, std::max(1, s.glassMl));
    d.takenMl -= ml;
    --d.glasses;
    if (s.bottleMl > 0) d.inBottleMl = std::min(s.bottleMl, BottleLeft(s, d) + ml);
}

inline void Refill(const Settings& s, Day& d) {
    d.inBottleMl = s.bottleMl > 0 ? s.bottleMl : -1;
    d.snoozeUntilMinute = -1;
}

inline void Snooze(Day& d, int minute) { d.snoozeUntilMinute = minute + kSnoozeMinutes; }

enum class Due { None, Drink, Refill };

// Whether to remind at `minute`. The caller records the reminder (lastRemindMinute)
// when it shows one.
inline Due Check(const Settings& s, const Day& d, int minute) {
    if (!s.reminders || s.goalMl <= 0) return Due::None;
    if (minute < StartMinute(s) || minute >= EndMinute(s)) return Due::None;
    if (d.takenMl >= s.goalMl) return Due::None;
    if (minute < d.snoozeUntilMinute) return Due::None;
    if (minute - d.lastRemindMinute < kMinGapMinutes) return Due::None;
    if (s.bottleMl > 0 && BottleLeft(s, d) <= 0) return Due::Refill;
    const int glass = std::max(1, s.glassMl);
    return ExpectedBy(s, minute) - d.takenMl >= glass ? Due::Drink : Due::None;
}

// When the next reminder is due (minutes since midnight), or -1 for none today.
inline int NextReminder(const Settings& s, const Day& d, int minute) {
    if (!s.reminders || d.takenMl >= s.goalMl) return -1;
    const int a = StartMinute(s), b = EndMinute(s);
    const int glass = std::max(1, s.glassMl);
    // The first minute at which the pace is a glass ahead of what's drunk.
    const long long need = static_cast<long long>(d.takenMl) + glass;
    int due = need >= s.goalMl ? b : a + static_cast<int>((need * (b - a) + s.goalMl - 1) / s.goalMl);
    if (s.bottleMl > 0 && BottleLeft(s, d) <= 0) due = minute;
    due = std::max({due, d.lastRemindMinute + kMinGapMinutes, d.snoozeUntilMinute, minute, a});
    return due < b ? due : -1;
}

// The log file: key=value lines for today, then "h=yyyymmdd,ml" for the history.
inline std::string Serialize(const Log& log) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "date=%d\ntaken=%d\nglasses=%d\nbottle=%d\nreminded=%d\nsnooze=%d\n", log.today.date,
                  log.today.takenMl, log.today.glasses, log.today.inBottleMl, log.today.lastRemindMinute,
                  log.today.snoozeUntilMinute);
    std::string out = buf;
    for (const HistoryDay& h : log.history) {
        std::snprintf(buf, sizeof buf, "h=%d,%d\n", h.date, h.ml);
        out += buf;
    }
    return out;
}

inline Log Parse(const std::string& text) {
    Log log;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = end + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const char* v = line.c_str() + eq + 1;
        if (key == "date") log.today.date = std::atoi(v);
        else if (key == "taken") log.today.takenMl = std::max(0, std::atoi(v));
        else if (key == "glasses") log.today.glasses = std::max(0, std::atoi(v));
        else if (key == "bottle") log.today.inBottleMl = std::atoi(v);
        else if (key == "reminded") log.today.lastRemindMinute = std::atoi(v);
        else if (key == "snooze") log.today.snoozeUntilMinute = std::atoi(v);
        else if (key == "h") {
            HistoryDay h;
            const char* comma = std::strchr(v, ',');
            h.date = std::atoi(v);
            h.ml = comma ? std::max(0, std::atoi(comma + 1)) : 0;
            if (h.date > 0) log.history.push_back(h);
        }
    }
    if (log.history.size() > static_cast<size_t>(kHistoryDays))
        log.history.erase(log.history.begin(), log.history.end() - kHistoryDays);
    return log;
}

}  // namespace bb::water
