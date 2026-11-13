// Bunbun: Weather visuals, layout caches and small types the Renderer uses.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

// Groupings mirror GetWeatherIconAndText so both stay keyed off the same WWO
// weather codes.
WeatherVisual WeatherVisualFromCode(int code) {
    switch (code) {
        case 113:
            return WeatherVisual::Clear;
        case 116:
            return WeatherVisual::PartlyCloudy;
        case 119: case 122:
            return WeatherVisual::Cloudy;
        case 143: case 248: case 260:
            return WeatherVisual::Fog;
        case 200: case 386: case 389: case 392: case 395:
            return WeatherVisual::Storm;
        case 176: case 263: case 266: case 281: case 284: case 293: case 296:
        case 299: case 302: case 305: case 308: case 311: case 314: case 353:
        case 356: case 359:
            return WeatherVisual::Rain;
        case 179: case 182: case 185: case 227: case 230: case 317: case 320:
        case 323: case 326: case 329: case 332: case 335: case 338: case 350:
        case 362: case 365: case 368: case 371:
            return WeatherVisual::Snow;
        default:
            return WeatherVisual::Unknown;
    }
}

// Cache for DrawKaraokeLetterPop: the layout and per-cluster hit rects only
// depend on the active lyric line's text/format/box size, never on time, so
// they are rebuilt only when the line changes. Rects are stored relative to a
// (0,0) layout origin.
struct KaraokeLayoutCache {
    struct ClusterInfo {
        UINT32 textStart = 0;
        UINT32 textLength = 0;
        bool isWhitespace = true;
        D2D1_RECT_F rect{};
    };

    std::wstring text;
    IDWriteTextFormat* format = nullptr;
    float layoutWidth = 0.0f;
    float layoutHeight = 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    std::vector<ClusterInfo> clusters;
    bool valid = false;
};

struct MarqueeLayoutCache {
    std::wstring text;
    IDWriteTextFormat* format = nullptr;
    float wrapWidth = 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics{};
    double startedAt = -1.0;  // Bunbun: when this text first showed, so it starts at its first word
};

// Measured geometry of the collapsed idle strip, produced by
// Renderer::MeasureIdleStrip. The render loop uses totalWidth to size the
// island; DrawIdleDashboard uses the per-slot widths to place the clock, divider
// and weather reading inside it.
struct IdleStripMetrics {
    float clockWidth = 0.0f;
    float weatherWidth = 0.0f;
    bool hasWeather = false;
    bool hasPrivacy = false;
    float privacyReserve = 0.0f;  // Bunbun: width kept clear for the privacy dots
    float ringWidth = 0.0f;       // Bunbun: the status ring beside the clock (gap included)
    float totalWidth = IdleStripLayout::kMinWidth;
};

// One visible character of the song title, used by the title-change animation.
struct TitleGlyph {
    wchar_t ch0 = 0;       // first UTF-16 unit (used to match letters between titles)
    wchar_t ch1 = 0;       // second unit of a surrogate pair, otherwise 0
    float x = 0.0f;        // left edge inside the full title layout
    float sx = 0.0f;       // on-screen left edge (relative to the title rect) when the animation started
    bool  eligible = true; // visible when the animation started (only these get paired)
    float w = 0.0f;
    float h = 0.0f;
    float seed = 0.0f;     // stable 0..1 value so each letter's smoke is unique
    int match = -1;        // index of the paired glyph in the other title, or -1
    ComPtr<IDWriteTextLayout> layout;  // this character alone, same format as the title
};

// Marquee state of one title at the moment the animation starts, mirroring what
// DrawMarqueeText does (offset = fmod(now * speed, cycle)).
struct TitleScroll {
    bool scrolls = false;
    float width = 0.0f;
    float cycle = 0.0f;
    float offset0 = 0.0f;
};

// State for the gentle swap animation of the artist / album lines.
struct SwapTextState {
    std::wstring shown;               // text currently on screen
    std::wstring prev;                // text that is fading out
    double start = -1.0;              // animation start, -1 = idle
    double lastDrawn = -1.0;          // last frame this row was drawn
    MarqueeLayoutCache prevCache;     // layout cache for the outgoing text
};

struct MediaPillMetrics {
    float clockWidth = 0.0f;
    float sectionWidth = 0.0f;                       // clock + divider block on the left
    float totalWidth = MediaPillLayout::kBaseWidth;  // content space, before sizeScale
};

// Substitutes '0' for every decimal digit before measuring.
//
// Proportional faces give '1' a visibly narrower advance than '0', so measuring
// the live clock would change the pill's width as the time changed -- with
// seconds enabled that is a layered-window resize and reposition every second,
// and a visible twitch. Normalising to the widest digit makes the width stable
// for a given digit count, and since '0' is never narrower than the digit it
// replaces, the real string is guaranteed to fit the box we reserve.
std::wstring WidestDigitForm(std::wstring text) {
    for (wchar_t& c : text) {
        if (c >= L'0' && c <= L'9') {
            c = L'0';
        }
    }
    return text;
}

// ── Collapsed-pill lyrics ────────────────────────────────────────────────────
// Width of the collapsed media pill while it carries a lyric line (content px).
constexpr float kCollapsedLyricsWidth = 320.0f;
// Bunbun: the lyric pill fits the song: wide enough for its longest line, between
// 280 and 380 (content units). Set by DrawCollapsedLyrics, 0 until a song's lyrics
// are measured.
std::atomic<float> g_collapsedLyricsWidth{0.0f};
// Bunbun: narrower when the pill only carries the song title (no lyrics).
constexpr float kCollapsedTitleWidth = 260.0f;

// True when the collapsed pill should be widened for lyrics. "fetching" counts
// too, so the pill does not shrink and regrow on every track change.
// Bunbun: the mascot's mood while something plays: popcorn for a video, headphones for music.
bb::mascot::Mood MediaMascotMood(const MediaSnapshot& media, const Settings& settings) {
    return media.video && settings.streamingMood ? bb::mascot::Mood::Streaming : bb::mascot::Mood::Music;
}

// Bunbun: a video plays and the small island shows the mascot and "Streaming".
bool StreamingPill(const SharedState& state, const Settings& settings) {
    return settings.streamingMood && settings.media && state.media.available && state.media.video;
}

bool CollapsedLyricsActive(const SharedState& state, const Settings& settings) {
    // Stays wide for as long as media is available. If there are no lyrics to
    // show, DrawCollapsedLyrics shows the media title instead.
    // Bunbun: the song-title setting reuses the same text slot without lyrics.
    return ((settings.collapsedLyrics || settings.collapsedTitle) && settings.media && state.media.available) ||
           StreamingPill(state, settings);  // Bunbun: "Streaming" takes the title's place
}

// One pre-built lyric line. Built once when the line changes, then reused every
// frame, so animating it costs a DrawTextLayout and nothing else.
struct CollapsedLyricLine {
    std::wstring text;
    ComPtr<IDWriteTextLayout> layout;
    float width = 0.0f;
    float height = 0.0f;
    bool isTitle = false;  // true when this is the media-title fallback, not a lyric line
    float builtFor = 0.0f; // Bunbun: the slot width a lyric line was fitted to
};

// Bunbun: the accent the island shows now (album art, custom or system), in
// accent.txt next to settings.ini, so the settings app's colours match the island's
// (its "Dynamic" accent comes from the album art, which only the island sees).
// Written only when it changes: a track change, not every frame.
void PublishAccent(const D2D1_COLOR_F& c) {
    static unsigned last = 0xFFFFFFFFu;
    const auto ch = [](float v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    const unsigned rgb = (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
    if (rgb == last) return;
    last = rgb;
    std::wstring path = bb::SettingsPath();
    path.erase(path.find_last_of(L"\\/"));
    path += L"\\accent.txt";
    char hex[8];
    snprintf(hex, sizeof hex, "#%06X", rgb);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(f, hex, 7, &wrote, nullptr);
    CloseHandle(f);
}

