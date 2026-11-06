// Bunbun — the water reminder on Linux. See water_linux.h.
#include "water_linux.h"

#include <glib.h>

#include <ctime>

#include "../../common/water.h"
#include "features_linux.h"
#include "platform.h"
#include "settings.h"

namespace bb::lwater {
namespace {

namespace w = bb::water;

w::Settings g_settings;
bool g_page = true;
w::Log g_log;
bool g_loaded = false;

std::string FilePath() { return bb::plat::StateDir() + "/water.txt"; }

int Today(int* minute) {
    const time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    if (minute) *minute = lt.tm_hour * 60 + lt.tm_min;
    return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}

void Save() { bb::plat::WriteFileAtomic(FilePath(), w::Serialize(g_log)); }

// Loads the file once, and starts a new day when the date has changed.
w::Day& Day(int* minute) {
    if (!g_loaded) {
        g_loaded = true;
        std::string text;
        bb::plat::ReadFile(FilePath(), &text);
        g_log = w::Parse(text);
    }
    const int date = Today(minute);
    if (g_log.today.date != date) {
        w::RollOver(g_log, date);
        Save();
    }
    return g_log.today;
}

}  // namespace

void LoadSettings() {
    namespace s = bb::settings;
    g_page = s::GetBool(L"Bunbun.WaterPage");
    g_settings.reminders = s::GetBool(L"Bunbun.WaterReminders");
    g_settings.goalMl = std::clamp(s::GetInt(L"Bunbun.WaterGoalMl"), 250, 8000);
    g_settings.glassMl = std::clamp(s::GetInt(L"Bunbun.WaterGlassMl"), 50, 2000);
    g_settings.bottleMl = std::clamp(s::GetInt(L"Bunbun.WaterBottleMl"), 0, 5000);
    g_settings.startHour = std::clamp(s::GetInt(L"Bunbun.WaterStartHour"), 0, 23);
    g_settings.endHour = std::clamp(s::GetInt(L"Bunbun.WaterEndHour"), 1, 24);
}

bool PageOn() { return g_page; }

Snapshot Now() {
    int minute = 0;
    const w::Day& d = Day(&minute);
    Snapshot snap;
    snap.goalMl = g_settings.goalMl;
    snap.glassMl = g_settings.glassMl;
    snap.bottleMl = g_settings.bottleMl;
    snap.takenMl = d.takenMl;
    snap.glasses = d.glasses;
    snap.bottleLeftMl = w::BottleLeft(g_settings, d);
    snap.reminders = g_settings.reminders;
    snap.nextMinute = w::NextReminder(g_settings, d, minute);
    for (const w::HistoryDay& h : g_log.history) snap.week.push_back({h.date, h.ml});
    snap.week.push_back({d.date, d.takenMl});
    return snap;
}

Due Check() {
    int minute = 0;
    w::Day& d = Day(&minute);
    // Not while you're away from the keyboard: there's nobody to drink.
    if (bb::lfeat::IdleSeconds() > 5 * 60) return Due::None;
    const w::Due due = w::Check(g_settings, d, minute);
    if (due == w::Due::None) return Due::None;
    d.lastRemindMinute = minute;
    Save();
    return due == w::Due::Refill ? Due::Refill : Due::Drink;
}

// The mascot drinks along, and cheers the glass that reaches the goal (DrankAt / GoalAt).
double g_drankAt = -100, g_goalAt = -100;
double DrankAt() { return g_drankAt; }
double GoalAt() { return g_goalAt; }

void Drink() {
    int minute = 0;
    w::Day& d = Day(&minute);
    const int before = d.takenMl;
    w::Drink(g_settings, d, g_settings.glassMl);
    Save();
    g_drankAt = g_get_monotonic_time() / 1e6;  // the island's clock (Now)
    if (g_settings.goalMl > 0 && before < g_settings.goalMl && d.takenMl >= g_settings.goalMl) g_goalAt = g_drankAt + 2.6;
}

void Undo() {
    int minute = 0;
    w::Undo(g_settings, Day(&minute));
    Save();
}

void Refill() {
    int minute = 0;
    w::Refill(g_settings, Day(&minute));
    Save();
}

void Snooze() {
    int minute = 0;
    w::Day& d = Day(&minute);
    w::Snooze(d, minute);
    Save();
}

}  // namespace bb::lwater
