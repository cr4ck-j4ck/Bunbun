// Bunbun: to-dos and reminders.
//
// Kept on this PC only, in todos.tsv next to settings.ini; the file format and
// the repeat maths live in src/common/todos.h, shared with the settings app's
// To-dos page. Ask Bun adds and ticks them off; the To-do tab lists them; a
// reminder thread fires a pill with Done / Snooze when one is due, and reloads
// the list when the settings app changes the file.
//
// Included in the core's anonymous namespace after calendar_module.inc (it uses
// EpochNow). Lives in g_state.todos / g_state.reminder under g_stateMutex.

HANDLE g_todoThread = nullptr;
HANDLE g_todoWake = nullptr;  // set when a to-do changes, so a new reminder is scheduled at once

// Focus mode (focus_module.inc) quiets reminders too.
bool FocusModeActive();

namespace Todos {

std::wstring FilePath() {
    std::wstring dir = bb::SettingsPath();
    dir.erase(dir.find_last_of(L"\\/"));
    return dir + L"\\todos.tsv";
}

// The file's last-write time, to tell our own saves from the settings app's.
ULONG64 FileWriteTime() {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(FilePath().c_str(), GetFileExInfoStandard, &d)) return 0;
    return (static_cast<ULONG64>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
}
std::atomic<ULONG64> g_ownWriteTime{0};

// Snoozed occurrences of repeating reminders: id -> when to ring again. Kept in
// memory, since the stored due time already points at the next occurrence.
std::map<int, double> g_snoozed;  // under g_stateMutex

// Saves g_state.todos and wakes the reminder thread. Call without the lock held.
void Commit() {
    std::vector<TodoItem> copy;
    {
        std::lock_guard lock(g_stateMutex);
        copy = g_state.todos;
    }
    bb::todos::Save(FilePath(), copy);
    g_ownWriteTime.store(FileWriteTime());
    if (g_todoWake) SetEvent(g_todoWake);
    g_layoutDirty = true;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
}

// The due time an Ask Bun intent asks for, in Unix seconds (0 = none). A clock
// time that has already passed today means tomorrow.
double DueFromIntent(const bb::assistant::Intent& in) {
    const double now = EpochNow();
    if (!in.repeat.empty()) {
        // Today at the asked time (9:00 if none); if that day or time doesn't fit
        // the repeat, the next one that does.
        SYSTEMTIME st;
        GetLocalTime(&st);
        st.wHour = static_cast<WORD>(in.dueHour >= 0 ? in.dueHour : 9);
        st.wMinute = static_cast<WORD>(in.dueHour >= 0 ? in.dueMinute : 0);
        st.wSecond = st.wMilliseconds = 0;
        const double today = bb::todos::FromLocal(st);
        if (today > now && (bb::todos::RepeatDays(in.repeat) & (1 << st.wDayOfWeek))) return today;
        return bb::todos::NextOccurrence(in.repeat, today, now);
    }
    if (in.dueInMinutes >= 0) return now + in.dueInMinutes * 60.0;
    if (in.dueHour < 0) return 0.0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    Ics::WallTime w{st.wYear, st.wMonth, st.wDay, in.dueHour, in.dueMinute, 0};
    int64_t day = Ics::DaysFromCivil(w.y, w.mo, w.d) + in.dueDays;
    Ics::CivilFromDays(day, &w.y, &w.mo, &w.d);
    double due = Ics::ToEpoch(w, L"");
    if (in.dueDays == 0 && due <= now) {
        Ics::CivilFromDays(day + 1, &w.y, &w.mo, &w.d);
        due = Ics::ToEpoch(w, L"");
    }
    return due;
}

// "3:00 PM", "Tomorrow 9:00 AM", "Mon 9:00 AM".
std::wstring FormatDue(double due) {
    const ULONGLONG t = static_cast<ULONGLONG>((due + 11644473600.0) * 1e7);
    FILETIME ft{static_cast<DWORD>(t), static_cast<DWORD>(t >> 32)}, lft;
    SYSTEMTIME st{}, today;
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    GetLocalTime(&today);
    wchar_t clock[32] = {};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, clock, 32);
    const int64_t days = Ics::DaysFromCivil(st.wYear, st.wMonth, st.wDay) - Ics::DaysFromCivil(today.wYear, today.wMonth, today.wDay);
    if (days == 0) return clock;
    if (days == 1) return std::wstring(Loc(L"Tomorrow")) + L" " + clock;
    wchar_t dayName[16] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, days > 0 && days < 7 ? L"ddd" : L"d MMM", dayName, 16, nullptr);
    return std::wstring(dayName) + L" " + clock;
}

// The open to-do that best matches what the user said: an exact match, else
// the first one containing it.
int FindOpen(const std::vector<TodoItem>& items, const std::wstring& said) {
    const std::wstring want = ToLowerCopy(said);
    for (const TodoItem& t : items)
        if (!t.done && ToLowerCopy(t.text) == want) return t.id;
    for (const TodoItem& t : items)
        if (!t.done && ToLowerCopy(t.text).find(want) != std::wstring::npos) return t.id;
    return 0;
}

}  // namespace Todos

void LoadTodos() {
    std::vector<TodoItem> items = bb::todos::Load(Todos::FilePath());
    Todos::g_ownWriteTime.store(Todos::FileWriteTime());
    {
        std::lock_guard lock(g_stateMutex);
        g_state.todos = std::move(items);
    }
    g_layoutDirty = true;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
}

// Done (or not done) by id; clears a reminder pill showing it. A repeating
// reminder is never finished by Done. When it rings it is already rescheduled
// for next time, so Done on its pill (or "done …") only dismisses it; ticking it
// on the To-do tab (`skipNext`) skips the next occurrence instead.
void SetTodoDone(int id, bool done, bool skipNext = false) {
    {
        std::lock_guard lock(g_stateMutex);
        for (TodoItem& t : g_state.todos) {
            if (t.id != id) continue;
            if (done && !t.repeat.empty()) {
                if (skipNext) {
                    t.due = bb::todos::NextOccurrence(t.repeat, t.due, std::max(EpochNow(), t.due));
                    t.notified = false;
                }
            } else {
                t.done = done;
            }
        }
        Todos::g_snoozed.erase(id);
        if (g_state.reminder.id == id) g_state.reminder.active = false;
    }
    Todos::Commit();
}

void SnoozeTodo(int id, int minutes) {
    {
        std::lock_guard lock(g_stateMutex);
        for (TodoItem& t : g_state.todos) {
            if (t.id != id) continue;
            if (!t.repeat.empty()) {
                Todos::g_snoozed[id] = EpochNow() + minutes * 60.0;  // the series keeps its schedule
                continue;
            }
            t.due = EpochNow() + minutes * 60.0;
            t.notified = false;
        }
        if (g_state.reminder.id == id) g_state.reminder.active = false;
    }
    Todos::Commit();
}

// Ask Bun's to-do commands. Returns the reply; `ran` says the command changed
// something (so the Ask Bun card only flashes it).
std::wstring RunTodoIntent(const bb::assistant::Intent& asked, bool* ran) {
    namespace as = bb::assistant;
    as::Intent in = asked;
    if (in.repeat == L"weekly") {  // "every week": on today's weekday
        SYSTEMTIME st;
        GetLocalTime(&st);
        in.repeat = L"weekly:" + std::to_wstring(st.wDayOfWeek);
    }
    *ran = false;
    std::wstring reply;
    switch (in.action) {
        case as::Action::TodoAdd: {
            const double due = Todos::DueFromIntent(in);
            {
                std::lock_guard lock(g_stateMutex);
                int id = 1;
                for (const TodoItem& t : g_state.todos) id = std::max(id, t.id + 1);
                TodoItem t;
                t.id = id;
                t.text = in.label;
                t.due = due;
                t.repeat = due > 0.0 ? in.repeat : std::wstring();
                g_state.todos.push_back(std::move(t));
            }
            Todos::Commit();
            *ran = true;
            if (due > 0.0 && !in.repeat.empty()) {
                SYSTEMTIME st = bb::todos::ToLocal(due);
                wchar_t clock[32] = {};
                GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, clock, 32);
                reply = std::wstring(Loc(L"I'll remind you")) + L" " + ToLowerCopy(bb::todos::DescribeRepeat(in.repeat)) + L" at " +
                        clock + L": " + in.label;
            } else {
                reply = due > 0.0 ? std::wstring(Loc(L"I'll remind you")) + L" " + Todos::FormatDue(due) + L": " + in.label
                                  : std::wstring(Loc(L"Added to your list:")) + L" " + in.label;
            }
            break;
        }
        case as::Action::TodoDone: {
            int id = 0;
            std::wstring text;
            {
                std::lock_guard lock(g_stateMutex);
                id = Todos::FindOpen(g_state.todos, in.label);
                for (const TodoItem& t : g_state.todos)
                    if (t.id == id) text = t.text;
            }
            if (id == 0) {
                reply = L"I couldn’t find “" + in.label + L"” on your list.";
            } else {
                SetTodoDone(id, true);
                *ran = true;
                reply = L"Done: " + text + L" ✓";
            }
            break;
        }
        case as::Action::TodoClearDone: {
            size_t removed = 0;
            {
                std::lock_guard lock(g_stateMutex);
                const size_t before = g_state.todos.size();
                g_state.todos.erase(std::remove_if(g_state.todos.begin(), g_state.todos.end(), [](const TodoItem& t) { return t.done; }),
                                    g_state.todos.end());
                removed = before - g_state.todos.size();
            }
            Todos::Commit();
            *ran = true;
            reply = removed ? L"Cleared " + std::to_wstring(removed) + L" finished to-do" + (removed == 1 ? L"." : L"s.")
                            : std::wstring(L"Nothing finished to clear.");
            break;
        }
        default: {  // TodoList
            std::vector<TodoItem> items;
            {
                std::lock_guard lock(g_stateMutex);
                items = g_state.todos;
            }
            int n = 0;
            for (const TodoItem& t : items) {
                if (t.done) continue;
                reply += std::to_wstring(++n) + L". " + t.text;
                if (t.due > 0.0) reply += L" (" + Todos::FormatDue(t.due) + (t.repeat.empty() ? L"" : L", " + ToLowerCopy(bb::todos::DescribeRepeat(t.repeat))) + L")";
                reply += L"\n";
            }
            if (n == 0) reply = L"Your list is empty. Try “add buy milk” or “remind me at 3pm to call Sam”.";
            else reply.pop_back();
            break;
        }
    }
    return reply;
}

// Quiet reminders: no sound and a small pill while a full-screen app runs,
// you're presenting, a meeting from the calendar is on, or Focus mode is on.
bool RemindersShouldBeQuiet() {
    if (!GetSettingsCopy().quietReminders) return false;
    if (g_isFullscreen.load(std::memory_order_relaxed) || FocusModeActive()) return true;
    QUERY_USER_NOTIFICATION_STATE qs{};
    if (SUCCEEDED(SHQueryUserNotificationState(&qs)) &&
        (qs == QUNS_BUSY || qs == QUNS_RUNNING_D3D_FULL_SCREEN || qs == QUNS_PRESENTATION_MODE)) {
        return true;
    }
    std::lock_guard lock(g_stateMutex);
    const double now = EpochNow();
    return g_state.meeting.valid && now >= g_state.meeting.startsAt && now < g_state.meeting.endsAt;
}

// Fires reminders when they're due. Wakes for the next due time, when a to-do
// changes, or when the settings app rewrites the file.
DWORD WINAPI TodoThreadProc(void*) {
    LoadTodos();
    std::wstring dir = Todos::FilePath();
    dir.erase(dir.find_last_of(L"\\/"));
    HANDLE change = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    for (;;) {
        double next = 0.0;
        std::wstring fired;
        int firedId = 0;
        {
            std::lock_guard lock(g_stateMutex);
            const double now = EpochNow();
            for (TodoItem& t : g_state.todos) {
                if (t.done || t.notified || t.due <= 0.0) continue;
                if (t.due <= now + 0.5 && fired.empty()) {
                    fired = t.text;
                    firedId = t.id;
                    // A repeating one is scheduled straight on to its next occurrence.
                    if (!t.repeat.empty()) t.due = bb::todos::NextOccurrence(t.repeat, t.due, now);
                    else t.notified = true;
                } else if (t.due > now && (next == 0.0 || t.due < next)) {
                    next = t.due;
                }
            }
            for (auto it = Todos::g_snoozed.begin(); it != Todos::g_snoozed.end();) {
                if (it->second <= now + 0.5 && fired.empty()) {
                    for (const TodoItem& t : g_state.todos)
                        if (t.id == it->first) { fired = t.text; firedId = t.id; }
                    it = Todos::g_snoozed.erase(it);
                    continue;
                }
                if (next == 0.0 || it->second < next) next = it->second;
                ++it;
            }
        }
        if (firedId) {
            const bool quiet = RemindersShouldBeQuiet();
            {
                std::lock_guard lock(g_stateMutex);
                g_state.reminder.active = true;
                g_state.reminder.id = firedId;
                g_state.reminder.text = fired;
                g_state.reminder.quiet = quiet;
                g_state.reminder.expiresAt = NowSeconds() + (quiet ? 20.0 : 60.0);
            }
            Todos::Commit();
            if (!quiet) {
                TriggerNudge();
                // Windows' own reminder sound (the sound scheme's "Calendar Reminder"),
                // falling back to the default notification sound.
                if (GetSettingsCopy().reminderSound &&
                    !PlaySoundW(L"Notification.Reminder", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT)) {
                    PlaySoundW(L"Notification.Default", nullptr, SND_ALIAS | SND_ASYNC);
                }
            }
            continue;  // there may be another one due
        }
        const DWORD wait = next > 0.0 ? static_cast<DWORD>(std::clamp((next - EpochNow()) * 1000.0, 200.0, 60000.0)) : 60000;
        const HANDLE waits[3] = {g_stopEvent, g_todoWake, change};
        const DWORD count = change != INVALID_HANDLE_VALUE ? 3 : 2;
        const DWORD r = WaitForMultipleObjects(g_todoWake ? count : 1, waits, FALSE, wait);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
        if (r == WAIT_OBJECT_0 + 2) {
            FindNextChangeNotification(change);
            // The settings app changed the list (not one of our own saves).
            const ULONG64 written = Todos::FileWriteTime();
            if (written != 0 && written != Todos::g_ownWriteTime.load()) LoadTodos();
        }
    }
    if (change != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change);
    return 0;
}

// ── The To-do tab and the reminder pill: layout shared with the hit tests ────
namespace TodoLayout {
    constexpr float kPadX = 22.0f;
    constexpr float kListTop = 40.0f;
    constexpr float kRowHeight = 26.0f;
    constexpr int kMaxRows = 5;
    // Reminder pill buttons, from the pill's right edge (unscaled pill pixels).
    constexpr float kButtonW = 64.0f;
    constexpr float kButtonGap = 8.0f;
    constexpr float kButtonRight = 14.0f;
}

int TodoTabIndex(const Settings& settings) {
    if (!settings.todoModule) return -1;
    return DashboardPageIndex(StudioPage::Todo, settings);
}

// What the To-do tab shows, in order: open to-dos (soonest due first), then
// finished ones, up to kMaxRows.
std::vector<TodoItem> TodoRows(const std::vector<TodoItem>& all) {
    std::vector<TodoItem> rows = all;
    std::stable_sort(rows.begin(), rows.end(), [](const TodoItem& a, const TodoItem& b) {
        if (a.done != b.done) return !a.done;
        const double da = a.due > 0.0 ? a.due : 1e18, db = b.due > 0.0 ? b.due : 1e18;
        return da < db;
    });
    if (rows.size() > static_cast<size_t>(TodoLayout::kMaxRows)) rows.resize(TodoLayout::kMaxRows);
    return rows;
}

// A click on the To-do tab ticks the row's to-do (or unticks it).
bool TodoTabClick(const MediaContentPoint& pt) {
    using namespace TodoLayout;
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight || pt.y < kListTop) return false;
    if (pt.x < kPadX - 6.0f || pt.x > pt.width - kPadX + 6.0f) return false;
    const int row = static_cast<int>((pt.y - kListTop) / kRowHeight);
    std::vector<TodoItem> rows;
    {
        std::lock_guard lock(g_stateMutex);
        rows = TodoRows(g_state.todos);
    }
    if (row < 0 || row >= static_cast<int>(rows.size())) return false;
    SetTodoDone(rows[static_cast<size_t>(row)].id, !rows[static_cast<size_t>(row)].done, true);  // repeating: skip the next one
    return true;
}

// Where the reminder pill last drew its buttons, in window (client) pixels. The
// pill records them while drawing, so the click test matches whatever layout it
// had: split beside music or an agent, scaled, or at any DPI. Render thread only.
D2D1_RECT_F g_reminderDoneRect{};
D2D1_RECT_F g_reminderSnoozeRect{};
D2D1_RECT_F g_reminderQuietRect{};  // the whole quiet pill
double g_reminderButtonsAt = -1.0;

// The reminder pill: Done or Snooze 10 min. Returns true when a button was hit.
bool ReminderPillClick(int x, int y) {
    if (NowSeconds() - g_reminderButtonsAt > 1.0) return false;  // not on screen
    auto inside = [&](const D2D1_RECT_F& r) {
        return x >= r.left - 3.0f && x <= r.right + 3.0f && y >= r.top - 4.0f && y <= r.bottom + 4.0f;  // a little slack
    };
    bool quiet = false;
    {
        std::lock_guard lock(g_stateMutex);
        quiet = g_state.reminder.active && g_state.reminder.quiet;
    }
    if (quiet) {  // a tap on the small pill opens the full one, with its buttons
        if (!inside(g_reminderQuietRect)) return false;
        std::lock_guard lock(g_stateMutex);
        g_state.reminder.quiet = false;
        g_state.reminder.expiresAt = NowSeconds() + 60.0;
        g_layoutDirty = true;
        return true;
    }
    const bool done = inside(g_reminderDoneRect);
    const bool snooze = !done && inside(g_reminderSnoozeRect);
    if (!done && !snooze) return false;
    int id = 0;
    {
        std::lock_guard lock(g_stateMutex);
        if (!g_state.reminder.active) return false;
        id = g_state.reminder.id;
    }
    if (done) SetTodoDone(id, true);
    else SnoozeTodo(id, 10);
    g_layoutDirty = true;
    return true;
}
