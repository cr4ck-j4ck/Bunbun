// Bunbun — the Timers page's rules, shared by the Windows island
// (src/island/modules/timers_module.inc) and the Linux island
// (src/platform/linux/sources.cpp): the Pomodoro cycle, the countdown dial,
// saved presets and the stopwatch's laps. Pure (no clock reads, no platform
// calls), so it is unit-tested in tests/test_studio.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace bb::timers {

// The Timers page's modes, in tab order. Stored as Bunbun state "TimersMode".
enum class Mode { Focus, Countdown, Stopwatch, Water };
constexpr int kModeCount = 4;

inline const wchar_t* ModeName(Mode m) {
    switch (m) {
        case Mode::Focus: return L"Focus";
        case Mode::Countdown: return L"Timer";
        case Mode::Stopwatch: return L"Stopwatch";
        case Mode::Water: return L"Water";
    }
    return L"";
}

// ── Pomodoro ─────────────────────────────────────────────────────────────────

enum class Phase { Focus, ShortBreak, LongBreak };

struct PomodoroConfig {
    int focusMinutes = 25;
    int shortMinutes = 5;
    int longMinutes = 15;
    int cycles = 4;          // focus sessions before a long break
    bool autoStart = true;   // the next phase starts by itself
};

// Where you are in the cycle. `cycle` counts focus sessions from 1 to cycles.
struct Pomodoro {
    Phase phase = Phase::Focus;
    int cycle = 1;
    int sessionsToday = 0;   // focus sessions finished today
    int day = 0;             // yyyymmdd that sessionsToday belongs to
};

inline PomodoroConfig Sanitize(PomodoroConfig c) {
    c.focusMinutes = std::clamp(c.focusMinutes, 1, 180);
    c.shortMinutes = std::clamp(c.shortMinutes, 1, 60);
    c.longMinutes = std::clamp(c.longMinutes, 1, 90);
    c.cycles = std::clamp(c.cycles, 1, 12);
    return c;
}

inline int PhaseMinutes(Phase p, const PomodoroConfig& c) {
    switch (p) {
        case Phase::Focus: return c.focusMinutes;
        case Phase::ShortBreak: return c.shortMinutes;
        case Phase::LongBreak: return c.longMinutes;
    }
    return c.focusMinutes;
}

inline const wchar_t* PhaseName(Phase p) {
    switch (p) {
        case Phase::Focus: return L"Focus";
        case Phase::ShortBreak: return L"Short break";
        case Phase::LongBreak: return L"Long break";
    }
    return L"";
}

// The phase that follows `p`: after the last focus session of a cycle comes the
// long break, after any other the short one; every break leads back to focus.
inline Phase NextPhase(const Pomodoro& p, const PomodoroConfig& c) {
    if (p.phase != Phase::Focus) return Phase::Focus;
    return p.cycle >= c.cycles ? Phase::LongBreak : Phase::ShortBreak;
}

// Today's count belongs to `today`; a new day starts at zero.
inline void Rollover(Pomodoro* p, int today) {
    if (p->day != today) {
        p->day = today;
        p->sessionsToday = 0;
    }
}

// The current phase finished (or was skipped, `counted` false): move on.
inline void Advance(Pomodoro* p, const PomodoroConfig& c, int today, bool counted) {
    Rollover(p, today);
    const Phase next = NextPhase(*p, c);
    if (p->phase == Phase::Focus && counted) ++p->sessionsToday;
    if (p->phase == Phase::LongBreak) p->cycle = 1;           // a fresh cycle
    else if (p->phase == Phase::ShortBreak) p->cycle = std::min(p->cycle + 1, c.cycles);
    p->phase = next;
}

// Back to the first focus session of a cycle (today's count stays).
inline void Reset(Pomodoro* p) {
    p->phase = Phase::Focus;
    p->cycle = 1;
}

// "Focus 1 of 4", "Short break", "Long break".
inline std::wstring PhaseLabel(const Pomodoro& p, const PomodoroConfig& c) {
    if (p.phase != Phase::Focus) return PhaseName(p.phase);
    return L"Focus " + std::to_wstring(std::min(p.cycle, c.cycles)) + L" of " + std::to_wstring(c.cycles);
}

// What the island says when a phase ends: "Focus done · Take a break".
inline std::wstring DoneMessage(Phase finished, Phase next) {
    if (finished == Phase::Focus) return next == Phase::LongBreak ? L"Cycle done \x00B7 Take a long break" : L"Focus done \x00B7 Take a break";
    return L"Break over \x00B7 Back to focus";
}

// ── Countdown dial ───────────────────────────────────────────────────────────
// One turn of the ring is 60 minutes; dragging snaps to whole minutes. Angle 0
// is 12 o'clock, growing clockwise, in radians.

constexpr int kDialSecondsPerTurn = 60 * 60;
constexpr int kMaxCountdownSeconds = 12 * 60 * 60;

inline int SecondsFromAngle(double angle) {
    constexpr double kTwoPi = 6.283185307179586;
    double a = std::fmod(angle, kTwoPi);
    if (a < 0) a += kTwoPi;
    int minutes = static_cast<int>(std::lround(a / kTwoPi * 60.0));
    if (minutes == 0) minutes = 60;  // a full turn, not nothing
    return minutes * 60;
}

// The fraction of the ring `seconds` fills (wraps every hour).
inline double DialFraction(int seconds) {
    if (seconds <= 0) return 0.0;
    const int inTurn = seconds % kDialSecondsPerTurn;
    return inTurn == 0 ? 1.0 : static_cast<double>(inTurn) / kDialSecondsPerTurn;
}

// The Focus length dial: one turn is two hours, in 5-minute steps, 5 min at least.
constexpr int kFocusDialMinutes = 120;
inline int FocusMinutesFromAngle(double angle) {
    constexpr double kTwoPi = 6.283185307179586;
    double a = std::fmod(angle, kTwoPi);
    if (a < 0) a += kTwoPi;
    int minutes = static_cast<int>(std::lround(a / kTwoPi * kFocusDialMinutes / 5.0)) * 5;
    if (minutes == 0) minutes = kFocusDialMinutes;  // a full turn, not nothing
    return std::max(5, minutes);
}
inline double FocusDialFraction(int minutes) {
    return std::clamp(static_cast<double>(minutes) / kFocusDialMinutes, 0.0, 1.0);
}
// The wheel over the Focus dial: 5 minutes a notch, 5 to 180.
inline int StepFocusMinutes(int minutes, int notches) {
    return std::clamp((minutes + 2) / 5 * 5 + notches * 5, 5, 180);
}

// The wheel over the dial: minutes, or seconds with Shift. Never below 1 s.
inline int StepCountdown(int seconds, int notches, bool fine) {
    const int step = fine ? 5 : 60;
    int v = seconds + notches * step;
    if (!fine && notches != 0) v = (v / 60) * 60;  // whole minutes when stepping minutes
    return std::clamp(v, fine ? 5 : 60, kMaxCountdownSeconds);
}

// ── Presets ──────────────────────────────────────────────────────────────────
// Bunbun.TimerPresets: durations separated by commas, "25" (minutes) or "1:30"
// (minutes:seconds). Kept in order, without duplicates, at most kMaxPresets.

constexpr size_t kMaxPresets = 6;

inline std::vector<int> ParsePresets(const std::wstring& text) {
    std::vector<int> out;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(L',', at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring item = text.substr(at, end - at);
        at = end + 1;
        item.erase(0, item.find_first_not_of(L" \t"));
        item.erase(item.find_last_not_of(L" \t") + 1);
        if (item.empty()) continue;
        int seconds = 0;
        const size_t colon = item.find(L':');
        wchar_t* stop = nullptr;
        if (colon == std::wstring::npos) {
            const double m = std::wcstod(item.c_str(), &stop);
            if (stop == item.c_str()) continue;
            seconds = static_cast<int>(std::lround(m * 60));
        } else {
            const long m = std::wcstol(item.c_str(), &stop, 10);
            const long s = std::wcstol(item.c_str() + colon + 1, nullptr, 10);
            seconds = static_cast<int>(m * 60 + s);
        }
        if (seconds < 5 || seconds > kMaxCountdownSeconds) continue;
        if (std::find(out.begin(), out.end(), seconds) != out.end()) continue;
        out.push_back(seconds);
        if (out.size() == kMaxPresets) break;
    }
    return out;
}

inline std::wstring PresetText(int seconds) {
    wchar_t b[24];
    if (seconds % 60 == 0) std::swprintf(b, 24, L"%d", seconds / 60);
    else std::swprintf(b, 24, L"%d:%02d", seconds / 60, seconds % 60);
    return b;
}

inline std::wstring SerializePresets(const std::vector<int>& presets) {
    std::wstring out;
    for (int s : presets) out += (out.empty() ? L"" : L", ") + PresetText(s);
    return out;
}

// "25 min", "1:30", "1 h 30".
inline std::wstring PresetLabel(int seconds) {
    if (seconds % 60 != 0) return PresetText(seconds);
    const int m = seconds / 60;
    if (m < 60) return std::to_wstring(m) + L" min";
    return std::to_wstring(m / 60) + L" h" + (m % 60 ? L" " + std::to_wstring(m % 60) : L"");
}

// Saves `seconds` first; when the list is full the oldest (last) one goes.
inline std::vector<int> AddPreset(std::vector<int> presets, int seconds) {
    presets.erase(std::remove(presets.begin(), presets.end(), seconds), presets.end());
    presets.insert(presets.begin(), seconds);
    if (presets.size() > kMaxPresets) presets.resize(kMaxPresets);
    return presets;
}

// ── Stopwatch ────────────────────────────────────────────────────────────────

struct Lap {
    double split = 0;  // this lap, seconds
    double total = 0;  // elapsed at the lap
};

// Index of the fastest and slowest lap; -1 until there are two laps to compare.
struct LapMarks {
    int fastest = -1;
    int slowest = -1;
};

inline LapMarks MarkLaps(const std::vector<Lap>& laps) {
    LapMarks m;
    if (laps.size() < 2) return m;
    m.fastest = m.slowest = 0;
    for (int i = 1; i < static_cast<int>(laps.size()); ++i) {
        if (laps[i].split < laps[m.fastest].split) m.fastest = i;
        if (laps[i].split > laps[m.slowest].split) m.slowest = i;
    }
    return m;
}

// "4:07.25", "1:02:03.40" (hundredths, rounded down).
inline std::wstring StopwatchText(double seconds) {
    if (seconds < 0) seconds = 0;
    const long long cs = static_cast<long long>(seconds * 100.0);
    const long long h = cs / 360000, m = cs / 6000 % 60, s = cs / 100 % 60, c = cs % 100;
    wchar_t b[32];
    if (h > 0) std::swprintf(b, 32, L"%lld:%02lld:%02lld.%02lld", h, m, s, c);
    else std::swprintf(b, 32, L"%lld:%02lld.%02lld", m, s, c);
    return b;
}

// Tab-separated, for pasting into a spreadsheet: "Lap\tSplit\tTotal\n1\t…".
inline std::wstring LapsText(const std::vector<Lap>& laps) {
    std::wstring out = L"Lap\tSplit\tTotal\n";
    for (size_t i = 0; i < laps.size(); ++i)
        out += std::to_wstring(i + 1) + L"\t" + StopwatchText(laps[i].split) + L"\t" + StopwatchText(laps[i].total) + L"\n";
    return out;
}

// A new lap at `elapsed` seconds.
inline void AddLap(std::vector<Lap>* laps, double elapsed) {
    const double prev = laps->empty() ? 0.0 : laps->back().total;
    laps->push_back({std::max(0.0, elapsed - prev), elapsed});
}

}  // namespace bb::timers
