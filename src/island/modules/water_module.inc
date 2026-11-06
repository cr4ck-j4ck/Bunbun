// Bunbun: the water reminder and the Water page.
//
// The rules (pacing the goal across the active hours, the bottle, the week) are
// in src/common/water.h, shared with Linux. Today's count and the last week live
// in water.txt next to settings.ini. A thread checks every 30 s and, when it's
// time, shows the reminder pill (todos_module.inc's, with a water drop and its own
// buttons) under kWaterDrinkId or kWaterRefillId.
//
// Included in the core's anonymous namespace after todos_module.inc (it uses the
// reminder pill and RemindersShouldBeQuiet). The Water page reads g_state.water.

HANDLE g_waterThread = nullptr;
HANDLE g_waterWake = nullptr;  // a drink, refill or settings change: check again now

namespace Water {

std::mutex g_mutex;
bb::water::Log g_log;  // under g_mutex

std::wstring FilePath() {
    std::wstring dir = bb::SettingsPath();
    dir.erase(dir.find_last_of(L"\\/"));
    return dir + L"\\water.txt";
}

// Today as yyyymmdd, and the minute of the day.
int Today(int* minute) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (minute) *minute = st.wHour * 60 + st.wMinute;
    return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
}

void Load() {
    std::string text;
    if (HANDLE f = CreateFileW(FilePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        f != INVALID_HANDLE_VALUE) {
        char buf[4096];
        DWORD got = 0;
        while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got > 0) text.append(buf, got);
        CloseHandle(f);
    }
    std::lock_guard lock(g_mutex);
    g_log = bb::water::Parse(text);
}

// Call with g_mutex held.
void SaveLocked() {
    const std::string text = bb::water::Serialize(g_log);
    const std::wstring path = FilePath(), tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    const bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) && wrote == text.size();
    CloseHandle(f);
    if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Copies today, the next reminder and the week into g_state.water for the page.
void Publish() {
    const bb::water::Settings s = GetSettingsCopy().water;
    int minute = 0;
    const int date = Today(&minute);
    WaterSnapshot snap;
    {
        std::lock_guard lock(g_mutex);
        bb::water::RollOver(g_log, date);
        const bb::water::Day& d = g_log.today;
        snap.goalMl = s.goalMl;
        snap.glassMl = s.glassMl;
        snap.bottleMl = s.bottleMl;
        snap.takenMl = d.takenMl;
        snap.glasses = d.glasses;
        snap.bottleLeftMl = bb::water::BottleLeft(s, d);
        snap.reminders = s.reminders;
        snap.nextMinute = bb::water::NextReminder(s, d, minute);
        for (const bb::water::HistoryDay& h : g_log.history) snap.week.push_back({h.date, h.ml});
        snap.week.push_back({date, d.takenMl});
    }
    bool changed = false;
    {
        std::lock_guard lock(g_stateMutex);
        const WaterSnapshot& old = g_state.water;
        changed = old.takenMl != snap.takenMl || old.glasses != snap.glasses || old.bottleLeftMl != snap.bottleLeftMl ||
                  old.nextMinute != snap.nextMinute || old.reminders != snap.reminders || old.goalMl != snap.goalMl ||
                  old.glassMl != snap.glassMl || old.bottleMl != snap.bottleMl || old.week != snap.week;
        g_state.water = std::move(snap);
    }
    // Only when something shows differently: the thread publishes every 30 s, and an
    // idle island shouldn't wake for nothing.
    if (!changed) return;
    g_layoutDirty = true;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
}

// Puts away a water reminder pill that's showing.
void ClearPill() {
    std::lock_guard lock(g_stateMutex);
    if (IsWaterReminderId(g_state.reminder.id)) g_state.reminder.active = false;
}

template <typename F>
void Change(F&& f) {
    const bb::water::Settings s = GetSettingsCopy().water;
    int minute = 0;
    const int date = Today(&minute);
    {
        std::lock_guard lock(g_mutex);
        bb::water::RollOver(g_log, date);
        f(s, g_log.today, minute);
        SaveLocked();
        Wh_Log(L"Water: %d ml today in %d glasses", g_log.today.takenMl, g_log.today.glasses);
    }
    Publish();
    if (g_waterWake) SetEvent(g_waterWake);
}

}  // namespace Water

// Bunbun: the mascot drinks along when a glass is logged, and celebrates the glass
// that reaches the day's goal (DrawBunSlot reads these, see WaterMood).
std::atomic<double> g_waterDrankAt{-100.0};
std::atomic<double> g_waterGoalAt{-100.0};
constexpr double kDrinkSeconds = 2.6, kGoalCheerSeconds = 3.0;

void WaterDrink() {
    bool reached = false;
    Water::Change([&reached](const bb::water::Settings& s, bb::water::Day& d, int) {
        const int before = d.takenMl;
        bb::water::Drink(s, d, s.glassMl);
        reached = s.goalMl > 0 && before < s.goalMl && d.takenMl >= s.goalMl;
    });
    const double now = NowSeconds();
    g_waterDrankAt.store(now, std::memory_order_relaxed);
    if (reached) g_waterGoalAt.store(now + kDrinkSeconds, std::memory_order_relaxed);  // after the last sip
    g_layoutDirty = true;
    Water::ClearPill();
}

// The mascot's water moment, if one is playing: drinking, then the goal's cheer.
bool WaterMood(double now, bb::mascot::Mood* mood, double* since) {
    const double drank = g_waterDrankAt.load(std::memory_order_relaxed);
    const double goal = g_waterGoalAt.load(std::memory_order_relaxed);
    if (now - drank >= 0.0 && now - drank < kDrinkSeconds) {
        *mood = bb::mascot::Mood::Drinking;
        *since = drank;
        return true;
    }
    if (now - goal >= 0.0 && now - goal < kGoalCheerSeconds) {
        *mood = bb::mascot::Mood::Happy;
        *since = goal;
        return true;
    }
    return false;
}
void WaterUndo() {
    Water::Change([](const bb::water::Settings& s, bb::water::Day& d, int) { bb::water::Undo(s, d); });
}
void WaterRefill() {
    Water::Change([](const bb::water::Settings& s, bb::water::Day& d, int) { bb::water::Refill(s, d); });
    Water::ClearPill();
}
void WaterSnooze() {
    Water::Change([](const bb::water::Settings&, bb::water::Day& d, int minute) { bb::water::Snooze(d, minute); });
    Water::ClearPill();
}

// The reminder pill's buttons (ReminderPillClick): Drank / Refilled, or Later.
void WaterReminderDone(int id) {
    if (id == kWaterRefillId) WaterRefill();
    else WaterDrink();
}
void WaterReminderLater() { WaterSnooze(); }

DWORD WINAPI WaterThreadProc(void*) {
    Water::Load();
    Water::Publish();
    for (;;) {
        const bb::water::Settings s = GetSettingsCopy().water;
        int minute = 0;
        const int date = Water::Today(&minute);
        bb::water::Due due = bb::water::Due::None;
        // Not while you're away from the keyboard: there's nobody to drink.
        LASTINPUTINFO lii{sizeof(lii)};
        const bool away = GetLastInputInfo(&lii) && GetTickCount() - lii.dwTime > 5u * 60u * 1000u;
        {
            std::lock_guard lock(Water::g_mutex);
            const int before = Water::g_log.today.date;
            bb::water::RollOver(Water::g_log, date);
            if (!away) due = bb::water::Check(s, Water::g_log.today, minute);
            if (due != bb::water::Due::None) Water::g_log.today.lastRemindMinute = minute;
            if (due != bb::water::Due::None || before != date) Water::SaveLocked();
        }
        if (due != bb::water::Due::None) {
            const bool quiet = RemindersShouldBeQuiet();
            // The day so far is in the pill's label (todos_draw.inc).
            const wchar_t* text = due == bb::water::Due::Refill ? L"Time to refill your bottle" : L"Time to drink water";
            {
                std::lock_guard lock(g_stateMutex);
                g_state.reminder.active = true;
                g_state.reminder.id = due == bb::water::Due::Refill ? kWaterRefillId : kWaterDrinkId;
                g_state.reminder.text = text;
                g_state.reminder.quiet = quiet;
                g_state.reminder.expiresAt = NowSeconds() + (quiet ? 20.0 : 60.0);
            }
            if (!quiet) {
                TriggerNudge();
                if (GetSettingsCopy().reminderSound) PlaySoundW(L"Notification.Default", nullptr, SND_ALIAS | SND_ASYNC);
            }
        }
        Water::Publish();
        const HANDLE waits[2] = {g_stopEvent, g_waterWake};
        const DWORD r = WaitForMultipleObjects(g_waterWake ? 2 : 1, waits, FALSE, 30000);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
    }
    return 0;
}
