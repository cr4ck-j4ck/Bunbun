// Bunbun: Pixel buffers and the media, battery, timer and system snapshots.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

struct BitmapPixels {
    std::vector<uint8_t> bgra;
    UINT width = 0;
    UINT height = 0;
    uint64_t generation = 0;
    D2D1_COLOR_F sampledAccent = D2D1::ColorF(0x4cc9f0);
};

// Immutable, shared pixel data. Copying one is a refcount bump, so copying
// SharedState every frame no longer copies album art / icons.
// Null-safe: operator-> on an empty ref points at a static empty BitmapPixels.
class PixelsRef {
   public:
    PixelsRef() = default;
    explicit PixelsRef(BitmapPixels&& p)
        : p_(std::make_shared<const BitmapPixels>(std::move(p))) {}

    explicit operator bool() const { return p_ && !p_->bgra.empty(); }
    const BitmapPixels* operator->() const { return p_ ? p_.get() : &Empty(); }
    const BitmapPixels& operator*() const { return *operator->(); }

   private:
    static const BitmapPixels& Empty() {
        static const BitmapPixels e{};
        return e;
    }
    std::shared_ptr<const BitmapPixels> p_;
};

struct MediaSnapshot {
    bool available = false;
    bool playing = false;
    bool video = false;  // Bunbun: a video or film, not music (IsVideoMedia)
    std::wstring title;
    std::wstring artist;
    std::wstring albumTitle;
    std::wstring sourceAppUserModelId;
    std::wstring sourceName;
    std::wstring sourceBadge;
    PixelsRef art;
    PixelsRef sourceIcon;
    uint64_t artGeneration = 0;
    uint64_t sourceIconGeneration = 0;
    double artChangedAt = 0.0;
    double titleChangedAt = 0.0;
    int64_t positionTicks = 0;
    int64_t endTicks = 0;
    int64_t lastUpdatedTicks = 0;
};

// One SMTC media session the dock can switch to.
struct MediaSourceInfo {
    std::wstring aumid;
    std::wstring name;
    std::wstring badge;
    bool playing = false;
    PixelsRef icon;
};

struct ClipboardSnapshot {
    bool active = false;
    bool image = false;
    std::wstring text;
    std::wstring appName;
    PixelsRef appIcon;
    PixelsRef imagePreview;  // decoded thumbnail when the clipboard holds an image
    double expiresAt = 0.0;
};

struct BatterySnapshot {
    bool active = false;
    bool low = false;
    bool charging = false;      // plugged in
    bool hasBattery = false;    // Bunbun: a laptop / tablet (desktops report none)
    bool reallyCharging = false;  // Bunbun: plugged in and the battery is taking charge
    bool reminder = false;      // Bunbun: this alert is the periodic "still charging" one
    bool full = false;          // Bunbun: this alert says the battery just filled up
    int minutesToFull = -1;     // Bunbun: from the battery report, -1 when unknown
    int percent = 100;
    DWORD secondsRemaining = BATTERY_LIFE_UNKNOWN;
    double expiresAt = 0.0;
};

struct ProgressSnapshot {
    bool active = false;
    int percent = 0;
};

struct NotificationSnapshot {
    bool active = false;
    std::wstring app;
    std::wstring title;
    std::wstring body;
    PixelsRef icon;
    double expiresAt = 0.0;
    double shownAt = 0.0;  // Bunbun: when it appeared (its lifetime is expiresAt - shownAt)
};

struct VolumeSnapshot {
    bool active = false;
    int percent = 0;
    bool muted = false;
    std::wstring deviceName;
    double expiresAt = 0.0;
    float displayPercent = 0.0f;  // animated bar level, set only on the render loop's snapshot copy
};

struct BrightnessSnapshot {
    bool active = false;
    int percent = 0;
    double expiresAt = 0.0;
};

struct CapsLockSnapshot {
    bool active = false;
    bool capsOn = false;
    bool numOn = false;
    bool isNumEvent = false;
    double expiresAt = 0.0;
};

struct TimerSnapshot {
    bool active = false;          // a session exists (running or paused)
    bool running = false;         // currently counting down
    bool isBreak = false;         // work vs break session
    int totalSeconds = 0;
    double endsAt = 0.0;          // NowSeconds() at completion, while running
    double remainingAtPause = 0.0;
    bool justFinished = false;
    double finishedExpiresAt = 0.0;
    int kind = 0;                 // Bunbun: kTimerKindTimer or kTimerKindPomodoro (timers_module.inc)
    std::wstring label;           // Bunbun: what the pill calls it ("Focus 1 of 4", "Timer"); empty = default
    std::wstring doneLabel;       // Bunbun: what it says when it ends ("Focus done · Take a break")
};

// Bunbun: the Timers page's kinds of timer on g_state.timer.
constexpr int kTimerKindTimer = 0;     // a countdown (the Timer mode, Ask Bun's timers)
constexpr int kTimerKindPomodoro = 1;  // a Pomodoro focus session or break

// Bunbun: the Timers page's stopwatch (timers_module.inc).
struct StopwatchSnapshot {
    bool active = false;          // started and not reset
    bool running = false;
    double startedAt = 0.0;       // NowSeconds() when it last started running
    double accumulated = 0.0;     // seconds counted before that
};

enum class DeviceEventType {
    Connected,
    Disconnected,
};

struct DeviceSnapshot {
    bool active = false;
    DeviceEventType eventType = DeviceEventType::Connected;
    std::wstring deviceName;  // e.g. "USB Drive" or "Bluetooth Device"
    bool isBluetoothLike = false;
    double expiresAt = 0.0;
};

struct BluetoothDeviceSnapshot {
    bool active = false;
    bool connected = true;   // true = just connected, false = just disconnected
    std::wstring deviceName;
    int batteryPercent = -1; // -1 = unknown/unavailable
    BluetoothDeviceCategory category = BluetoothDeviceCategory::Generic;
    double expiresAt = 0.0;
};

struct SystemSnapshot {
    int volumePercent = 0;
    bool volumeMuted = false;
    int cpuPercent = 0;
    int memoryPercent = 0;
    float memoryUsedGB = 0.0f;
    float memoryTotalGB = 0.0f;
    int diskFreePercent = 0;
    int renderFps = 0;
    int gpuPercent = -1;
    float netUpMbps = 0.0f;
    float netDownMbps = 0.0f;
    bool netFresh = false;       // Bunbun: false until a real 1 s window has been measured
    bool charging = false;
    bool micActive = false;      // orange dot: microphone in use
    bool cameraActive = false;   // green dot: camera in use
    bool screenActive = false;   // Bunbun: blue dot, an app is capturing the screen
    std::wstring screenApp;
    std::wstring foregroundTitle;
    std::wstring micApp;
    std::wstring cameraApp;
    std::wstring audioDeviceName; // Bunbun: default audio output endpoint name
};

