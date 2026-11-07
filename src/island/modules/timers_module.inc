// Bunbun: the Timers page (one page, four modes around a big ring).
//
//   Focus      Pomodoro: focus, short break and long break, "Focus 1 of 4",
//              skip, reset, auto-start of the next phase, sessions today.
//   Timer      a countdown set by dragging the ring or the wheel over it
//              (Shift: seconds), +1 / +5 min, your own presets.
//   Stopwatch  laps with their split, the fastest and slowest marked, copy.
//   Water      today's glass (water_module.inc), when Bunbun.WaterPage is on.
//
// The rules are in src/common/timers.h, shared with Linux. Pomodoro phases and
// the countdown both run on the island's one timer (g_state.timer, under
// g_stateMutex), so the timer pill on the small island shows them like any other
// timer; TimerSnapshot::kind says which one it is. The stopwatch has its own
// g_state.stopwatch and shows in the same pill while it runs. Everything else
// here (the mode, the cycle, the laps) is render-thread state: the page, its
// clicks and the render loop's completion hook all run on the render thread.
//
// Included in the core's anonymous namespace after focus_module.inc.

namespace Timers {

namespace tm = bb::timers;

tm::Mode g_mode = tm::Mode::Focus;
tm::Pomodoro g_pomo;
int g_countSet = 25 * 60;          // the countdown on the ring when none is running, seconds
double g_autoStartAt = 0.0;        // NowSeconds() when the next Pomodoro phase starts by itself
std::vector<tm::Lap> g_laps;       // stopwatch laps, oldest first
bool g_loaded = false;
bool g_ringDrag = false;           // left button went down on the countdown ring
bool g_ringFocus = false;          // Bunbun: ... on the Focus length dial instead
int g_focusDrag = -1;              // Bunbun: the Focus length being dragged (minutes; -1 none)
D2D1_POINT_2F g_ringCenter{};      // that ring's centre, in content space

int Today() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
}

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    g_mode = static_cast<tm::Mode>(std::clamp(Wh_GetIntValue(L"TimersMode", 0), 0, tm::kModeCount - 1));
    g_countSet = std::clamp(Wh_GetIntValue(L"CountdownSeconds", 0), 0, tm::kMaxCountdownSeconds);
    if (g_countSet <= 0) g_countSet = FocusPickerMinutes() * 60;  // the old Focus page's picker
    g_pomo.phase = static_cast<tm::Phase>(std::clamp(Wh_GetIntValue(L"PomoPhase", 0), 0, 2));
    g_pomo.cycle = std::max(1, Wh_GetIntValue(L"PomoCycle", 1));
    g_pomo.sessionsToday = std::max(0, Wh_GetIntValue(L"PomoSessions", 0));
    g_pomo.day = Wh_GetIntValue(L"PomoDay", 0);
    tm::Rollover(&g_pomo, Today());
}

void Save() {
    Wh_SetIntValue(L"TimersMode", static_cast<int>(g_mode));
    Wh_SetIntValue(L"CountdownSeconds", g_countSet);
    Wh_SetIntValue(L"PomoPhase", static_cast<int>(g_pomo.phase));
    Wh_SetIntValue(L"PomoCycle", g_pomo.cycle);
    Wh_SetIntValue(L"PomoSessions", g_pomo.sessionsToday);
    Wh_SetIntValue(L"PomoDay", g_pomo.day);
}

tm::PomodoroConfig Config(const Settings& s) {
    return tm::Sanitize({s.pomoFocus, s.pomoShort, s.pomoLong, s.pomoCycles, s.pomoAutoStart});
}

// The running (or just finished) timer's kind, or -1 when there is none.
int ActiveKind(bool* running = nullptr) {
    std::lock_guard lock(g_stateMutex);
    if (running) *running = g_state.timer.running;
    return g_state.timer.active ? g_state.timer.kind : -1;
}

void Label(int kind, const std::wstring& label) {
    std::lock_guard lock(g_stateMutex);
    g_state.timer.kind = kind;
    g_state.timer.label = label;
    g_state.timer.doneLabel.clear();
}

void SetMode(tm::Mode m) {
    Load();
    g_mode = m;
    Save();
    g_layoutDirty = true;
}

// ── Pomodoro ──

void StartPhase(const Settings& s) {
    Load();
    const tm::PomodoroConfig c = Config(s);
    tm::Rollover(&g_pomo, Today());
    g_pomo.cycle = std::min(g_pomo.cycle, c.cycles);
    StartTimerSeconds(tm::PhaseMinutes(g_pomo.phase, c) * 60, g_pomo.phase != tm::Phase::Focus);
    Label(kTimerKindPomodoro, tm::PhaseLabel(g_pomo, c));
    g_autoStartAt = 0.0;
    Save();
}

// Start, or pause / resume the running phase. Starting replaces any other timer.
void PomodoroToggle(const Settings& s) {
    if (ActiveKind() == kTimerKindPomodoro) ToggleTimerPause();
    else StartPhase(s);
}

void PomodoroSkip(const Settings& s) {
    Load();
    const bool wasRunning = ActiveKind() == kTimerKindPomodoro;
    if (wasRunning) StopFocusTimer();
    tm::Advance(&g_pomo, Config(s), Today(), false);
    g_autoStartAt = 0.0;
    if (wasRunning && s.pomoAutoStart) StartPhase(s);
    Save();
    g_layoutDirty = true;
}

void PomodoroReset() {
    Load();
    if (ActiveKind() == kTimerKindPomodoro) StopFocusTimer();
    tm::Reset(&g_pomo);
    g_autoStartAt = 0.0;
    Save();
    g_layoutDirty = true;
}

// ── Countdown ──

void CountdownToggle() {
    Load();
    if (ActiveKind() == kTimerKindTimer) {
        ToggleTimerPause();
        return;
    }
    StartTimerSeconds(g_countSet, false);
    Label(kTimerKindTimer, L"Timer");
    Save();
}

void CountdownAdd(int minutes) {
    Load();
    if (ActiveKind() == kTimerKindTimer) {
        AddFocusMinutes(minutes);
        return;
    }
    g_countSet = std::clamp(g_countSet + minutes * 60, 60, tm::kMaxCountdownSeconds);
    Save();
    g_layoutDirty = true;
}

void CountdownReset() {
    if (ActiveKind() == kTimerKindTimer) StopFocusTimer();
    g_layoutDirty = true;
}

void CountdownSet(int seconds) {
    Load();
    g_countSet = std::clamp(seconds, 5, tm::kMaxCountdownSeconds);
    Save();
    g_layoutDirty = true;
}

void SavePreset(const Settings& s) {
    Load();
    const std::vector<int> presets = tm::AddPreset(tm::ParsePresets(s.timerPresets), g_countSet);
    WriteBunbunSetting(L"TimerPresets", tm::SerializePresets(presets).c_str());
}

// The countdown ring: point straight up is 0, clockwise, one turn an hour.
// Bunbun: the Focus length (Bunbun.PomodoroFocus); 25 is the default, so it's written as unset.
void SaveFocusMinutes(int minutes) {
    WriteBunbunSetting(L"PomodoroFocus", minutes == 25 ? L"-" : std::to_wstring(minutes).c_str());
}

void SetCountdownFromPoint(const MediaContentPoint& pt) {
    if (!pt.valid) return;
    const double angle = std::atan2(pt.x - g_ringCenter.x, g_ringCenter.y - pt.y);
    if (g_ringFocus) {  // Bunbun: the Focus dial shows the length while dragging; saved on release
        const int m = tm::FocusMinutesFromAngle(angle);
        if (m != g_focusDrag) { g_focusDrag = m; g_layoutDirty = true; }
        return;
    }
    const int seconds = tm::SecondsFromAngle(angle);
    if (seconds != g_countSet) CountdownSet(seconds);
}

bool CountdownRunning() {
    return ActiveKind() == kTimerKindTimer;
}

// ── Stopwatch ──

double StopwatchElapsed(const StopwatchSnapshot& sw, double now) {
    return sw.accumulated + (sw.running ? std::max(0.0, now - sw.startedAt) : 0.0);
}

void StopwatchToggle() {
    const double now = NowSeconds();
    {
        std::lock_guard lock(g_stateMutex);
        StopwatchSnapshot& sw = g_state.stopwatch;
        if (sw.running) {
            sw.accumulated += std::max(0.0, now - sw.startedAt);
            sw.running = false;
        } else {
            sw.startedAt = now;
            sw.running = true;
            sw.active = true;
        }
    }
    g_layoutDirty = true;
}

void StopwatchLap() {
    StopwatchSnapshot sw;
    {
        std::lock_guard lock(g_stateMutex);
        sw = g_state.stopwatch;
    }
    if (!sw.running) return;
    tm::AddLap(&g_laps, StopwatchElapsed(sw, NowSeconds()));
    g_layoutDirty = true;
}

void StopwatchReset() {
    {
        std::lock_guard lock(g_stateMutex);
        if (g_state.stopwatch.running) return;  // pause first
        g_state.stopwatch = StopwatchSnapshot{};
    }
    g_laps.clear();
    g_layoutDirty = true;
}

// ── The render loop ──

// The island's timer just ran out. A Pomodoro phase moves the cycle on and says
// what's next; with auto-start the next phase begins a few seconds later.
void OnTimerFinished(int kind, const std::wstring& label, const Settings& s, double now) {
    Load();
    std::wstring done;
    if (kind == kTimerKindPomodoro) {
        const tm::PomodoroConfig c = Config(s);
        const tm::Phase finished = g_pomo.phase;
        tm::Advance(&g_pomo, c, Today(), true);
        done = tm::DoneMessage(finished, g_pomo.phase);
        if (c.autoStart) g_autoStartAt = now + 5.0;
        Save();
    } else if (!label.empty()) {
        done = label + L" done";
    }
    std::lock_guard lock(g_stateMutex);
    g_state.timer.doneLabel = done;
}

// Each frame: start the next Pomodoro phase when its moment comes (unless you
// started another timer meanwhile).
void Tick(const Settings& s, double now) {
    if (g_autoStartAt <= 0.0 || now < g_autoStartAt) return;
    g_autoStartAt = 0.0;
    if (ActiveKind() < 0) StartPhase(s);
}

}  // namespace Timers
