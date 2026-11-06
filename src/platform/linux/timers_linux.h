// The Timers page's state on Linux: Pomodoro, the countdown, the stopwatch, as on
// Windows (src/island/modules/timers_module.inc). The rules are shared
// (src/common/timers.h); the running countdown is bb::lsrc::Focus, so the
// island's timer pill shows Pomodoro phases and countdowns alike.
#pragma once

#include <string>
#include <vector>

#include "../../common/timers.h"

namespace bb::ltimers {

namespace tm = bb::timers;

// lsrc::Focus::kind: who started the running countdown.
enum : int { kKindNone = 0, kKindPomodoro = 1, kKindTimer = 2 };

struct Stopwatch {
    bool active = false, running = false;
    double startedAt = 0, accumulated = 0;  // the island's clock (seconds)
};

void LoadSettings();

tm::Mode Mode();          // Water falls back to Focus when Water in Timers is off
void SetMode(tm::Mode m);
bool WaterMode();         // Bunbun.WaterPage: Water is one of the modes

// Pomodoro
tm::PomodoroConfig Config();
const tm::Pomodoro& Pomo();
double AutoStartAt();     // the island's clock when the next phase starts by itself (0: not pending)
void PomodoroToggle();
void PomodoroSkip();
void PomodoroReset();
void PomodoroLength(int minutes);  // Bunbun.PomodoroFocus
void PomodoroAuto();               // flips Bunbun.PomodoroAutoStart

// Countdown
int CountSet();
std::vector<int> Presets();        // Bunbun.TimerPresets, seconds
void CountdownToggle();
void CountdownAdd(int minutes);
void CountdownReset();
void CountdownSet(int seconds);
void CountdownStep(int notches, bool fine);  // the wheel over the ring
void SavePreset();

// Stopwatch
const Stopwatch& StopwatchState();
double StopwatchElapsed(double now);
const std::vector<tm::Lap>& Laps();
void StopwatchToggle();
void StopwatchLap();
void StopwatchReset();
std::string LapsText();

// The running countdown just finished: what the toast says ("Break time" ...).
std::string OnFinished(int kind);
// Each frame: starts the next Pomodoro phase when its moment comes.
void Tick();

}  // namespace bb::ltimers
