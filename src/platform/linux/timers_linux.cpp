// The Timers page's state on Linux (see timers_linux.h).
#include "timers_linux.h"

#include <glib.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <sstream>

#include "platform.h"
#include "settings.h"
#include "sources.h"

namespace bb::ltimers {
namespace {

tm::Mode g_mode = tm::Mode::Focus;
tm::Pomodoro g_pomo;
tm::PomodoroConfig g_cfg;
int g_countSet = 25 * 60;
double g_autoStartAt = 0;
std::vector<tm::Lap> g_laps;
Stopwatch g_sw;
std::wstring g_presets;
bool g_water = true;
bool g_loaded = false;

double Mono() { return g_get_monotonic_time() / 1e6; }  // the same clock as lsrc::Focus

int Today() {
    const time_t t = time(nullptr);
    const struct ::tm* lt = localtime(&t);
    return (lt->tm_year + 1900) * 10000 + (lt->tm_mon + 1) * 100 + lt->tm_mday;
}

std::string FilePath() { return bb::plat::StateDir() + "/timers.txt"; }

// timers.txt: key=value lines, the same values Windows keeps in its state.
void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::string text;
    if (!bb::plat::ReadFile(FilePath(), &text)) return;
    std::map<std::string, int> v;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos) v[line.substr(0, eq)] = std::atoi(line.c_str() + eq + 1);
    }
    g_mode = static_cast<tm::Mode>(std::clamp(v["TimersMode"], 0, tm::kModeCount - 1));
    if (v["CountdownSeconds"] > 0) g_countSet = std::clamp(v["CountdownSeconds"], 5, tm::kMaxCountdownSeconds);
    g_pomo.phase = static_cast<tm::Phase>(std::clamp(v["PomoPhase"], 0, 2));
    g_pomo.cycle = std::max(1, v.count("PomoCycle") ? v["PomoCycle"] : 1);
    g_pomo.sessionsToday = std::max(0, v["PomoSessions"]);
    g_pomo.day = v["PomoDay"];
    tm::Rollover(&g_pomo, Today());
}

void Save() {
    std::ostringstream o;
    o << "TimersMode=" << static_cast<int>(g_mode) << "\nCountdownSeconds=" << g_countSet << "\nPomoPhase="
      << static_cast<int>(g_pomo.phase) << "\nPomoCycle=" << g_pomo.cycle << "\nPomoSessions=" << g_pomo.sessionsToday
      << "\nPomoDay=" << g_pomo.day << "\n";
    bb::plat::MakeDirs(bb::plat::StateDir());
    bb::plat::WriteFileAtomic(FilePath(), o.str());
}

int RunningKind() {
    const bb::lsrc::Focus& f = bb::lsrc::FocusState();
    return f.active ? f.kind : kKindNone;
}

void StartPhase() {
    Load();
    tm::Rollover(&g_pomo, Today());
    g_pomo.cycle = std::min(g_pomo.cycle, g_cfg.cycles);
    bb::lsrc::FocusStartSeconds(tm::PhaseMinutes(g_pomo.phase, g_cfg) * 60, kKindPomodoro);
    g_autoStartAt = 0;
    Save();
}

// Writes a Bunbun setting; the default value is written as unset.
void SetSetting(const wchar_t* name, const std::wstring& value, const std::wstring& deflt) {
    if (value == deflt) bb::settings::Set(name, nullptr);
    else bb::settings::Set(name, &value);
}

}  // namespace

void LoadSettings() {
    namespace s = bb::settings;
    g_cfg = tm::Sanitize({static_cast<int>(s::GetInt(L"Bunbun.PomodoroFocus")), static_cast<int>(s::GetInt(L"Bunbun.PomodoroShortBreak")),
                          static_cast<int>(s::GetInt(L"Bunbun.PomodoroLongBreak")), static_cast<int>(s::GetInt(L"Bunbun.PomodoroCycles")),
                          s::GetBool(L"Bunbun.PomodoroAutoStart")});
    g_presets = s::Get(L"Bunbun.TimerPresets");
    g_water = s::GetBool(L"Bunbun.WaterPage");
    Load();
}

tm::Mode Mode() {
    Load();
    return g_mode == tm::Mode::Water && !g_water ? tm::Mode::Focus : g_mode;
}
void SetMode(tm::Mode m) { Load(); g_mode = m; Save(); }
bool WaterMode() { return g_water; }

tm::PomodoroConfig Config() { return g_cfg; }
const tm::Pomodoro& Pomo() { Load(); return g_pomo; }
double AutoStartAt() { return g_autoStartAt; }

void PomodoroToggle() {
    if (RunningKind() == kKindPomodoro) bb::lsrc::FocusPause();
    else StartPhase();
}

void PomodoroSkip() {
    Load();
    const bool was = RunningKind() == kKindPomodoro;
    if (was) bb::lsrc::FocusStop();
    tm::Advance(&g_pomo, g_cfg, Today(), false);
    g_autoStartAt = 0;
    if (was && g_cfg.autoStart) StartPhase();
    Save();
}

void PomodoroReset() {
    Load();
    if (RunningKind() == kKindPomodoro) bb::lsrc::FocusStop();
    tm::Reset(&g_pomo);
    g_autoStartAt = 0;
    Save();
}

void PomodoroLength(int minutes) { SetSetting(L"Bunbun.PomodoroFocus", std::to_wstring(minutes), L"25"); }
void PomodoroAuto() { SetSetting(L"Bunbun.PomodoroAutoStart", g_cfg.autoStart ? L"false" : L"true", L"true"); }

int CountSet() { Load(); return g_countSet; }
std::vector<int> Presets() { return tm::ParsePresets(g_presets); }

void CountdownToggle() {
    Load();
    if (RunningKind() == kKindTimer) { bb::lsrc::FocusPause(); return; }
    bb::lsrc::FocusStartSeconds(g_countSet, kKindTimer);
    Save();
}

void CountdownAdd(int minutes) {
    Load();
    if (RunningKind() == kKindTimer) { bb::lsrc::FocusAdd(minutes); return; }
    g_countSet = std::clamp(g_countSet + minutes * 60, 60, tm::kMaxCountdownSeconds);
    Save();
}

void CountdownReset() { if (RunningKind() == kKindTimer) bb::lsrc::FocusStop(); }
void CountdownSet(int seconds) { Load(); g_countSet = std::clamp(seconds, 5, tm::kMaxCountdownSeconds); Save(); }
void CountdownStep(int notches, bool fine) { Load(); CountdownSet(tm::StepCountdown(g_countSet, notches, fine)); }

void SavePreset() {
    Load();
    bb::settings::Set(L"Bunbun.TimerPresets", nullptr);
    const std::wstring text = tm::SerializePresets(tm::AddPreset(tm::ParsePresets(g_presets), g_countSet));
    bb::settings::Set(L"Bunbun.TimerPresets", &text);
}

const Stopwatch& StopwatchState() { return g_sw; }
double StopwatchElapsed(double now) { return g_sw.accumulated + (g_sw.running ? std::max(0.0, now - g_sw.startedAt) : 0.0); }
const std::vector<tm::Lap>& Laps() { return g_laps; }

void StopwatchToggle() {
    const double now = Mono();
    if (g_sw.running) {
        g_sw.accumulated += std::max(0.0, now - g_sw.startedAt);
        g_sw.running = false;
    } else {
        g_sw.startedAt = now;
        g_sw.running = g_sw.active = true;
    }
}
void StopwatchLap() { if (g_sw.running) tm::AddLap(&g_laps, StopwatchElapsed(Mono())); }
void StopwatchReset() {
    if (g_sw.running) return;  // pause first
    g_sw = Stopwatch{};
    g_laps.clear();
}
std::string LapsText() { return bb::plat::Utf8(tm::LapsText(g_laps)); }

std::string OnFinished(int kind) {
    Load();
    if (kind == kKindPomodoro) {
        const tm::Phase finished = g_pomo.phase;
        tm::Advance(&g_pomo, g_cfg, Today(), true);
        if (g_cfg.autoStart) g_autoStartAt = Mono() + 5.0;
        Save();
        return bb::plat::Utf8(tm::DoneMessage(finished, g_pomo.phase));
    }
    if (kind == kKindTimer) return "Timer done";
    return "Time's up \xE2\x80\x94 take a break";
}

void Tick() {
    if (g_autoStartAt <= 0 || Mono() < g_autoStartAt) return;
    g_autoStartAt = 0;
    if (RunningKind() == kKindNone) StartPhase();
}

}  // namespace bb::ltimers
