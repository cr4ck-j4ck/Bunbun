// Bunbun — skin packs from character sheets, and eye tracking for pack art.
//
// A character sheet is one image with a row per mood and several frames per row
// (a label box on the left and arrows between frames are fine). SliceSheet finds
// the rows and frames, cuts each frame out of its background (transparent, or a
// flat colour) and places it on a shared canvas, so every frame of a pack lines
// up and plays as an animation.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb::mascot {

// Straight (not premultiplied) 8-bit RGBA, rows top to bottom.
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w * h * 4
    uint8_t* at(int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
};

// Decodes anything Windows Imaging can read (PNG, JPEG, WebP, BMP…). COM must be initialised.
bool DecodeImage(const void* data, size_t size, Image* out);
bool DecodeImageFile(const std::wstring& path, Image* out);
bool EncodePng(const Image& img, const std::wstring& path);
bool EncodePngBytes(const Image& img, std::string* bytes);

// Every frame SliceSheet returns is this size, the character's head centred and
// the bottom of its row at the bottom edge.
constexpr int kPackCanvasW = 288;
constexpr int kPackCanvasH = 192;

struct SheetRow {
    std::vector<Image> frames;  // left to right
};
std::vector<SheetRow> SliceSheet(const Image& sheet);

// Writes `frames` as <mood>_1.png, <mood>_2.png… in the pack's folder (created if
// needed), removing that mood's old frames. `mood` is a file stem: "idle", "happy"…
bool WriteMoodFrames(const std::wstring& packDir, const std::wstring& mood, const std::vector<Image>& frames,
                     std::wstring* err);

// ── Eye tracking ────────────────────────────────────────────────────────────
// For a front-facing frame: finds both eyes (white with a dark iris), and splits
// the frame into a version with the irises painted over in the eye's white, the
// irises alone, and where they may show (the eye whites). Drawing the irises
// shifted toward the cursor, clipped to the whites, makes the eyes follow it.
struct EyeRig {
    bool found = false;
    Image base;    // the frame, irises painted out
    Image irises;  // only the iris pixels
    Image mask;    // opaque where an iris may show
    float maxDx = 0, maxDy = 0;  // how far the irises may move, in frame pixels
};
EyeRig FindEyes(const Image& frame);

}  // namespace bb::mascot
