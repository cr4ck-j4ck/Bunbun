// Bunbun: Work mode (Bunbun.UseCase = work or both).
//
// The rules (the schedule, lunch, breaks, away and asleep, the day's timeline)
// are in src/common/work.h, shared with Linux. The days live in work.txt next to
// settings.ini. A thread ticks every 5 s: it reads the clock and how long the
// keyboard and mouse have been idle, advances the tracker, saves now and then,
// shows the lunch / break / goal / day-over notes and copies the day into
// g_state.work for the Home card, the Work page and the mascot.
//
// Included in the core's anonymous namespace after water_module.inc.

HANDLE g_workThread = nullptr;

namespace Work {

std::mutex g_mutex;
bb::work::Tracker g_tracker;  // under g_mutex
bb::work::Log g_log;          // under g_mutex: the earlier days
bool g_loaded = false;

std::wstring FilePath() {
    std::wstring dir = bb::SettingsPath();
    dir.erase(dir.find_last_of(L"\\/"));
    return dir + L"\\work.txt";
}

// Bunbun: all-day calendar events titled leave / vacation / PTO / OOO / out of office / day off
// are days off, so work mode doesn't start by itself that day. (Public-holiday feeds carry festival
// names, which don't match, so a festival doesn't cancel the day unless the user lists it in Work.DaysOff.)
std::vector<int> CalendarLeaveDates(const std::vector<CalendarItem>& items) {
    std::vector<int> out;
    for (const CalendarItem& it : items) {
        if (it.endsAt - it.startsAt < 20 * 3600.0) continue;  // not an all-day event
        const std::wstring t = ToLowerCopy(it.title);
        bool leave = false;
        for (const wchar_t* w : {L"leave", L"vacation", L"pto", L"ooo", L"out of office", L"day off", L"holiday"}) {
            if (t.find(w) != std::wstring::npos) { leave = true; break; }
        }
        if (!leave) continue;
        for (double e = it.startsAt + 3600; e < it.endsAt && out.size() < 400; e += 86400.0) {
            ULARGE_INTEGER u;
            u.QuadPart = static_cast<ULONGLONG>((e + 11644473600.0) * 1e7);
            FILETIME utc{u.LowPart, u.HighPart}, local;
            SYSTEMTIME st;
            FileTimeToLocalFileTime(&utc, &local);
            FileTimeToSystemTime(&local, &st);
            out.push_back(st.wYear * 10000 + st.wMonth * 100 + st.wDay);
        }
    }
    return out;
}

// Now, as the tracker sees it.
bb::work::Input Now(const Settings& s) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    bb::work::Input in;
    in.date = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
    in.weekday = (st.wDayOfWeek + 6) % 7;  // Monday first
    in.second = st.wHour * 3600 + st.wMinute * 60 + st.wSecond;
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart -= 864000000000ULL;  // a day of 100 ns ticks
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME y;
    FileTimeToSystemTime(&ft, &y);
    in.prevDate = y.wYear * 10000 + y.wMonth * 100 + y.wDay;
    in.epoch = static_cast<long long>(time(nullptr));
    LASTINPUTINFO lii{sizeof(lii)};
    in.idle = GetLastInputInfo(&lii) ? (GetTickCount() - lii.dwTime) / 1000.0 : 0.0;
    in.afkMinutes = s.afkMinutes;
    in.sleepMinutes = s.sleepMinutes;
    return in;
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
    const Settings s = GetSettingsCopy();
    std::lock_guard lock(g_mutex);
    g_log = bb::work::Parse(text);
    g_tracker.schedule = s.workSchedule;
    g_tracker.manual = g_log.manual;
    // Carry on with today's timeline after a restart.
    bool within = false;
    int workDate = 0, second = 0;
    g_tracker.Locate(Now(s), &within, &workDate, &second);
    if (auto it = g_log.days.find(workDate); it != g_log.days.end()) g_tracker.day = it->second;
    else g_tracker.day.date = workDate;
    // Already within hours at start-up: not "your day began" (it fired on every restart).
    g_tracker.inHours = within;
    g_loaded = true;
}

// Call with g_mutex held.
void SaveLocked() {
    if (g_tracker.day.date) g_log.days[g_tracker.day.date] = g_tracker.day;
    g_log.manual = g_tracker.manual;
    const std::string text = bb::work::Serialize(g_log);
    const std::wstring path = FilePath(), tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    const bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) && wrote == text.size();
    CloseHandle(f);
    if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Copies the day into g_state.work. Wakes the island when what it shows changed.
void Publish(bool on) {
    WorkSnapshot snap;
    snap.on = on;
    {
        std::lock_guard lock(g_mutex);
        const bb::work::Tracker& t = g_tracker;
        const bb::work::Summary sum = bb::work::Summarize(t.day);
        for (int k = 0; k < static_cast<int>(bb::work::Kind::Count); ++k) snap.seconds[k] = sum.seconds[k];
        snap.phase = static_cast<int>(t.phase);
        snap.goal = bb::work::GoalMinutes(t.schedule) * 60;
        snap.breaks = sum.breaks;
        snap.first = sum.first;
        snap.last = sum.last;
        snap.start = t.schedule.start;
        snap.end = t.schedule.end;
        snap.lunchStart = t.schedule.lunchStart;
        snap.lunchMinutes = t.schedule.lunchMinutes;
        snap.autoStart = t.schedule.autoStart;
        snap.inHours = t.inHours;
        snap.workDate = t.day.date;
        snap.breakUntil = t.manual.breakUntil;
        snap.segments = t.day.segments;
        snap.study = !t.schedule.blocks.empty() || bb::work::StudyOn(static_cast<bb::work::UseCase>(GetSettingsCopy().useCase));
        if (snap.study) {
            snap.subject = t.subject;
            if (t.block >= 0) snap.blockEnd = t.schedule.blocks[static_cast<size_t>(t.block)].end;
            SYSTEMTIME st;
            GetLocalTime(&st);
            const int weekday = (st.wDayOfWeek + 6) % 7, minute = st.wHour * 60 + st.wMinute;
            for (const bb::work::Block& b : t.schedule.blocks)
                if (b.weekday == weekday && b.start > minute && (snap.nextBlock < 0 || b.start < snap.nextBlock)) {
                    snap.nextBlock = b.start;
                    snap.nextSubject = b.subject;
                }
            for (const auto& s : sum.subjects) snap.subjects.push_back(s);
            std::sort(snap.subjects.begin(), snap.subjects.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            snap.streak = bb::work::Streak(g_log, t.day, t.day.date, snap.goal);
        }
    }
    bool changed = false;
    {
        std::lock_guard lock(g_stateMutex);
        const WorkSnapshot& old = g_state.work;
        changed = old.on != snap.on || old.phase != snap.phase || old.breaks != snap.breaks ||
                  old.seconds[0] / 60 != snap.seconds[0] / 60 || old.segments.size() != snap.segments.size();
        g_state.work = std::move(snap);
    }
    if (!changed) return;
    g_layoutDirty = true;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
}

// A note on the island (the notification pill, as "Work").
void Note(const std::wstring& title, const std::wstring& body) {
    {
        std::lock_guard lock(g_stateMutex);
        g_state.notification.active = true;
        g_state.notification.app = L"Work";
        g_state.notification.title = title;
        g_state.notification.body = body;
        g_state.notification.icon = {};
        g_state.notification.shownAt = NowSeconds();
        g_state.notification.expiresAt = NowSeconds() + 6.0;
    }
    g_layoutDirty = true;
    TriggerNudge();
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
}

void Announce(unsigned events, const bb::work::Summary& sum, const bb::work::Schedule& sch, int block) {
    namespace wk = bb::work;
    if ((events & wk::kBlockStarted) && block >= 0 && block < static_cast<int>(sch.blocks.size())) {
        const wk::Block& b = sch.blocks[static_cast<size_t>(block)];
        Note(b.subject, L"Until " + wk::FormatClock(b.end));
    }
    // Short: a note can share the island with another pill (a camera in use, say).
    if (events & wk::kLunchStart) Note(L"Lunch time", L"Back at " + wk::FormatClock(sch.lunchStart + sch.lunchMinutes));
    if (events & wk::kBreakDue) Note(L"Take a short break", std::to_wstring(sch.breakMinutes) + L" min, after " + std::to_wstring(sch.breakEvery) + L" min of work");
    if (events & wk::kGoalReached) Note(sch.blocks.empty() ? L"Daily goal reached" : L"Study goal reached", wk::Duration(sum.Worked()) + L" today");
    if (events & wk::kDayEnded)
        Note(L"Work day done", wk::Duration(sum.Worked()) + L" worked, " + wk::Duration(sum.Of(wk::Kind::Away) + sum.Of(wk::Kind::Asleep)) + L" away");
    if (events & wk::kDayStarted) Note(L"Work mode on", L"Until " + wk::FormatClock(sch.end));
}

}  // namespace Work

// Bunbun: away and asleep (Bunbun.AfkMinutes / SleepMinutes) for every mascot, in
// every state: over music, agents at work and Work mode. Only an agent waiting for
// your answer still shows. True when it applies, with the mood in `mood`.
bool AwayMood(const Settings& s, const SharedState& state, bb::mascot::Mood* mood) {
    if (state.agent.phase == bb::AgentPhase::Approval || state.agent.phase == bb::AgentPhase::Question) return false;
    LASTINPUTINFO lii{sizeof(lii)};
    const double idle = GetLastInputInfo(&lii) ? (GetTickCount() - lii.dwTime) / 1000.0 : 0.0;
    if (s.sleepMinutes > 0 && idle >= s.sleepMinutes * 60.0) { *mood = bb::mascot::Mood::Sleepy; return true; }
    if (s.afkMinutes > 0 && idle >= s.afkMinutes * 60.0) { *mood = bb::mascot::Mood::Snack; return true; }
    return false;
}

// Bunbun: Work mode's mood: at the laptop while working, a snack at lunch.
bool WorkMood(const WorkSnapshot& w, bb::mascot::Mood* mood) {
    if (!w.on) return false;
    switch (static_cast<bb::work::Phase>(w.phase)) {
        case bb::work::Phase::Working:
        case bb::work::Phase::Overtime: *mood = bb::mascot::Mood::Working; return true;
        case bb::work::Phase::Lunch: *mood = bb::mascot::Mood::Snack; return true;
        default: return false;
    }
}

// The Work page's and the Home card's buttons.
enum class WorkButton { Start, Pause, Resume, Break, Done };
void WorkCommand(WorkButton b) {
    if (!Work::g_loaded) return;
    const Settings s = GetSettingsCopy();
    {
        std::lock_guard lock(Work::g_mutex);
        bb::work::Tracker& t = Work::g_tracker;
        bool within = false;
        int workDate = 0, second = 0;
        t.Locate(Work::Now(s), &within, &workDate, &second);
        switch (b) {
            case WorkButton::Start: t.Start(workDate); break;
            case WorkButton::Pause: t.Pause(); break;
            case WorkButton::Resume: t.Resume(); break;
            case WorkButton::Break: t.TakeBreak(static_cast<long long>(time(nullptr))); break;
            case WorkButton::Done: t.Done(workDate); break;
        }
        Work::SaveLocked();
    }
    if (g_workWake) SetEvent(g_workWake);
}

DWORD WINAPI WorkThreadProc(void*) {
    Work::Load();
    ULONGLONG last = GetTickCount64();
    ULONGLONG lastSave = last;
    for (;;) {
        const Settings s = GetSettingsCopy();
        const bool on = bb::work::WorkOn(static_cast<bb::work::UseCase>(s.useCase));
        if (on) {
            const ULONGLONG nowTick = GetTickCount64();
            int dt = static_cast<int>((nowTick - last) / 1000);
            last += static_cast<ULONGLONG>(dt) * 1000;
            bb::work::Input in = Work::Now(s);
            // Study: a running Pomodoro focus session counts as study.
            if (s.studyPomodoro && bb::work::StudyOn(static_cast<bb::work::UseCase>(s.useCase))) {
                std::lock_guard lock(g_stateMutex);
                const TimerSnapshot& tm = g_state.timer;
                in.extraActive = tm.active && tm.running && !tm.isBreak && tm.kind == kTimerKindPomodoro;
            }
            // A long gap: the PC slept or was off. It counts as time away, not work.
            if (dt > 60) in.idle = std::max(in.idle, static_cast<double>(dt));
            dt = std::clamp(dt, 0, 12 * 3600);
            unsigned events = 0;
            int block = -1;
            bb::work::Summary sum;
            bb::work::Schedule sch;
            bool save = false;
            std::vector<int> leave;
            if (s.workSchedule.autoStart) {
                std::shared_ptr<const std::vector<CalendarItem>> cal;
                {
                    std::lock_guard lock(g_stateMutex);
                    cal = g_state.calendar;
                }
                if (cal) leave = Work::CalendarLeaveDates(*cal);
            }
            {
                std::lock_guard lock(Work::g_mutex);
                Work::g_tracker.schedule = s.workSchedule;
                Work::g_tracker.schedule.daysOff.insert(Work::g_tracker.schedule.daysOff.end(), leave.begin(), leave.end());
                const bb::work::Day before = Work::g_tracker.day;
                const bb::work::Phase was = Work::g_tracker.phase;
                events = Work::g_tracker.Tick(in, dt);
                if (events & bb::work::kNewDay) Work::g_log.days[before.date] = before;
                sum = bb::work::Summarize(Work::g_tracker.day);
                sch = Work::g_tracker.schedule;
                block = Work::g_tracker.block;
                save = events || was != Work::g_tracker.phase || nowTick - lastSave > 60000;
                if (save) {
                    Work::SaveLocked();
                    lastSave = nowTick;
                }
            }
            if (s.workReminders && events) Work::Announce(events, sum, sch, block);
        } else {
            last = GetTickCount64();
        }
        Work::Publish(on);
        const HANDLE waits[2] = {g_stopEvent, g_workWake};
        const DWORD r = WaitForMultipleObjects(g_workWake ? 2 : 1, waits, FALSE, on ? 5000 : 15000);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
    }
    {
        std::lock_guard lock(Work::g_mutex);
        if (Work::g_loaded) Work::SaveLocked();
    }
    return 0;
}
