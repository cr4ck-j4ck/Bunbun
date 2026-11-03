// Bunbun — the island's mascot.
//
// Six original characters drawn entirely with Direct2D primitives (bunny "Bun",
// cat, dog, fox, panda, robot), or a custom skin pack: a folder of PNGs, one per
// mood, which gets the same bob / hop / squash motion. Crisp at any DPI, no
// assets shipped.
//
// Custom packs live in %APPDATA%\Bunbun\mascots\<name>\ with idle.png,
// working.png, thinking.png, alert.png, happy.png, sad.png, sleepy.png (any
// missing mood falls back to idle.png).
#pragma once

#include <d2d1.h>

#include <string>
#include <vector>

namespace bb::mascot {

enum class Mood {
    Idle,      // gentle breathing, blinks
    Working,   // ears sway, eyes glance at the ticker
    Thinking,  // eyes up, head tilt
    Alert,     // ears straight up and twitching: needs you
    Happy,     // hops with ^^ eyes: task finished
    Sad,       // droopy ears: error
    Sleepy,    // eyes closed, ears down
    Music,     // headphones on, bobbing to the beat
    Snack,     // munching a drumstick: do not disturb is on
    Listening, // ears up, leaning in: you're typing to Bun
    Talking,   // mouth moving, speech bubble: Bun is answering
    Confused,  // head tilt, uneven eyes, a swirl: Bun didn't understand
    Streaming, // watching a video, munching popcorn from a bucket
};

enum class Character { Bunny, Cat, Dog, Fox, Panda, Robot, Spiderman, Luffy, Dazai, Custom };

struct Style {
    Character character = Character::Bunny;
    std::wstring customPack;      // folder name under %APPDATA%\Bunbun\mascots (Character::Custom)
    bool hasColor = false;        // override the character's main colour
    D2D1_COLOR_F color{};
    float motion = 1.0f;          // 0 = still, 1 = default, 2 = lively
    float hop = 1.0f;             // hop height multiplier
    float blinkSeconds = 3.7f;    // 0 = never blink
    bool eyesFollow = true;       // gaze follows the cursor
    bool celebrate = true;        // hop on Happy
};

// Applied to every later Draw (thread-safe).
void SetStyle(const Style& style);
Style GetStyle();

// Parses the [Bunbun] settings values into a Style.
Style StyleFromSettings(const std::wstring& character, const std::wstring& colorHex, int motionPercent,
                        int hopPercent, int blinkTenths, bool eyesFollow, bool celebrate);

// Character ids as stored in settings ("bunny", ..., "custom:<pack>").
std::wstring CharacterId(const Style& style);
const wchar_t* CharacterName(Character c);

// Folder holding custom packs, and the packs found there.
std::wstring PacksFolder();
std::vector<std::wstring> ListCustomPacks();

// Draws the mascot centered at `center`, fitting a square of side `size`.
// `t` is the clock in seconds, `moodSince` when the mood began (drives one-off
// motions such as the hop). `look` is the gaze direction, each axis in -1..1.
void Draw(ID2D1RenderTarget* rt, D2D1_POINT_2F center, float size, Mood mood, double t, double moodSince,
          D2D1_POINT_2F look);

// Picks the center and size that make the current character's real outline
// (tall ears, antenna, hood, or just a round head) fill `box`. A plain square
// fit sized for Bun's ears leaves short-eared characters with a gap above them.
void FitToBox(D2D1_RECT_F box, D2D1_POINT_2F* center, float* size);

// How far the current character's art reaches left and right of its centre, in
// units of its size (0.5 = the edge of its square). A skin pack's effects (a
// question mark, sparks, a raised hand) can reach past the head; layouts leave
// room for them so the island's edge or the text beside it doesn't cut them.
void HorizontalReach(float* left, float* right);

// Same, with an explicit style (the settings app previews unsaved styles).
void DrawStyled(ID2D1RenderTarget* rt, const Style& style, D2D1_POINT_2F center, float size, Mood mood, double t,
                double moodSince, D2D1_POINT_2F look);

// True while the mood has motion that needs continuous frames (the hop, the
// alert twitch). Steady moods only need a few frames per second.
bool NeedsFastFrames(Mood mood, double t, double moodSince);

// Drops this thread's cached brush and skin-pack bitmaps. Call it after drawing
// to a short-lived render target, and before releasing a long-lived one (device
// loss, shutdown): the caches are keyed by target address and never free
// themselves at exit (see mascot.cpp).
void ReleaseThreadResources();

// Music mood: how hard the beat is (0..1, e.g. the audio level). Defaults to a
// steady groove when nothing better is known. Thread-safe.
void SetMusicEnergy(float energy);

// Battery: while charging, the built-in characters hold a little plug. Thread-safe.
void SetCharging(bool charging);

}  // namespace bb::mascot
