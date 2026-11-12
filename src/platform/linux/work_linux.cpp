// Bunbun — Work mode on Linux. See work_linux.h.
#include "work_linux.h"

#include <glib.h>

#include <algorithm>
#include <ctime>

#include "calendar_linux.h"
#include "features_linux.h"
#include "platform.h"
#include "settings.h"
#include "sources.h"
#include "timers_linux.h"

namespace bb::lwork {
namespace {

namespace wk = bb::work;

wk::UseCase g_use = wk::UseCase::Agents;
bool g_reminders = true;
bool g_studyPomodoro = true;
wk::Tracker g_tracker;
wk::Log g_log;
bool g_loaded = false;
double g_lastTick = 0;
double g_lastSave = 0;

std::string FilePath() { return bb::plat::StateDir() + "/work.txt"; }
double Seconds() { return g_get_monotonic_time() / 1e6; }

int Ymd(const tm& t) { return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday; }

wk::Input Input() {
    const time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    const time_t y = now - 86400;
    tm yt{};
    localtime_r(&y, &yt);
    wk::Input in;
    in.epoch = static_cast<long long>(now);
    in.date = Ymd(lt);
    in.prevDate = Ymd(yt);
    in.weekday = (lt.tm_wday + 6) % 7;  // Monday first
    in.second = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    in.idle = bb::lfeat::IdleSeconds();
    in.afkMinutes = bb::lfeat::Get().afkMinutes;
    in.sleepMinutes = bb::lfeat::Get().sleepMinutes;
    return in;
}

void Save() {
    if (g_tracker.day.date) g_log.days[g_tracker.day.date] = g_tracker.day;
    g_log.manual = g_tracker.manual;
    bb::plat::WriteFileAtomic(FilePath(), wk::Serialize(g_log));
    g_lastSave = Seconds();
}

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::string text;
    bb::plat::ReadFile(FilePath(), &text);
    g_log = wk::Parse(text);
    g_tracker.manual = g_log.manual;
    bool within = false;
    int workDate = 0, second = 0;
    g_tracker.Locate(Input(), &within, &workDate, &second);
    if (auto it = g_log.days.find(workDate); it != g_log.days.end()) g_tracker.day = it->second;
    else g_tracker.day.date = workDate;
    g_tracker.inHours = within;  // already within hours at start-up isn't "your day began"
    g_lastTick = Seconds();
}

int WorkDate() {
    bool within = false;
    int workDate = 0, second = 0;
    g_tracker.Locate(Input(), &within, &workDate, &second);
    return workDate;
}

std::string U(const std::wstring& w) { return bb::plat::Utf8(w); }

std::vector<int> g_daysOff;  // Work.DaysOff (holidays and leave you picked), as on Windows

}  // namespace

void LoadSettings() {
    namespace s = bb::settings;
    g_use = wk::ParseUseCase(s::Get(L"Bunbun.UseCase"));
    wk::Schedule& w = g_tracker.schedule;
    const unsigned days = wk::ParseDays(s::Get(L"Work.Days"));
    w.days = days ? days : 0x1F;
    w.start = wk::ParseClock(s::Get(L"Work.Start"), 9 * 60);
    w.end = wk::ParseClock(s::Get(L"Work.End"), 18 * 60);
    w.lunchStart = wk::ParseClock(s::Get(L"Work.LunchStart"), 13 * 60);
    w.lunchMinutes = std::clamp(s::GetInt(L"Work.LunchMinutes"), 0, 240);
    w.breakEvery = std::clamp(s::GetInt(L"Work.BreakEvery"), 0, 480);
    w.breakMinutes = std::clamp(s::GetInt(L"Work.BreakMinutes"), 1, 120);
    w.goalMinutes = std::clamp(s::GetInt(L"Work.GoalHours"), 0, 24) * 60;
    w.autoStart = s::GetBool(L"Work.AutoStart");
    g_daysOff = wk::ParseDatesOff(s::Get(L"Work.DaysOff"));
    g_reminders = s::GetBool(L"Work.Reminders");
    w.blocks.clear();
    if (wk::StudyOn(g_use)) {  // Study: the timetable replaces the office hours and lunch
        w.blocks = wk::ParseTimetable(s::Get(L"Study.Timetable"));
        w.lunchMinutes = 0;
        w.autoStart = true;
        w.goalMinutes = std::clamp(s::GetInt(L"Study.GoalHours"), 0, 24) * 60;
        g_studyPomodoro = s::GetBool(L"Study.Pomodoro");
    }
}

bool On() { return wk::WorkOn(g_use); }
bool AgentsOn() { return wk::AgentsOn(g_use); }

Snapshot Now() {
    Load();
    Snapshot s;
    s.on = On();
    s.phase = g_tracker.phase;
    s.today = wk::Summarize(g_tracker.day);
    s.goal = wk::GoalMinutes(g_tracker.schedule) * 60;
    s.schedule = g_tracker.schedule;
    s.inHours = g_tracker.inHours;
    s.breakUntil = g_tracker.manual.breakUntil;
    s.segments = g_tracker.day.segments;
    s.study = wk::StudyOn(g_use);
    if (s.study) {
        s.subject = g_tracker.subject;
        if (g_tracker.block >= 0) s.blockEnd = g_tracker.schedule.blocks[static_cast<size_t>(g_tracker.block)].end;
        const wk::Input in = Input();
        for (const wk::Block& b : g_tracker.schedule.blocks)
            if (b.weekday == in.weekday && b.start > in.second / 60 && (s.nextBlock < 0 || b.start < s.nextBlock)) {
                s.nextBlock = b.start;
                s.nextSubject = b.subject;
            }
        for (const auto& x : s.today.subjects) s.subjects.push_back(x);
        std::sort(s.subjects.begin(), s.subjects.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        s.streak = wk::Streak(g_log, g_tracker.day, g_tracker.day.date, s.goal);
    }
    return s;
}

std::vector<std::pair<std::string, std::string>> Tick() {
    std::vector<std::pair<std::string, std::string>> notes;
    Load();
    const double now = Seconds();
    int dt = static_cast<int>(now - g_lastTick);
    if (!On()) { g_lastTick = now; return notes; }
    g_lastTick += dt;
    wk::Input in = Input();
    // Study: a running Pomodoro focus session (not a break) counts as study.
    if (wk::StudyOn(g_use) && g_studyPomodoro) {
        const auto& f = bb::lsrc::FocusState();
        in.extraActive = f.active && f.running && f.kind == bb::ltimers::kKindPomodoro &&
                         bb::ltimers::Pomo().phase == bb::timers::Phase::Focus;
    }
    if (dt > 60) in.idle = std::max(in.idle, static_cast<double>(dt));  // the PC slept: away, not work
    dt = std::clamp(dt, 0, 12 * 3600);
    // Days off: the ones you picked, and all-day leave events from your calendar link.
    g_tracker.schedule.daysOff = g_daysOff;
    if (g_tracker.schedule.autoStart) {
        const std::vector<int>& leave = bb::lcal::LeaveDays();
        g_tracker.schedule.daysOff.insert(g_tracker.schedule.daysOff.end(), leave.begin(), leave.end());
    }
    const wk::Day before = g_tracker.day;
    const wk::Phase was = g_tracker.phase;
    const unsigned events = g_tracker.Tick(in, dt);
    if (events & wk::kNewDay) g_log.days[before.date] = before;
    if (events || was != g_tracker.phase || now - g_lastSave > 60) Save();
    if (!g_reminders || !events) return notes;
    const wk::Summary sum = wk::Summarize(g_tracker.day);
    const wk::Schedule& sch = g_tracker.schedule;
    if ((events & wk::kBlockStarted) && g_tracker.block >= 0) {
        const wk::Block& b = sch.blocks[static_cast<size_t>(g_tracker.block)];
        notes.push_back({U(b.subject), "Until " + U(wk::FormatClock(b.end))});
    }
    // Short: a note can share the island with another pill.
    if (events & wk::kLunchStart) notes.push_back({"Lunch time", "Back at " + U(wk::FormatClock(sch.lunchStart + sch.lunchMinutes))});
    if (events & wk::kBreakDue)
        notes.push_back({"Take a short break", std::to_string(sch.breakMinutes) + " min, after " + std::to_string(sch.breakEvery) + " min of work"});
    if (events & wk::kGoalReached)
        notes.push_back({sch.blocks.empty() ? "Daily goal reached" : "Study goal reached", U(wk::Duration(sum.Worked())) + " today"});
    if (events & wk::kDayEnded)
        notes.push_back({"Work day done", U(wk::Duration(sum.Worked())) + " worked, " +
                                              U(wk::Duration(sum.Of(wk::Kind::Away) + sum.Of(wk::Kind::Asleep))) + " away"});
    if (events & wk::kDayStarted) notes.push_back({"Work mode on", "Until " + U(wk::FormatClock(sch.end))});
    return notes;
}

void Start() { Load(); g_tracker.Start(WorkDate()); Save(); }
void Pause() { Load(); g_tracker.Pause(); Save(); }
void Resume() { Load(); g_tracker.Resume(); Save(); }
void TakeBreak() { Load(); g_tracker.TakeBreak(static_cast<long long>(time(nullptr))); Save(); }
void Done() { Load(); g_tracker.Done(WorkDate()); Save(); }

}  // namespace bb::lwork
