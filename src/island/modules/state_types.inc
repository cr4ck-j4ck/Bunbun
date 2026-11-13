// Bunbun: Connectivity, weather, lyrics, Quick Lookup and agent state types.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

// Bunbun: Wi-Fi / Ethernet / Bluetooth status for the Connectivity tab.
// Do Not Disturb comes from g_isDnDActive, which is already event-driven.
struct ConnectivitySnapshot {
    bool sampled = false;
    int wifiRadio = -1;          // -1 no adapter, 0 off, 1 on
    bool wifiConnected = false;
    std::wstring wifiName;       // empty when Windows withholds it (location access off)
    int wifiSignal = 0;          // 0-100
    bool ethernetConnected = false;
    int bluetoothRadio = -1;     // -1 no radio, 0 off, 1 on
    std::vector<std::wstring> bluetoothDevices;
    // Bunbun: the laptop / tablet battery. Desktops report none and get no card.
    bool hasBattery = false;
    int batteryPercent = 0;
    bool onPower = false;        // plugged in
    bool charging = false;       // actually charging (plugged in can also mean "held at a limit")
    bool batterySaver = false;
    DWORD batterySecondsLeft = BATTERY_LIFE_UNKNOWN;
    int chargeRateMw = 0;        // from the battery report; 0 when unknown
    int remainingMwh = 0;
    int fullMwh = 0;             // today's full charge
    int designMwh = 0;           // when new
    // Bunbun: connected Bluetooth device & battery (for Studio System page)
    std::wstring connectedBtName;
    int connectedBtBattery = -1;  // 0-100 or -1 if unknown
    // Long-press lists, only filled while one is on screen.
    std::vector<WifiNetwork> wifiNetworks;
    std::vector<BluetoothListDevice> bluetoothList;
};

struct Activity {
    IslandKind kind = IslandKind::Idle;
    float width = 120.0f;
    float height = 36.0f;
};

struct WeatherHourlyPoint {
    std::wstring timeLabel; // "Now", "10 AM", etc.
    float temp = 0.0f;
    int weatherCode = 0;
    bool isDay = true;
};

struct WeatherDailyPoint {
    std::wstring dayName; // "Today", "Sat", etc.
    float minTemp = 0.0f;
    float maxTemp = 0.0f;
    int weatherCode = 0;
};

struct WeatherSnapshot {
    bool hasData = false;
    float temperature = 0.0f;
    int weatherCode = 0;
    std::wstring city;
    std::wstring weatherDesc;
    std::wstring windSpeed;
    std::wstring windDir;
    std::wstring humidity;
    std::wstring feelsLike;
    double lastUpdated = 0.0;
    // Bunbun Studio Weather extensions:
    float todayHigh = 0.0f;
    float todayLow = 0.0f;
    float uvIndex = 0.0f;
    std::wstring uvDesc;       // e.g. "2 Low"
    std::wstring sunrise;      // e.g. "6:39"
    std::wstring sunset;       // e.g. "5:40"
    std::vector<WeatherHourlyPoint> hourly; // 6 points
    std::vector<WeatherDailyPoint> daily;   // 5 days
};

struct DoNotDisturbSnapshot {
    bool active = false;
    bool enabled = false;
    double expiresAt = 0.0;
};

// One file parked on the island's shelf (#33). Kept deliberately small: the
// tray holds references, never copies of the files themselves.
struct FileTrayItem {
    std::wstring path;
    std::wstring name;
    uint64_t sizeBytes = 0;
    bool isDirectory = false;
    bool isText = false;     // a pasted text snippet rather than a file reference
    std::wstring text;       // full snippet when isText is set
    PixelsRef icon;
};

struct LyricsLine {
    int64_t timeMs = -1;  // -1 for unsynced (plain) lines
    std::wstring text;
};

// Immutable, shared lyric lines. Same read API as the vector it replaces, so
// the drawing code (empty / size / operator[]) is unchanged.
class LyricsLines {
   public:
    LyricsLines() = default;
    explicit LyricsLines(std::vector<LyricsLine>&& v)
        : p_(std::make_shared<const std::vector<LyricsLine>>(std::move(v))) {}

    bool empty() const { return !p_ || p_->empty(); }
    size_t size() const { return p_ ? p_->size() : 0; }
    const LyricsLine& operator[](size_t i) const { return (*p_)[i]; }
    const std::vector<LyricsLine>& vec() const {
        static const std::vector<LyricsLine> none;
        return p_ ? *p_ : none;
    }

   private:
    std::shared_ptr<const std::vector<LyricsLine>> p_;
};

struct LyricsSnapshot {
    bool hasData = false;
    bool synced = false;
    bool notFound = false;
    bool instrumental = false;  // Bunbun: with notFound; a source says the track has no vocals
    bool fetching = false;
    std::wstring matchedTitle;
    std::wstring matchedArtist;
    LyricsLines lines;
};

// ── Quick Lookup state ───────────────────────────────────────────────────────
enum class LookupStatus {
    Search,    // panel open: search box + recent list
    Loading,   // request running
    Found,     // title / subtitle / body (/ example) are filled in
    NoResult,  // nothing found, or the network failed
    NoText,    // legacy, no longer produced
};

struct LookupSnapshot {
    bool active = false;
    LookupStatus status = LookupStatus::Loading;
    std::wstring query;      // the normalised text being looked up
    std::wstring title;      // headword / article title
    std::wstring subtitle;   // "phonetic · part of speech", or the Wikipedia short description
    std::wstring body;       // definition / first sentences of the extract
    std::wstring example;    // dictionary example sentence, if any
    std::wstring source;     // "Dictionary" / "Wikipedia"
    double expiresAt = 0.0;
    bool talking = false;    // Bunbun: the local AI is still writing this answer (mascot talks)
    bool confused = false;   // Bunbun: Bun didn't understand, or couldn't get an answer (mascot is puzzled)
};

// Shared by Renderer::MeasureLookupCard / DrawLookup and ActivityForKind so the
// size the island animates to and the layout painted inside it cannot drift.
// One finished lookup, kept in memory only (never written to disk).
struct LookupRecent {
    std::wstring query;   // what was searched (normalised)
    std::wstring title;   // headword / article title that came back
    std::wstring detail;  // one-line definition preview
    std::wstring source;
};

namespace LookupLayout {
    constexpr float kWidth = 380.0f;
    constexpr float kPadX = 16.0f;
    constexpr float kPadTop = 12.0f;
    constexpr float kPadBottom = 12.0f;
    constexpr float kTitleH = 20.0f;
    constexpr float kSubtitleH = 15.0f;
    constexpr float kCardGap = 6.0f;
    constexpr float kCardPad = 9.0f;
    constexpr float kExampleGap = 5.0f;
    constexpr float kSimpleHeight = 54.0f;
    constexpr float kMaxHeight = 300.0f;
    constexpr int kMaxBodyLines = 4;
    constexpr int kMaxExampleLines = 2;

    // Search panel
    constexpr float kFieldTop = kPadTop;
    constexpr float kFieldH = 36.0f;
    constexpr float kFieldGap = 10.0f;
    constexpr float kBelowField = kFieldTop + kFieldH + kFieldGap;  // where content under the box starts
    constexpr float kSectionH = 18.0f;   // "Recent ... Clear" header band
    constexpr float kRowH = 32.0f;
    constexpr float kRowGap = 3.0f;
    constexpr float kStatusH = 30.0f;    // one-line states under the box
    constexpr int kMaxRows = 4;
    constexpr double kCaretPeriod = 1.0;
    constexpr double kCaretOn = 0.55;

    constexpr float RowsTop(bool header) { return kBelowField + (header ? kSectionH : 0.0f); }
    constexpr float SearchHeight(int rows, bool header) {
        return rows <= 0 ? kBelowField + kStatusH + kPadBottom
                         : RowsTop(header) + rows * kRowH + (rows - 1) * kRowGap + kPadBottom;
    }
    constexpr float StatusHeight() { return kBelowField + kStatusH + kPadBottom; }
}

// Input state of the search box. Render thread only (the window procedure and the
// render loop share that thread), so it needs no lock.
struct LookupUiState {
    bool open = false;
    std::wstring text;
    size_t caret = 0;
    bool selectAll = false;
    int selected = 0;               // highlighted row
    double caretResetAt = 0.0;      // blink restarts on every edit
    std::wstring clipboardQuery;    // clipboard suggestion captured when the panel opened
    HWND prevForeground = nullptr;  // window to hand focus back to
    int historyPos = -1;            // Bunbun: Up / Down through earlier queries; -1 = typing
};
LookupUiState g_lookupUi;

// ── Bunbun: agent pill / approval card ──────────────────────────────────────
// Shared by ActivityForKind, Renderer::DrawAgent and HandleAgentClick so the
// size, the painting and the hit-testing cannot drift apart.
namespace AgentLayout {
    constexpr float kCompactWidth = 330.0f;
    constexpr float kCompactHeight = 46.0f;
    constexpr float kCardWidth = 400.0f;
    constexpr float kCardHeight = 140.0f;
    constexpr float kPadX = 14.0f;
    constexpr float kPadTop = 12.0f;
    constexpr float kMascot = 44.0f;     // card mascot box
    constexpr float kButtonH = 30.0f;
    constexpr float kButtonGap = 8.0f;
    constexpr float kPadBottom = 12.0f;

    inline bool IsCard(bb::AgentPhase p) {
        return p == bb::AgentPhase::Approval || p == bb::AgentPhase::Question;
    }
}

// Buttons painted on the agent card, in content coordinates (the same space
// MediaContentFromClient returns). Render thread only: DrawAgent writes them and
// OverlayWndProc, which runs on that thread, reads them.
struct AgentButton {
    D2D1_RECT_F rect{};
    bb::AgentDecision decision = bb::AgentDecision::Terminal;
    int answerIndex = -1;
    int action = 0;  // Bunbun: 0 = decide; else an AgentAction (studio_module.inc)
    int arg = 0;
};
std::vector<AgentButton> g_agentButtons;
uint64_t g_agentButtonsRequest = 0;
int g_agentHoveredButton = -1;

// Bunbun: the settings app's live preview asks the island for a snapshot of
// what it shows. g_frameMutex keeps a snapshot from reading a half-drawn frame.
class Renderer;
std::mutex g_frameMutex;
Renderer* g_snapshotRenderer = nullptr;  // guarded by g_frameMutex
// Bunbun: Separate displays. The second renderer that draws the other displays'
// islands at rest, what they show (the island's last resting activity, in content
// units, without the notch's extra width) and this frame's state. Render thread only.
Renderer* g_restRenderer = nullptr;
struct SharedState;
struct RestLook { bool valid = false; int kind = 0; float width = 0, height = 0, bunSlot = 0; };
RestLook g_restLook;
const SharedState* g_restState = nullptr;

// Bunbun: width of the mascot slot at the left of the collapsed island (idle
// strip and music pill), 0 when hidden. Set by the render loop, read by
// Renderer::Render; both on the render thread.
float g_bunSlotWidth = 0.0f;
constexpr float kBunSlotContent = 40.0f;   // content px, times sizeScale
constexpr float kNotchExtraContent = 36.0f; // notch islands read better a little wider

