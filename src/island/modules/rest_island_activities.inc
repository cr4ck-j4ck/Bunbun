// Bunbun: The rest island and activity choice.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

// Bunbun: Separate displays. Another display's island at rest, drawn live by the
// second renderer: the island's last resting activity, in that display's shape and
// size. Shape and size go through g_settings too, because parts of the drawing read
// them there; they are put back at once (unless a settings reload replaced them).
bool RenderRestIsland(int shape, float scale, POINT origin, HDC* dc, SIZE* size, RECT* pill, bool tucked) {
    if (!g_restRenderer || !g_restState || !g_restLook.valid) return false;
    tucked = tucked || g_tucked.load(std::memory_order_relaxed);  // Bunbun: Tuck away folds every display
    Settings s = GetSettingsCopy();
    const bool notch = shape == 2, w11 = shape == 1;
    s.notchStyle = notch;
    s.w11Style = w11;
    s.sizeScale = scale;
    Activity rest = ActivityForKind(static_cast<IslandKind>(g_restLook.kind), s, *g_restState);
    rest.width = (g_restLook.width + (notch ? kNotchExtraContent : 0.0f)) * scale;
    rest.height = g_restLook.height * scale;
    float bunSlot = g_restLook.bunSlot * scale;
    if (tucked) {  // Bunbun: the same line the working island folds to
        rest.width = 60.0f * scale;
        rest.height = 6.0f * scale;
        bunSlot = 0.0f;
    }
    bool wasNotch, wasW11;
    float wasScale;
    {
        std::lock_guard lock(g_settingsMutex);
        wasNotch = g_settings.notchStyle;
        wasW11 = g_settings.w11Style;
        wasScale = g_settings.sizeScale;
        g_settings.notchStyle = notch;
        g_settings.w11Style = w11;
        g_settings.sizeScale = scale;
    }
    g_restRenderer->screenOrigin_ = origin;  // for the eyes
    g_restRenderer->restTuck_ = tucked;
    const bool ok = g_restRenderer->RenderOffscreen(*g_restState, s, rest, bunSlot, NowSeconds(),
                                                    dc, size, pill);
    {
        std::lock_guard lock(g_settingsMutex);
        if (g_settings.notchStyle == notch && g_settings.w11Style == w11 && g_settings.sizeScale == scale) {
            g_settings.notchStyle = wasNotch;
            g_settings.w11Style = wasW11;
            g_settings.sizeScale = wasScale;
        }
    }
    return ok;
}

Activity ActivityForKind(IslandKind kind, const Settings& settings, const SharedState& state) {
    Activity activity;
    activity.kind = kind;

    switch (kind) {
        case IslandKind::Media:
            activity.width = 150.0f;
            activity.height = 44.0f;
            break;
        case IslandKind::Progress:
            activity.width = 230.0f;
            activity.height = 48.0f;
            break;
        case IslandKind::Clipboard:
            activity.width = 340.0f;
            activity.height = 56.0f;
            break;
        case IslandKind::Notification: {
            // Bunbun: Samsung-style, in the island's own shape (Notch, Pill or Windows 11):
            // coming in it shows the app's icon at about its closed size, widens for the
            // title, then grows the second line; going away it steps back the same way.
            // The springs morph between the steps, and DrawNotification shows what fits.
            const double now = NowSeconds();
            const NotificationSnapshot& n = state.notification;
            const double age = now - (n.shownAt > 0.0 ? n.shownAt : n.expiresAt - 4.0);  // lifetimes vary (4-8 s)
            const double left = n.expiresAt - now;
            const double t = std::min(age, left + 0.05);  // the way out mirrors the way in
            if (t < 0.16) { activity.width = 170.0f; activity.height = 44.0f; }
            else if (t < 0.36) { activity.width = 290.0f; activity.height = 50.0f; }
            else { activity.width = 360.0f; activity.height = 70.0f; }
            break;
        }
        case IslandKind::Volume:
        case IslandKind::Brightness:
            activity.width = 300.0f;
            activity.height = 54.0f;
            break;
        case IslandKind::BatteryLow:
            activity.width = 290.0f;
            activity.height = 52.0f;
            break;
        case IslandKind::CapsLock:
            activity.width = 180.0f;
            activity.height = 42.0f;
            break;
        case IslandKind::Device:
            activity.width = 240.0f;
            activity.height = 50.0f;
            break;
        case IslandKind::Bluetooth:
            activity.width = 280.0f;
            activity.height = 54.0f;
            break;
        case IslandKind::Timer:
            activity.width = 260.0f;
            activity.height = 54.0f;
            break;
        case IslandKind::DoNotDisturb:
            activity.width = 220.0f;
            activity.height = 42.0f;
            break;
        case IslandKind::Reminder:  // Bunbun: small and silent when quiet
            activity.width = state.reminder.quiet ? 260.0f : 380.0f;
            activity.height = state.reminder.quiet ? 40.0f : 54.0f;
            break;
        case IslandKind::Privacy:   // Bunbun
            activity.width = 320.0f;
            activity.height = 40.0f;
            break;
        case IslandKind::Meeting:   // Bunbun
            activity.width = 330.0f;
            activity.height = 54.0f;
            break;
        case IslandKind::Download:  // Bunbun
            activity.width = 320.0f;
            activity.height = 54.0f;
            break;
        case IslandKind::Lookup:
            // Width is fixed; the real height is measured per result by
            // Renderer::MeasureLookupCard in the render loop. This is only a seed.
            activity.width = LookupLayout::kWidth;
            activity.height = 42.0f;
            break;
        case IslandKind::Agent:
            if (AgentLayout::IsCard(state.agent.phase)) {
                activity.width = AgentLayout::kCardWidth;
                activity.height = AgentLayout::kCardHeight;
                // Bunbun: a plan gets room to read; a question card one row per option.
                if (!state.agent.plan.empty()) {
                    activity.width = 480.0f;
                    activity.height = 320.0f;
                } else if (NeedsQuestionCard(state.agent)) {
                    const size_t idx = std::min<size_t>(static_cast<size_t>(std::max(0, g_questions.index)),
                                                        state.agent.questions.size() - 1);
                    const size_t rows = std::min<size_t>(state.agent.questions[idx].options.size(), 6);
                    activity.width = 440.0f;
                    activity.height = 128.0f + 30.0f * static_cast<float>(rows);
                } else if (!state.agent.diffOld.empty() || !state.agent.diffNew.empty()) {
                    // Bunbun: an edit grows the card to its lines (up to kDiffRowsShown; the
                    // wheel scrolls the rest): the file row, then the − and + lines.
                    const int rows = std::min<int>(static_cast<int>(bb::DiffRows(state.agent).size()), kDiffRowsShown);
                    activity.width = 480.0f;
                    activity.height = AgentLayout::kCardHeight - 28.0f + 30.0f + kDiffRowH * static_cast<float>(rows);
                }
            } else {
                activity.width = AgentLayout::kCompactWidth;
                activity.height = AgentLayout::kCompactHeight;
            }
            break;
        case IslandKind::Idle:
        default:
            if (settings.autoHideIdleSeconds == -1 && !state.system.micActive && !state.system.cameraActive) {
                activity.width = 0.0f;
                activity.height = 0.0f;
            } else {
                // Seed only. The real collapsed width is measured from the clock
                // and weather strings by Renderer::MeasureIdleStrip and applied in
                // the render loop -- this function has no DWrite access. A fixed
                // width here caused dead air around a short "9:41", and clipping on
                // "10:41:32 PM" once Text size reached 140.
                activity.width = settings.weather ? 170.0f : 96.0f;
                activity.height = IdleStripLayout::kHeight;
            }
            break;
    }

    // Bunbun: compact pills grow by the room the privacy dots need, so the dots
    // never cover their content (the idle strip and media pill reserve their own).
    if (kind != IslandKind::Idle && kind != IslandKind::Media && activity.width > 0.0f && activity.height <= 55.0f) {
        activity.width += PrivacyDotsReserve(state, settings);
    }
    activity.width *= settings.sizeScale;
    activity.height *= settings.sizeScale;
    return activity;
}

std::vector<IslandKind> ChooseActivities(const SharedState& state, const Settings& settings, double now) {
    std::vector<IslandKind> activities;

    if (settings.quickLookup && state.lookup.active && now < state.lookup.expiresAt) {
        activities.push_back(IslandKind::Lookup);
    }

    // Bunbun: a reminder that's due comes before everything but Ask Bun.
    if (settings.todoModule && state.reminder.active && now < state.reminder.expiresAt) {
        activities.push_back(IslandKind::Reminder);
    }

    // Bunbun: name an app the moment it starts using the mic, camera or screen.
    if (settings.privacyDots && state.privacyAlert.active && now < state.privacyAlert.expiresAt) {
        activities.push_back(IslandKind::Privacy);
    }

    // Bunbun: an agent waiting for you outranks every passive alert.
    const bool agentShown = settings.bunbunAgents && state.agent.phase != bb::AgentPhase::None &&
                            settings.showAgent[AgentKindFor(state.agent.agent)];  // Bunbun: per-agent switch
    if (agentShown && AgentLayout::IsCard(state.agent.phase)) {
        activities.push_back(IslandKind::Agent);
    }

    if (state.clipboard.active && now < state.clipboard.expiresAt) {
        activities.push_back(IslandKind::Clipboard);
    }
    // settings.capsLock is checked here, like every other module above and below.
    // Leaving it out was what made the Caps Lock toggle not actually work: the
    // handler gate stopped the nudge but the pill was still selected and drawn.
    if (settings.capsLock && state.capsLock.active && now < state.capsLock.expiresAt) {
        activities.push_back(IslandKind::CapsLock);
    }
    if (state.device.active && now < state.device.expiresAt) {
        activities.push_back(IslandKind::Device);
    }
    if (settings.bluetoothIndicator && state.bluetoothDevice.active &&
        now < state.bluetoothDevice.expiresAt) {
        activities.push_back(IslandKind::Bluetooth);
    }
    if (settings.doNotDisturbIndicator && state.doNotDisturb.active &&
        now < state.doNotDisturb.expiresAt) {
        activities.push_back(IslandKind::DoNotDisturb);
    }
    if (settings.volume && state.volume.active && now < state.volume.expiresAt) {
        activities.push_back(IslandKind::Volume);
    }
    if (settings.brightness && state.brightness.active && now < state.brightness.expiresAt) {
        activities.push_back(IslandKind::Brightness);
    }
    if (state.notification.active && now < state.notification.expiresAt) {
        activities.push_back(IslandKind::Notification);
    }
    if (settings.battery && state.battery.active && now < state.battery.expiresAt) {
        activities.push_back(IslandKind::BatteryLow);
    }
    if (settings.progress && state.progress.active) {
        activities.push_back(IslandKind::Progress);
    }
    if (settings.timerEnabled &&
        ((state.timer.justFinished && now < state.timer.finishedExpiresAt) ||
         state.timer.active || state.stopwatch.running)) {  // Bunbun: and a running stopwatch
        activities.push_back(IslandKind::Timer);
    }
    // Bunbun: a meeting about to start, and a browser download.
    if (MeetingPillWanted(state, settings, EpochNow())) {
        activities.push_back(IslandKind::Meeting);
    }
    if (DownloadPillWanted(state, settings, now)) {
        activities.push_back(IslandKind::Download);
    }
    // A working (or just-finished) agent sits beside the media pill.
    if (agentShown && settings.agentPill && !AgentLayout::IsCard(state.agent.phase)) {  // Bunbun: AgentPill
        activities.push_back(IslandKind::Agent);
    }
    if (settings.media && state.media.available) {
        activities.push_back(IslandKind::Media);
    }

    if (activities.empty()) {
        activities.push_back(IslandKind::Idle);
    }

    return activities;
}

constexpr UINT WM_APP_CAPSLOCK = WM_APP + 0x444;
HHOOK g_keyboardHook = nullptr;
HANDLE g_keyboardThread = nullptr;
DWORD g_keyboardThreadId = 0;

