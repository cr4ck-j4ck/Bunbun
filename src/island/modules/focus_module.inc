// Bunbun: Focus mode.
//
// A focus session runs the focus timer and, while it lasts: the mascot settles
// into its do-not-disturb mood, app notifications don't pop up on the island
// (they still reach Windows' notification centre), and reminders go quiet. When
// it ends (the timer runs out, or "stop focus") a summary card says how long it
// lasted, what got ticked off and how many notifications waited.
//
// Included in the core's anonymous namespace after StartFocusTimer /
// StopFocusTimer and the lookup module (the summary is an Ask Bun card).

std::atomic<bool> g_focusActive{false};  // read by the notification thread and the mascot
std::atomic<int> g_focusHeld{0};          // notifications held back this session

struct FocusSession {
    int minutes = 0;
    double startedAt = 0.0;     // Unix seconds
    std::vector<int> openAtStart;  // to-dos still open when it started
};
FocusSession g_focus;  // under g_stateMutex

bool FocusModeActive() {
    return g_focusActive.load(std::memory_order_relaxed);
}

void HoldNotificationForFocus() {
    g_focusHeld.fetch_add(1, std::memory_order_relaxed);
}

void StartFocus(int minutes) {
    minutes = std::clamp(minutes, 1, 8 * 60);
    StartFocusTimer(minutes, false);
    {
        std::lock_guard lock(g_stateMutex);
        g_focus = FocusSession{};
        g_focus.minutes = minutes;
        g_focus.startedAt = EpochNow();
        for (const TodoItem& t : g_state.todos)
            if (!t.done) g_focus.openAtStart.push_back(t.id);
    }
    g_focusHeld.store(0);
    g_focusActive.store(true);
    g_layoutDirty = true;
}

// Ends the session and shows its summary. `completed`: the timer ran out.
void EndFocus(bool completed, bool stopTimer) {
    if (!g_focusActive.exchange(false)) return;
    if (stopTimer) StopFocusTimer();
    int ticked = 0;
    double startedAt = 0.0;
    {
        std::lock_guard lock(g_stateMutex);
        startedAt = g_focus.startedAt;
        for (const TodoItem& t : g_state.todos)
            if (t.done && std::find(g_focus.openAtStart.begin(), g_focus.openAtStart.end(), t.id) != g_focus.openAtStart.end()) ++ticked;
    }
    const int minutes = std::max(1, static_cast<int>((EpochNow() - startedAt) / 60.0 + 0.5));
    const int held = g_focusHeld.exchange(0);

    std::wstring body = std::to_wstring(minutes) + L" min of focus.";
    if (ticked > 0) body += L" You ticked off " + std::to_wstring(ticked) + (ticked == 1 ? L" to-do." : L" to-dos.");
    if (held > 0) body += L" " + std::to_wstring(held) + (held == 1 ? L" notification" : L" notifications") + L" waited in Windows' notification centre.";
    if (ticked == 0 && held == 0) body += L" Nice and quiet.";

    LookupSnapshot card;
    card.active = true;
    card.status = LookupStatus::Found;
    card.source = L"Bun";
    card.subtitle = L"Bun";
    card.title = completed ? Loc(L"Focus done") : Loc(L"Focus ended");
    card.body = body;
    card.expiresAt = NowSeconds() + 15.0;
    PublishNewLookup(card);
    g_layoutDirty = true;
}

// Called each frame by the render loop with the timer's state.
void UpdateFocus(bool timerJustCompleted, bool timerActive) {
    if (!g_focusActive.load(std::memory_order_relaxed)) return;
    if (timerJustCompleted) EndFocus(true, false);
    else if (!timerActive) EndFocus(false, false);  // the timer was stopped another way (tray menu)
}
