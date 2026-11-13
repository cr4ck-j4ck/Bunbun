// Bunbun: SharedState, springs, settings copies, power saving and tab / source helpers.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

inline bb::mascot::Mood AgentMood(bb::AgentPhase phase) {
    switch (phase) {
        case bb::AgentPhase::Thinking: return bb::mascot::Mood::Thinking;
        case bb::AgentPhase::Working: return bb::mascot::Mood::Working;
        case bb::AgentPhase::Approval:
        case bb::AgentPhase::Question: return bb::mascot::Mood::Alert;
        case bb::AgentPhase::Finished: return bb::mascot::Mood::Happy;
        case bb::AgentPhase::Error: return bb::mascot::Mood::Sad;
        default: return bb::mascot::Mood::Idle;
    }
}

struct SharedState {
    MediaSnapshot media;
    std::vector<MediaSourceInfo> mediaSources;  // stable dock order
    ClipboardSnapshot clipboard;
    NotificationSnapshot notification;
    VolumeSnapshot volume;
    BrightnessSnapshot brightness;
    CapsLockSnapshot capsLock;
    TimerSnapshot timer;
    StopwatchSnapshot stopwatch;  // Bunbun: the Timers page's stopwatch
    DeviceSnapshot device;
    BluetoothDeviceSnapshot bluetoothDevice;
    DoNotDisturbSnapshot doNotDisturb;
    BatterySnapshot battery;
    MeetingSnapshot meeting;    // Bunbun: calendar link
    // Bunbun: its events from 6 weeks back to 10 ahead, by start. Shared, so the per-frame
    // state copy doesn't copy every event (the calendar thread swaps in a new list).
    std::shared_ptr<const std::vector<CalendarItem>> calendar;
    PrivacyAlertSnapshot privacyAlert;  // Bunbun: "<app> is using your microphone"
    std::vector<TodoItem> todos;        // Bunbun: to-dos (todos_module.inc)
    ReminderSnapshot reminder;          // Bunbun: a reminder that's due
    WaterSnapshot water;                // Bunbun: today's water (water_module.inc)
    WorkSnapshot work;                  // Bunbun: Work mode's day (work_module.inc)
    DownloadSnapshot download;  // Bunbun: browser downloads
    ProgressSnapshot progress;
    SystemSnapshot system;
    ConnectivitySnapshot connectivity;  // Bunbun
    WeatherSnapshot weather;
    LyricsSnapshot lyrics;
    LookupSnapshot lookup;
    std::vector<LookupRecent> lookupRecent;  // newest first, guarded by g_stateMutex
    std::array<float, 48> waveform{};
    size_t waveformWrite = 0;
    std::array<float, kSpectrumBands> bands{};  // smoothed 0..1 magnitude per band (low -> high)
    bool muted = false;
    std::vector<FileTrayItem> fileTrayItems;
    bb::AgentView agent;  // Bunbun: written by IslandSetAgentView
};

struct SpringValue {
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;

    void Reset(float v) {
        value = target = v;
        velocity = 0.0f;
    }

    void Step(float totalDt, float stiffness, float damping) {
        const float kFixedDt = 0.0005f;
        while (totalDt > 0.0f) {
            float dt = std::min(totalDt, kFixedDt);
            const float displacement = value - target;
            const float acceleration = -stiffness * displacement - damping * velocity;
            velocity += acceleration * dt;
            value += velocity * dt;
            totalDt -= dt;
        }

        // Bunbun: settle once the motion is far below a pixel. The old 0.01 / 0.01
        // limits let sub-pixel nudges (text-measured widths shifting by a hair)
        // keep a spring "moving" forever, and with it a full-refresh-rate redraw.
        if (std::fabs(value - target) < 0.05f && std::fabs(velocity) < 0.5f) {
            value = target;
            velocity = 0.0f;
        }
    }
};

Settings g_settings;

// Guards every access to g_settings. LoadSettings() replaces the whole struct
// from the settings thread *and* from the tray context menu (which runs
// on the render thread), while the render, weather and media threads read it
// concurrently. Settings holds std::wstring / std::vector members, so an
// unsynchronised assignment can free a buffer a reader is still walking -- not
// a stale-value glitch but a genuine use-after-free.
//
// This is a strict LEAF lock: never acquire another mutex while holding it.
// That keeps it deadlock-free even where a caller already owns g_stateMutex
// (see WeatherThreadProc), because no path can ever take the two in the
// opposite order.
std::mutex g_settingsMutex;
HANDLE g_workWake = nullptr;  // Bunbun: wakes Work mode's thread (work_module.inc) after a button or a settings change

// Returns a private copy of the current settings. Callers then read from the
// copy for the rest of the frame, which also means a settings change landing
// mid-frame can't tear a single render across two configurations.
Settings GetSettingsCopy() {
    std::lock_guard lock(g_settingsMutex);
    return g_settings;
}

std::mutex g_stateMutex;
SharedState g_state;
std::atomic<uint64_t> g_artGenerationCounter = 0;

HWND g_hwnd = nullptr;
HANDLE g_stopEvent = nullptr;
HANDLE g_settingsChangedEvent = nullptr;
HANDLE g_renderThread = nullptr;
HANDLE g_mediaThread = nullptr;
HANDLE g_audioThread = nullptr;
HANDLE g_weatherThread = nullptr;
HANDLE g_lyricsThread = nullptr;
HANDLE g_notificationThread = nullptr;
HANDLE g_bluetoothThread = nullptr;
std::atomic<bool> g_running = false;
std::atomic<int> g_idleTab = 0;
std::atomic<double> g_lastMascotPokeTime{0.0};
std::atomic<double> g_lastIslandInteractionTime{0.0};
std::atomic<int> g_activityIndex = 0;
// Bunbun: set when the wheel moves the ring, so the render loop follows it to
// whatever stop it lands on (the first one included).
std::atomic<bool> g_ringStepped{false};
std::atomic<int> g_activeActivityCount = 1;
std::atomic<int> g_ringActivityCount{0};  // Bunbun: activities ahead of the dashboard tabs in the ring
// True while a media pill exists, i.e. while the Lyrics tab can be shown.
// Set by the render loop; read by ActiveTabCount().
std::atomic<bool> g_lyricsTabAvailable{false};

// Lyrics sync offset. Written rarely (settings load, right-click nudge, default-output
// change) and only read by draw code, so no locks are needed.
std::atomic<bool> g_defaultOutputIsBluetooth{false};  // set by BindVolumeEndpoint(), event-driven
std::atomic<int> g_lyricsEffectiveOffsetMs{0};        // speakers / wired: setting + right-click nudge
std::atomic<int> g_lyricsEffectiveOffsetBtMs{0};      // Bluetooth output: setting + right-click nudge
// Set when the default render endpoint must be re-read. Declared up here (not next to
// VolumeNotifyClient) so the Bluetooth watcher, which comes earlier in the file, can set it.
std::atomic<bool> g_volumeEndpointStale{true};
std::atomic<bool> g_studioVolumeDrag{false};
std::atomic<double> g_suppressVolumeNotificationUntil{0.0};
std::atomic<bool> g_layoutDirty = true;
std::atomic<bool> g_clickExpanded = false;
std::atomic<int> g_pressedMediaButton = -1;
std::atomic<int> g_hoveredMediaButton = -1;
std::atomic<unsigned> g_skipTriggerPrev{0};  // bumped on click, the render thread plays the prev button's animation
std::atomic<unsigned> g_skipTriggerNext{0};  // same for the next button

// Media source selection (source dock + right-click submenu).
std::mutex g_mediaSourceMutex;                     // leaf lock for the two strings below
std::wstring g_preferredMediaSource;               // AUMID the user picked; empty = Auto
std::wstring g_switchTarget;                       // pending animated switch
std::wstring g_dockOverrideAumid;                  // temporary pick from the dock; guarded by g_mediaSourceMutex
std::atomic<unsigned> g_dockOverrideRev{0};        // bumped on every dock pick / Auto reset
std::atomic<int> g_switchDir{1};
std::atomic<unsigned> g_switchSeq{0};
std::atomic<double> g_userSourceSwitchAt{-100.0};  // NowSeconds() of the last user switch
HANDLE g_mediaRefreshEvent = nullptr;              // wakes MediaThreadProc
std::atomic<int> g_hoveredSourceSlot{-1};
std::atomic<int> g_pressedSourceSlot{-1};
std::atomic<int> g_hoveredFileTrayRow = -1;  // row index under the cursor on the File Tray card
std::atomic<int> g_hoveredConnectivityCard = -1;  // Bunbun: Connectivity tab card under the cursor
int g_connectivityCards = 3;  // Bunbun: cards last drawn on the Connectivity tab (4 with a battery); render thread only
std::atomic<int> g_hoveredFileTrayAction = -1;  // -2 = bin button, >=0 = cut button of that row
std::atomic<bool> g_trayDragOver{false};      // a file drag from outside is hovering the island
std::atomic<bool> g_trayInternalDrag{false};  // a drag that started from the tray itself is running
std::atomic<int> g_tabEdgeBump{0};            // +1 / -1: wheel or swipe pushed past the last / first tab
std::atomic<double> g_tabLastPaintAt{0.0};    // last time the tab view painted a moving frame
std::atomic<bool> g_tabAnimating{false};      // a tab transition is in motion (render loop paces frames for it)
bool g_oleInitialized = false;
std::atomic<bool> g_scrubbing = false;           // true while press-dragging the media timeline scrubber
std::atomic<float> g_scrubDragFraction = 0.0f;   // live 0..1 drag position while g_scrubbing is true
std::atomic<double> g_lastLiveSeekTime = 0.0;    // throttle gate for live seeks while dragging
std::atomic<bool> g_audioCaptureNeeded = false;  // gates the WASAPI loopback thread
std::atomic<bool> g_spectrumBandsNeeded = false; // Bars, Pulse Orb and Plasma Thread read the frequency bands
std::atomic<bool> g_manuallyHidden = false;      // user-toggled hide, via menu or hotkey
UINT g_registeredHotkeyModifiers = 0;            // modifiers currently registered with the OS
UINT g_registeredHotkeyVk = 0;                   // vk currently registered with the OS
bool g_hotkeyRegistered = false;

// --- Zero-CPU parking for idle/fullscreen auto-hide (mirrors g_manuallyHidden) ---
std::atomic<bool> g_autoHiddenParked = false;          // true while OS-hidden due to idle/fullscreen auto-hide
std::atomic<bool> g_fullscreenOverrideVisible = false; // user forced the island visible via hotkey while fullscreen
std::atomic<bool> g_isFullscreen = false;              // cached fullscreen state, shared with the hotkey handler
std::atomic<double> g_hotkeyUnhideUntil = 0.0;         // grace deadline to keep island visible after hotkey / settings unhide
FILETIME g_prevIdleTime = {};
FILETIME g_prevKernelTime = {};
FILETIME g_prevUserTime = {};
UINT g_shellHookMessage = 0;
UINT g_taskbarCreatedMessage = 0;
bool g_brightnessInitialized = false;
int g_lastBrightness = -1;
std::atomic<double> g_fullscreenSeenAt{-100.0};      // Bunbun: when a full-screen app was last in front (DND pill filter)
std::atomic<double> g_suppressBrightnessUntil{0.0};  // Bunbun: no popup for a slider drag on the System page
BYTE g_brightnessPowerSource = 255;  // ACLineStatus of the previous sample
std::atomic<double> g_lastNudgeTime = 0.0;
std::atomic<double> g_lastLyricsViewTime = 0.0;

std::mutex g_bluetoothBatteryCacheMutex;
std::unordered_map<std::wstring, int> g_bluetoothBatteryCache;  // Bluetooth device Id -> last known battery percent (-1 = never learned)
std::atomic<uint64_t> g_bluetoothConnectGeneration = 0;
std::atomic<bool> g_isDnDActive = false;
// Bunbun: until this GetTickCount64(), the media poll keeps the position the scrubber
// set on release instead of a stale one from the player.
std::atomic<ULONGLONG> g_seekHoldUntil{0};
bool FocusModeActive();             // Bunbun: focus_module.inc
void HoldNotificationForFocus();    // Bunbun: focus_module.inc
void* g_wnfDndSubscription = nullptr;

// Bunbun: battery saver. On when the setting says so, or (auto) when the PC runs
// on battery or Windows' own battery saver is on. Read by the render loop only.
bool g_powerSaving = false;
long long g_framesDrawn = 0;  // Bunbun: render-thread only, for BUNBUN_LOG diagnostics
double g_renderMsTotal = 0.0, g_ulwMsTotal = 0.0, g_loopBusyMs = 0.0;  // Bunbun: same; time in Render() and in UpdateLayeredWindow
double QpcMs() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }
void UpdatePowerSaving(int mode, double now) {
    static double s_checkedAt = -1e9;
    if (mode == 1) { g_powerSaving = true; return; }
    if (mode == 2) { g_powerSaving = false; return; }
    if (now - s_checkedAt < 5.0) return;  // power state changes rarely
    s_checkedAt = now;
    SYSTEM_POWER_STATUS ps{};
    g_powerSaving = GetSystemPowerStatus(&ps) && (ps.ACLineStatus == 0 || (ps.SystemStatusFlag & 1) != 0);
}

std::atomic<bool> g_gameOverlayPinned{false};
// Bunbun: the game overlay keeps hover from expanding the island (so a mouse near
// the top mid-game doesn't pop it open), which also made the dashboard impossible
// to scroll. Scrolling the wheel over the island is deliberate, so it opens a
// "peek" that shows the normal dashboard until the cursor has been away a moment.
std::atomic<double> g_gameOverlayPeekUntil{0.0};
double NowSeconds();
bool GameOverlayShown(bool enabled) {
    return (enabled || g_gameOverlayPinned.load()) && NowSeconds() >= g_gameOverlayPeekUntil.load(std::memory_order_relaxed);
}
std::atomic<bool> g_pinnedExpanded{false};
std::atomic<bool> g_hoverSuppressed{false};  // Bunbun: suppresses hover expansion after Close until mouse leaves
std::atomic<bool> g_capturingPages{false};  // Bunbun: IslandCapturePages is stepping through the pages
std::atomic<bool> g_dashboardOpen{false};    // Bunbun: the dashboard (not an activity) is open on screen
std::atomic<int> g_pillOpacityOverride{-1};

constexpr GUID kSubTypeIeeeFloat = {
    0x00000003,
    0x0000,
    0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71},
};



double NowSeconds() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

float Clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

int ClampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

// ── Lyrics sync offset ───────────────────────────────────────────────────────
// Convention: positive offset = lyrics appear EARLIER, negative = LATER.
// Draw code only reads the atomics, so nothing here takes a lock or polls anything.
constexpr double kLyricsBaseLookaheadSec = 0.49;  // built-in baseline; the user offset is added on top
constexpr int kLyricsOffsetLimitMs = 3000;

// Effective offset = Mod Settings value + right-click nudge, clamped. Called from
// LoadSettings() and from the right-click menu.
void PublishLyricsOffsets(int settingMs, int settingBtMs) {
    g_lyricsEffectiveOffsetMs.store(
        ClampInt(settingMs + Wh_GetIntValue(L"LyricsNudgeMs", 0),
                 -kLyricsOffsetLimitMs, kLyricsOffsetLimitMs),
        std::memory_order_relaxed);
    g_lyricsEffectiveOffsetBtMs.store(
        ClampInt(settingBtMs + Wh_GetIntValue(L"LyricsNudgeBtMs", 0),
                 -kLyricsOffsetLimitMs, kLyricsOffsetLimitMs),
        std::memory_order_relaxed);
}

// Seconds added to the reported playback position for every lyric surface.
double LyricsLookaheadSec() {
    const int offsetMs = g_defaultOutputIsBluetooth.load(std::memory_order_relaxed)
                             ? g_lyricsEffectiveOffsetBtMs.load(std::memory_order_relaxed)
                             : g_lyricsEffectiveOffsetMs.load(std::memory_order_relaxed);
    return kLyricsBaseLookaheadSec + static_cast<double>(offsetMs) / 1000.0;
}

// Smoothstep easing (ease in / ease out). Used by the Lyrics tab.
float SmoothStep01(float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ── Media source selection ───────────────────────────────────────────────────
int ActiveMediaSourceIndex(const SharedState& state) {
    for (size_t i = 0; i < state.mediaSources.size(); ++i) {
        if (state.mediaSources[i].aumid == state.media.sourceAppUserModelId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::wstring GetPreferredMediaSource() {
    std::lock_guard lock(g_mediaSourceMutex);
    return g_preferredMediaSource;
}

// Instant switch. Empty AUMID = Auto. The timestamp tells MediaThreadProc this title
// change is not a "new track": no auto-expand, no 5s recentTrackChange window.
void RequestMediaSource(const std::wstring& aumid) {
    {
        std::lock_guard lock(g_mediaSourceMutex);
        // Dock pick: a TEMPORARY override, not a lock. MediaThreadProc drops it as soon as
        // something new starts playing or that app goes away, so Auto keeps working.
        g_dockOverrideAumid = aumid;
        g_dockOverrideRev.fetch_add(1);
    }
    g_userSourceSwitchAt.store(NowSeconds());
    if (g_mediaRefreshEvent) {
        SetEvent(g_mediaRefreshEvent);
    }
    g_layoutDirty = true;
}

// Right-click menu pick. Empty AUMID = Auto. A real AUMID LOCKS the island to that app
// until Auto or another app is picked; what starts playing elsewhere is ignored.
// Any temporary dock pick is cleared, so "Auto" really means "newest playing app".
void LockMediaSource(const std::wstring& aumid) {
    {
        std::lock_guard lock(g_mediaSourceMutex);
        g_preferredMediaSource.assign(aumid);
        g_dockOverrideAumid.clear();
        g_dockOverrideRev.fetch_add(1);
    }
    g_userSourceSwitchAt.store(NowSeconds());
    if (g_mediaRefreshEvent) {
        SetEvent(g_mediaRefreshEvent);
    }
    g_layoutDirty = true;
}

// Dock click. Animated when "Expanded player transitions" is on: the renderer slides
// the old content out first, then calls RequestMediaSource().
void RequestMediaSourceSwitch(const std::wstring& aumid) {
    int direction = 1;
    bool sameAsActive = false;
    {
        std::lock_guard lock(g_stateMutex);
        const int active = ActiveMediaSourceIndex(g_state);
        int target = -1;
        for (size_t i = 0; i < g_state.mediaSources.size(); ++i) {
            if (g_state.mediaSources[i].aumid == aumid) {
                target = static_cast<int>(i);
                break;
            }
        }
        sameAsActive = (target >= 0 && target == active);
        if (target >= 0 && active >= 0) {
            direction = target >= active ? 1 : -1;
        }
    }

    if (sameAsActive || !GetSettingsCopy().expandedMediaAnim) {
        RequestMediaSource(aumid);
        return;
    }
    {
        std::lock_guard lock(g_mediaSourceMutex);
        g_switchTarget = aumid;
    }
    g_switchDir.store(direction);
    g_switchSeq.fetch_add(1);
    g_layoutDirty = true;
}

// "Playing from %s" + "Brave" -> "Playing from Brave"
std::wstring FormatWithName(const wchar_t* format, const std::wstring& name) {
    std::wstring out = format;
    const size_t pos = out.find(L"%s");
    if (pos != std::wstring::npos) {
        out.replace(pos, 2, name);
    }
    return out;
}

// ── Dashboard tab loop ───────────────────────────────────────────────────────
// Single source of truth for how many tabs are in the mouse-wheel scroll loop.
// The first tab's meaning depends on context -- the media view when something is
// playing, the clock when idle -- but the *count* is identical, which is why the
// renderer, the scroll handler and the hit tests can all share this. That
// arithmetic used to be copy-pasted in seven places, which is exactly how they
// drifted out of sync.
int StudioExtraTabCount(const Settings& s);  // Bunbun: Agent, Clipboard, Note (studio_module.inc)
// Bunbun: every dashboard page. The tab order is user-set (Bunbun.PageOrder), so the
// helpers below look a page up in DashboardTabIds instead of counting positions.
using StudioPage = bb::studio::Page;  // Bunbun: studio_pages.h, shared with the Linux island
int DashboardPageIndex(StudioPage page, const Settings& s);  // studio_module.inc; -1 when the page is off
int DashboardPageIndexIn(int context, StudioPage page, const Settings& s);

int DashboardTabCount(const Settings& s);  // Bunbun: studio_module.inc

int ActiveTabCount(const Settings& settings) {
    // Bunbun: the same list the dashboard draws (DashboardTabIds), so scrolling can
    // reach every page (Studio's media view has Home too, and the order is user-set).
    return DashboardTabCount(settings);
}

int NormalizedTabIndex(const Settings& settings) {
    const int total = std::max(1, ActiveTabCount(settings));
    const int raw = g_idleTab.load(std::memory_order_relaxed);
    return ((raw % total) + total) % total;
}

// Tab order is always: primary, calendar, weather?, hardware?, file tray?
// The expensive GPU / network counters are only sampled while the hardware card
// is actually on screen, so that check needs the card's real index rather than
// assuming it is the last tab -- which stopped being true once the File Tray
// was added after it.
// With "Pin lyrics as first tab" on (and a Lyrics tab present), the media view's
// order is: lyrics, media, calendar, ... so the media tab and every tab after it
// sit one position later. Returns the media tab's index: 0 normally, 1 when pinned.
int MediaTabIndex(const Settings& settings) {
    // Bunbun: looked up, since Studio puts Home before the player.
    return std::max(0, DashboardPageIndexIn(0, StudioPage::Music, settings));
}

int HardwareMonitorTabIndex(const Settings& settings) {
    if (!settings.hardwareMonitorModule) {
        return -1;
    }
    return DashboardPageIndex(StudioPage::Hardware, settings);  // Bunbun: user-set order
}

// Bunbun: the Connectivity tab sits right after the Hardware Monitor.
int ConnectivityTabIndex(const Settings& settings) {
    if (!settings.connectivityModule) {
        return -1;
    }
    return DashboardPageIndex(StudioPage::Connectivity, settings);  // Bunbun: user-set order
}

int FileTrayTabIndex(const Settings& settings) {
    if (!settings.fileTrayModule) {
        return -1;
    }
    return DashboardPageIndex(StudioPage::Tray, settings);  // Bunbun: user-set order
}

// Moves the dashboard one tab (+1 = next, -1 = previous). Clamped at both ends:
// pushing past the first or last tab is ignored completely (no wrap, no bounce),
// so a free-spinning wheel or touchpad inertia can never make the edge tabs twitch.
void StepIdleTab(int direction) {
    const Settings settings = GetSettingsCopy();
    const int count = std::max(1, ActiveTabCount(settings));
    const int next = NormalizedTabIndex(settings) + direction;
    if (next < 0 || next >= count) {
        return;
    }
    g_idleTab = next;
    g_layoutDirty = true;
}

void StepActivityScroll(int direction, int totalActivities) {
    if (totalActivities <= 1) return;
    int cur = g_activityIndex.load(std::memory_order_relaxed);
    int next = cur + direction;
    if (next < 0) next = totalActivities - 1;
    else if (next >= totalActivities) next = 0;
    g_activityIndex.store(next, std::memory_order_relaxed);
    g_ringStepped.store(true, std::memory_order_relaxed);  // Bunbun
    g_layoutDirty = true;
}

