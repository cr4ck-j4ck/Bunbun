// Bunbun: The Settings struct.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

struct Settings {
    Position position = Position::TopCenter;
    int targetMonitor = 0;
    int offsetX = 0;
    int offsetY = 0;
    bool separateExpandedOffsetY = false;  // #83
    int offsetYExpanded = 0;               // #83
    float sizeScale = 1.0f;
    float baseSizeScale = 1.0f;
    std::wstring fontFamily;
    AccentMode accentMode = AccentMode::Auto;
    D2D1_COLOR_F customAccent = D2D1::ColorF(0x4cc9f0);
    int targetFps = 0; // 0 = Auto
    AnimationStyle animationStyle = AnimationStyle::Default;
    ProgressStyle progressStyle = ProgressStyle::Slim;
    SpectrumStyle spectrumStyle = SpectrumStyle::Bars;
    bool expandedMediaAnim = false;   // hover player: text/cover flip/controls transitions
    bool pillCoverAnim = false;      // collapsed pill: cover spin/drop
    float animationSpeed = 1.0f;
    int powerSaver = 0;  // Bunbun: 0 auto (on battery / Windows battery saver), 1 always, 2 never
    bool media = true;
    bool mediaAutoExpand = false;
    bool clipboard = true;
    bool statusCountdownProgress = false;
    bool battery = true;
    bool progress = true;
    bool volume = true;
    bool brightness = true;
    CalendarAccentMode calendarAccent = CalendarAccentMode::Accent;
    bool privacyDots = true;
    bool privacyDotsMic = true;
    bool privacyDotsCam = true;
    bool privacyDotsPulse = true;
    // Modules.CapsLock -- option to turn off Caps Lock / Num Lock indicators.
    //
    // Enforced in three places, because any one of them alone leaks:
    //   ChooseActivities        - the pill is never selected for display
    //   WM_APP_CAPSLOCK handler - no state is recorded, no nudge is triggered
    //   CapsLockHookWanted      - WH_KEYBOARD_LL is not installed at all
    // The activity gate is the one that actually hides the pill; the others stop
    // the work leading up to it.
    bool capsLock = true;
    bool timerEnabled = true;
    bool hideShowHotkeyEnabled = true;
    UINT hideShowModifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    UINT hideShowVk = 'D';

    // Quick Lookup: hotkey-triggered dictionary / Wikipedia card.
    bool quickLookup = false;
    UINT lookupModifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    UINT lookupVk = VK_SPACE;
    bool bluetoothIndicator = true;
    bool bluetoothShowBattery = true;
    D2D1_COLOR_F privacyDotsMicHex = D2D1::ColorF(1.0f, 0.584f, 0.0f, 1.0f); // #FF9500
    D2D1_COLOR_F privacyDotsCamHex = D2D1::ColorF(0.133f, 0.776f, 0.239f, 1.0f); // #10B981
    float tintOpacity = 0.72f;
    float pillOpacity = 0.96f;
    bool gameOverlay = false;
    bool showMetricText = true;
    bool weather = true;
    std::wstring weatherCity;
    bool weatherFahrenheit = false;
    bool lyrics = false;
    bool lyricsVisualizer = true;
    bool karaokeLyrics = true;
    bool karaokeLetterGlow = true;
    bool lyricsNeighborLinesVisible = false;
    int lyricsOffsetMs = 0;      // Modules.LyricsOffsetMs, speakers / wired, ms
    int lyricsOffsetBtMs = 0;    // Modules.LyricsOffsetBluetoothMs, Bluetooth output, ms
    bool collapsedLyrics = false;  // lyrics inside the collapsed media pill (Bunbun.PillLyrics)
    bool agentPill = true;         // Bunbun: the working-agent pill (Bunbun.AgentPill); cards always show
    bool lyricsPinnedFirst = false;  // Lyrics tab is the first tab of the expanded media view (right-click toggle)
    bool lyricsCache = true;       // Modules.LyricsCache
    int lyricsCacheMaxMB = 25;     // Modules.LyricsCacheMaxMB; 0 = disk cache off
    bool lyricsNetEase = true;     // Bunbun: Modules.LyricsNetEase, fallback when LRCLIB has nothing
    bool mediaTabLyrics = false;   // lyrics side panel in the expanded media tab
    int autoHideIdleSeconds = 0;
    bool autoHideFullscreen = true;
    bool fullscreenTuck = false;  // Bunbun: tuck to a line in full screen instead of hiding
    std::wstring fullscreenShowApps, fullscreenTuckApps, fullscreenHideApps;  // Bunbun: per-app full screen
    bool borderMergedMode = false;
    bool unhideOnHover = true;
    bool alwaysOnTop = true;
    bool expandOnHover = true;
    bool autoDpiScale = true;
    bool matchScreenSize = false;   // Bunbun: Appearance.MatchScreenSize (MonitorScale)
    bool w11Style = false;
    bool notchStyle = false;
    // Color customization
    ThemePreset themePreset = ThemePreset::Obsidian;
    D2D1_COLOR_F pillBgColor = D2D1::ColorF(0.031f, 0.031f, 0.039f, 1.0f); // #08080A
    D2D1_COLOR_F textPrimaryColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f); // #FFFFFF
    D2D1_COLOR_F textSecondaryColor = D2D1::ColorF(0.608f, 0.608f, 0.647f, 1.0f); // #9B9BA5
    ContourBorderMode contourBorderMode = ContourBorderMode::Default;
    bool contourBorderEnabled = true;
    D2D1_COLOR_F contourBorderColor = D2D1::ColorF(0.200f, 0.200f, 0.220f, 1.0f); // #333338
    bool clockAccentGlow = true;
    bool privacyDotsEnabled = true;
    bool privacyDotPulsing = true;
    D2D1_COLOR_F micDotColor = D2D1::ColorF(1.0f, 0.584f, 0.0f, 1.0f); // #FF9500
    D2D1_COLOR_F camDotColor = D2D1::ColorF(0.204f, 0.780f, 0.349f, 1.0f); // #34C759
    bool hardwareMonitorModule = true;
    bool connectivityModule = true;  // Bunbun: Wi-Fi / Bluetooth / DND tab
    bool todoModule = true;          // Bunbun: To-do tab and reminders
    bool localAi = false;            // Bunbun: Ask Bun's local AI (bunbun-brain.exe)
    std::wstring aiBackend, ollamaUrl, ollamaModel, geminiKey, geminiModel;  // Bunbun: local | ollama | gemini
    std::wstring localAiModel;       // Bunbun: a .gguf file; empty = the first in %LOCALAPPDATA%\Bunbun\models
    int localAiIdleMinutes = 5;      // Bunbun: close the helper (free its memory) after this long idle
    bool reminderSound = true;       // Bunbun: play Windows' reminder sound when one is due
    bool quietReminders = true;      // Bunbun: silent, small pill in full screen / presenting / meetings / focus
    std::wstring calendarIcsUrl;     // Bunbun: private iCal link(s) for the next meeting
    int meetingAlertMinutes = 10;    // Bunbun: meeting pill shows this long before; 0 = never
    bool downloadsModule = true;     // Bunbun: browser download pill
    bool batteryMoods = true;        // Bunbun: mascot plug / drowsy / cheer
    int chargingReminderMinutes = 15;  // Bunbun: "still charging" pill interval; 0 = off
    bool privacyDotsScreen = true;   // Bunbun: dot while the screen is being captured
    D2D1_COLOR_F privacyDotsScreenHex = D2D1::ColorF(0.31f, 0.55f, 1.0f, 1.0f);
    bool doNotDisturbIndicator = true;
    bool notificationRespectDnD = true;

    // ── Premium material / redesign ──────────────────────────────────────────
    bool materialDepth = true;      // downward depth shading + accent bloom
    bool dropShadow = true;         // soft shadow under the island
    float accentBloom = 1.0f;       // 0..2 multiplier on the accent wash
    float textScale = 1.0f;         // independent typography scale (#41)
    BackdropMaterial backdropMaterial = BackdropMaterial::None;  // #59
    float backdropTint = 0.55f;     // acrylic tint strength, 0..1
    float backdropFillAlpha = 0.45f;  // how opaque the pill's own fill stays

    // ── Clock / date presentation (#61) ──────────────────────────────────────
    bool showSeconds = false;
    bool use24HourClock = false;    // false = follow system locale
    bool clockFollowSystem = true;
    std::wstring dateFormat;        // empty = locale default
    bool mediaPillClock = false;    // show the clock inside the collapsed media pill
    bool collapsedTitle = true;     // Bunbun: song title inside the collapsed media pill
    bool streamingMood = true;      // Bunbun: videos show the Streaming mascot, not title and cover
    bool liveMood = true;           // Bunbun: the Live mood while the screen is shared or recorded
    int afkMinutes = 3;             // Bunbun: no input this long = AFK (snack); 0 = off
    int sleepMinutes = 10;          // Bunbun: no input (or no island touch) this long = asleep; 0 = never
    bool dndFollowSystem = false;   // Bunbun: Windows' Do Not Disturb puts the mascot in DND too
    int tuckRevealMs = 450;         // Bunbun: pointing at the tucked line this long brings the island back
    std::wstring musicApps, videoApps;  // Bunbun: overrides for the music / video guess
    bool dateFirst = false;
    int firstDayOfWeek = -1;        // 0 = Sunday .. 6 = Saturday, -1 = follow Windows

    // ── Localization (#35) ───────────────────────────────────────────────────
    std::wstring language = L"auto";

    // ── File tray (#33) ──────────────────────────────────────────────────────
    bool fileTrayModule = false;
    int fileTrayMaxItems = 10;
    bool fileTrayPersist = true;   // Modules.FileTrayPersist

    // ── Media auto-expand exclusions (#62) ───────────────────────────────────
    std::vector<std::wstring> mediaExpandBlocklist;

    // ── Game overlay options (#25) ───────────────────────────────────────────
    bool gameOverlayShowFps = true;
    bool gameOverlayShowCpu = true;
    bool gameOverlayShowGpu = true;
    bool gameOverlayShowRam = true;
    bool gameOverlayShowDisk = true;
    bool gameOverlayCompact = false;

    // ── Bunbun ───────────────────────────────────────────────────────────────
    bool bunbunAgents = true;
    bool bunbunMascot = true;
    int bunbunAmbientFps = 30;  // continuous content when nothing is resizing
    bool bunbunAssistant = true;  // the Quick Lookup box asks Bun
    bool bunbunMascotOnIsland = true;  // Bun beside the clock / music pill
    bool mascotDnd = false;            // Bunbun: mascot do-not-disturb (snack), separate from Windows DND
    // Bunbun: Studio layout and its pages (studio_module.inc)
    bool studioLayout = true;
    bool tuckHotkey = true;
    bool agentTab = true;
    bool clipboardHistory = true;
    bool quickNote = true;
    bool pokeReactions = true;
    bool musicSwipe = true;
    bool idleEyes = false;  // Bunbun: eyes instead of the mascot beside the clock
    bool idleShowClock = true;    // Bunbun: Bunbun.IdleStripItems
    std::wstring idleName;        // Bunbun: "name": this text in the time's place (Bunbun.IdleName)
    int statusRing = 1;           // Bunbun: Bunbun.StatusRing: 0 off, 1 auto, 2 water, 3 tasks, 4 focus
    int edgeLight = 2;            // Bunbun: Bunbun.EdgeLight: 0 off, 1 comet, 2 twin comets, 3 breathe
    unsigned edgeLightOn = 0;     // Bunbun: Bunbun.EdgeLightOn, EdgeLight::kOn* bits (what plays it)
    int edgeLightColor = 0;       // Bunbun: Bunbun.EdgeLightColor: 0 default (white), 1 dynamic, 2 custom
    D2D1_COLOR_F edgeLightCustom = D2D1::ColorF(0.353f, 0.784f, 0.98f, 1.0f);  // Bunbun: Bunbun.EdgeLightCustomColor
    bool focusPage = true;        // Bunbun: Focus page and guard (studio_module.inc)
    bool screenTime = true;       // Bunbun: Screen Time page
    int screenTimeLimit = 0;      // Bunbun: Bunbun.ScreenTimeLimit, hours a day (0 = none)
    bool appsPage = true;         // Bunbun: All apps page
    int homeTile[3] = {-1, -1, -1};  // Bunbun: Home slots, -1 = automatic (HomeTile order)
    bool bluetoothLowWarning = true;  // Bunbun: heads-up at 20% for connected BT devices
    bool sounds = true;           // Bunbun: chimes (sounds_module.inc)
    bool mirrorPage = false;      // Bunbun: camera preview page
    bool waterPage = true;        // Bunbun: Water page (Bunbun.WaterPage)
    bb::water::Settings water;    // Bunbun: the water reminder (Bunbun.Water*)
    int useCase = 0;              // Bunbun: Bunbun.UseCase (bb::work::UseCase: agents, work, both)
    bb::work::Schedule workSchedule;  // Bunbun: Work.* (work_module.inc)
    bool workReminders = true;
    bool studyPomodoro = true;    // Bunbun: Study.Pomodoro (a Pomodoro focus session counts as study)
    bool soundOpenClose = false;
    int soundVolume = 40;
    std::wstring homeLinks;       // Bunbun: "Name|address; ..." under the Home tiles
    std::wstring pageOrder;       // Bunbun: Bunbun.PageOrder, page keys in tab order (DashboardTabIds)
    bool focusGuard = false;
    int pomoFocus = 25, pomoShort = 5, pomoLong = 15, pomoCycles = 4;  // Bunbun: Bunbun.Pomodoro*
    bool pomoAutoStart = true;     // Bunbun: Bunbun.PomodoroAutoStart
    std::wstring timerPresets;     // Bunbun: Bunbun.TimerPresets
    std::wstring focusGuardApps;
    bool idleShowWeather = true;
    int eyeColor = 0;             // Bunbun: Bunbun.EyeColor, index into kEyeColors
    bool eyeStatusColors = false; // Bunbun: eyes follow the agents' status
    bool usageLimits = true;      // Bunbun: Claude Code usage ring and bars
    bool antigravityQuota = true; // Bunbun: Antigravity model quota (AgQuota)
    // Bunbun: which agents the island shows (AgentKind order) and which one leads.
    bool showAgent[6] = {true, true, true, true, true, true};
    int mainAgent = -1;           // -1 = auto, else an AgentKind
    // Mirrors on other displays (see Mirrors)
    bool displaysAll = false;
    bool mirrorPositionSame = true;
    Position mirrorPosition = Position::TopCenter;
    std::wstring displayPositions;  // "2=bottom-center;3=top-left"
    bool displaysSeparate = false;  // Bunbun: Appearance.DisplayLink = separate (else linked)
    std::wstring displayShapes, displaySizes;  // Bunbun: Appearance.DisplayShapes / DisplaySizes ("2=pill;3=notch", "2=1.2")
    bool baseNotch = false, baseW11 = false;     // Bunbun: Appearance.ShapeStyle as set (the shape may follow the display)
    Position basePosition = Position::TopCenter;  // Bunbun: Appearance.Position as set (position may follow the display)
};

