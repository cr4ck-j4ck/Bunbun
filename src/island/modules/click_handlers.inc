// Bunbun: Agent and Lookup clicks, hotkey, status clicks and timers.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

int AgentButtonAt(int x, int y) {
    if (g_agentButtons.empty()) return -1;
    const MediaContentPoint pt = MediaContentFromClient(x, y);
    if (!pt.valid) return -1;
    for (size_t i = 0; i < g_agentButtons.size(); ++i) {
        const D2D1_RECT_F& r = g_agentButtons[i].rect;
        if (pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom) return static_cast<int>(i);
    }
    return -1;
}

// Returns true when the click landed on an agent card button.
void HandleAgentAction(int action, int arg);  // Bunbun: studio_module.inc
bool HandleStudioWheel(HWND hwnd, LPARAM lParam, int step);  // Bunbun: studio_module.inc

bool HandleAgentClick(int x, int y) {
    const int i = AgentButtonAt(x, y);
    if (i < 0) return false;
    const AgentButton b = g_agentButtons[static_cast<size_t>(i)];
    if (b.action != 0) {  // Bunbun: plan feedback, reply, question steps
        HandleAgentAction(b.action, b.arg);
        return true;
    }
    // Clear at once so a double click cannot answer the next queued request.
    g_agentButtons.clear();
    g_agentHoveredButton = -1;
    bb::agents::Decide(g_agentButtonsRequest, b.decision, b.answerIndex);
    g_layoutDirty = true;
    return true;
}

void HandleAgentMouseMove(int x, int y) {
    const int i = AgentButtonAt(x, y);
    if (i != g_agentHoveredButton) {
        g_agentHoveredButton = i;
        g_layoutDirty = true;
    }
}

// Returns true when the click belonged to the panel (so it must not fall through
// to "open the media app" and friends).
bool HandleLookupClick(int x, int y) {
    LookupUiState& ui = g_lookupUi;
    if (!ui.open) return false;

    LookupStatus status = LookupStatus::Search;
    const std::vector<LookupRow> rows = CurrentLookupRows(&status);
    const MediaContentPoint pt = MediaContentFromClient(x, y);
    TouchLookupPanel();
    if (!pt.valid) return true;

    if (status == LookupStatus::Search) {
        const bool header = LookupHasHeader(ui, rows);
        if (header && LookupHasRecentRows(rows) && LookupClearHitTest(pt)) {
            {
                std::lock_guard lock(g_stateMutex);
                g_state.lookupRecent.clear();
            }
            ui.selected = 0;
            g_layoutDirty = true;
            return true;
        }
        const int row = LookupRowAtContentPoint(pt, static_cast<int>(rows.size()), header);
        if (row >= 0) {
            SubmitLookup(rows[static_cast<size_t>(row)].query);
            return true;
        }
    }
    if (LookupFieldHitTest(pt)) {
        ui.caret = ui.text.size();
        ui.selectAll = false;
        ui.caretResetAt = NowSeconds();
        g_layoutDirty = true;
    }
    return true;
}

void HandleLookupMouseMove(int x, int y) {
    LookupUiState& ui = g_lookupUi;
    LookupStatus status = LookupStatus::Search;
    const std::vector<LookupRow> rows = CurrentLookupRows(&status);
    TouchLookupPanel();
    if (status != LookupStatus::Search) return;

    const int row = LookupRowAtContentPoint(MediaContentFromClient(x, y),
                                            static_cast<int>(rows.size()),
                                            LookupHasHeader(ui, rows));
    if (row >= 0 && row != ui.selected) {
        ui.selected = row;
        g_layoutDirty = true;
    }
}

LPCWSTR LookupCursorAt(int x, int y) {
    LookupStatus status = LookupStatus::Search;
    const std::vector<LookupRow> rows = CurrentLookupRows(&status);
    const MediaContentPoint pt = MediaContentFromClient(x, y);
    if (status == LookupStatus::Search) {
        const bool header = LookupHasHeader(g_lookupUi, rows);
        if ((header && LookupHasRecentRows(rows) && LookupClearHitTest(pt)) ||
            LookupRowAtContentPoint(pt, static_cast<int>(rows.size()), header) >= 0) {
            return IDC_HAND;
        }
    }
    return LookupFieldHitTest(pt) ? IDC_IBEAM : IDC_ARROW;
}

// Registers (or releases) the Quick Lookup hotkey. Render thread only, because
// RegisterHotKey must run on the thread that owns the window.
void ApplyLookupHotkey() {
    HWND hwnd = g_hwnd;
    if (!hwnd) {
        return;
    }
    if (g_lookupHotkeyRegistered) {
        UnregisterHotKey(hwnd, ID_LOOKUP_HOTKEY);
        g_lookupHotkeyRegistered = false;
    }

    const Settings settings = GetSettingsCopy();
    // Bunbun: the tuck hotkey rides along with the lookup one.
    UnregisterHotKey(hwnd, ID_TUCK_HOTKEY);
    if (settings.tuckHotkey && !RegisterHotKey(hwnd, ID_TUCK_HOTKEY, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'H')) {
        Wh_Log(L"Failed to register the tuck hotkey Ctrl+Alt+H (error %lu); another app may own it.", GetLastError());
    }
    if (!settings.quickLookup) {
        return;
    }
    if (RegisterHotKey(hwnd, ID_LOOKUP_HOTKEY, settings.lookupModifiers, settings.lookupVk)) {
        g_lookupHotkeyRegistered = true;
    } else {
        Wh_Log(L"Failed to register Quick Lookup hotkey (error %lu).", GetLastError());
    }
}

void SetClickThrough(HWND hwnd, bool clickThrough) {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool has = (exStyle & WS_EX_TRANSPARENT) != 0;
    if (clickThrough == has) {
        return;
    }

    if (clickThrough) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }

    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void OpenRelevantApp();


void ToggleEndpointMute();
void SeekMediaToTicks(int64_t targetTicks);
void PasteIntoFileTray(HWND hwnd);
void SaveFileTray();
void LoadFileTray();
int AgentKindOf(const std::wstring& displayName);  // Bunbun: studio_module.inc

void HandleStatusClickAtPoint(HWND hwnd, LPARAM lParam) {
    bb::AgentView agent;
    std::wstring notifApp, notifTitle;
    bool hasAgent = false, hasNotif = false;
    {
        std::lock_guard lock(g_stateMutex);
        if (g_settings.bunbunAgents && g_state.agent.phase != bb::AgentPhase::None &&
            g_settings.showAgent[AgentKindOf(g_state.agent.agent)] &&  // Bunbun: per-agent switch
            (!g_state.agent.project.empty() || !g_state.agent.agent.empty())) {
            agent = g_state.agent;
            hasAgent = true;
        }
        if (g_state.notification.active) {
            notifApp = g_state.notification.app;
            notifTitle = g_state.notification.title;
            hasNotif = true;
        }
    }
    if (hasAgent) {
        FocusAgentApp(agent);
    } else if (hasNotif) {
        FocusNotificationApp(notifApp, notifTitle);
    }
}

// Bunbun: in seconds (the Timers page's countdown); a plain timer until the
// Timers page labels it.
void StartTimerSeconds(int seconds, bool isBreak) {
    seconds = std::max(1, seconds);
    {
        std::lock_guard lock(g_stateMutex);
        g_state.timer.active = true;
        g_state.timer.running = true;
        g_state.timer.isBreak = isBreak;
        g_state.timer.totalSeconds = seconds;
        g_state.timer.endsAt = NowSeconds() + seconds;
        g_state.timer.remainingAtPause = 0.0;
        g_state.timer.justFinished = false;
        g_state.timer.kind = kTimerKindTimer;
        g_state.timer.label.clear();
        g_state.timer.doneLabel.clear();
    }
    TriggerNudge();
}

void StartFocusTimer(int minutes, bool isBreak) {
    StartTimerSeconds(minutes * 60, isBreak);
}

void ToggleTimerPause() {
    {
        std::lock_guard lock(g_stateMutex);
        if (!g_state.timer.active) {
            return;
        }
        const double now = NowSeconds();
        if (g_state.timer.running) {
            g_state.timer.remainingAtPause = std::max(0.0, g_state.timer.endsAt - now);
            g_state.timer.running = false;
        } else {
            g_state.timer.endsAt = now + g_state.timer.remainingAtPause;
            g_state.timer.running = true;
        }
    }
    TriggerNudge();
}

void StopFocusTimer() {
    {
        std::lock_guard lock(g_stateMutex);
        g_state.timer.active = false;
        g_state.timer.running = false;
        g_state.timer.justFinished = false;
    }
    TriggerNudge();
}

