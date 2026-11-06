// Bunbun — mascot characters (see mascot.h). Original artwork, drawn in code.
#include "mascot.h"
#include "pack_sheet.h"

#include <d2d1_1.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace bb::mascot {
namespace {

constexpr float kPi = 3.14159265f;

std::mutex g_styleMutex;
Style g_style;
std::atomic<float> g_musicEnergy{0.6f};
std::atomic<bool> g_charging{false};

D2D1_COLOR_F Hex(UINT32 rgb, float a = 1.0f) { return D2D1::ColorF(rgb, a); }

D2D1_COLOR_F Shade(D2D1_COLOR_F c, float k) {  // k < 1 darkens
    return D2D1::ColorF(std::clamp(c.r * k, 0.0f, 1.0f), std::clamp(c.g * k, 0.0f, 1.0f),
                        std::clamp(c.b * k, 0.0f, 1.0f), c.a);
}

enum class Ears { Long, Pointy, Floppy, Round, Antenna, None };

struct Look {
    Ears ears = Ears::Long;
    D2D1_COLOR_F fur, inner, eye, nose, blush;
    D2D1_COLOR_F earColor{};        // outer ear (defaults to fur; zero alpha = unset)
    bool muzzle = false;            // lighter lower face (dog, fox)
    D2D1_COLOR_F muzzleColor{};
    bool patches = false;           // panda eye patches
    bool whiskers = false;
    bool robot = false;
    bool spiderman = false;
    bool luffy = false;
    bool dazai = false;
};

Look LookFor(const Style& s) {
    Look l;
    l.eye = Hex(0x2A2230);
    l.nose = Hex(0xE8708F);
    l.blush = Hex(0xF59AB0, 0.45f);
    switch (s.character) {
        case Character::Cat:
            l.ears = Ears::Pointy;
            l.fur = Hex(0xCFC6BD);
            l.inner = Hex(0xF2A9BA);
            l.whiskers = true;
            break;
        case Character::Dog:
            l.ears = Ears::Floppy;
            l.fur = Hex(0xDDAE74);
            l.earColor = Hex(0x8C5B3E);
            l.inner = l.earColor;
            l.muzzle = true;
            l.muzzleColor = Hex(0xF5E3C8);
            l.nose = Hex(0x2B2124);
            break;
        case Character::Fox:
            l.ears = Ears::Pointy;
            l.fur = Hex(0xF08A3C);
            l.inner = Hex(0xFFE9D2);
            l.muzzle = true;
            l.muzzleColor = Hex(0xFFF4E8);
            l.nose = Hex(0x2B2124);
            l.blush = Hex(0xFFB38A, 0.35f);
            break;
        case Character::Panda:
            l.ears = Ears::Round;
            l.fur = Hex(0xF8F7F4);
            l.earColor = Hex(0x26252A);
            l.inner = l.earColor;
            l.patches = true;
            l.nose = Hex(0x26252A);
            break;
        case Character::Robot:
            l.ears = Ears::Antenna;
            l.fur = Hex(0xC9D3DF);
            l.inner = Hex(0x55E6FF);       // LEDs
            l.eye = Hex(0x55E6FF);
            l.nose = Hex(0x55E6FF);
            l.blush = Hex(0xFF7AA8, 0.30f);
            l.robot = true;
            break;
        case Character::Spiderman:
            l.ears = Ears::None;
            l.fur = Hex(0xDF1F2D);         // iconic Spidey scarlet red
            l.eye = Hex(0xFFFFFF);         // white mask lens
            l.inner = Hex(0x1B1B22);       // black web lines
            l.spiderman = true;
            break;
        case Character::Luffy:
            l.ears = Ears::None;
            l.fur = Hex(0xF6C79E);         // skin; LuffyHead draws the face itself
            l.eye = Hex(0x15121A);
            l.luffy = true;
            break;
        case Character::Dazai:
            l.ears = Ears::None;
            l.fur = Hex(0xF9DCC2);         // skin; DazaiHead draws the face itself
            l.eye = Hex(0x2B1E1A);
            l.dazai = true;
            break;
        default:  // Bunny
            l.ears = Ears::Long;
            l.fur = Hex(0xF7F1E8);
            l.inner = Hex(0xF4A7BB);
            break;
    }
    if (s.hasColor) l.fur = s.color;
    if (l.earColor.a == 0.0f) l.earColor = l.fur;
    return l;
}

// One solid brush recoloured per shape keeps resource churn at zero.
struct BrushCache {
    ID2D1RenderTarget* owner = nullptr;
    ComPtr<ID2D1SolidColorBrush> brush;
    ID2D1SolidColorBrush* Get(ID2D1RenderTarget* rt, D2D1_COLOR_F c) {
        if (owner != rt || !brush) {
            brush.Reset();
            owner = rt;
            rt->CreateSolidColorBrush(c, &brush);
        }
        brush->SetColor(c);
        return brush.Get();
    }
};
// Per-thread caches, deliberately never destroyed. A thread_local destructor
// runs during thread or process exit under the loader lock, and releasing a
// D2D resource there tears down the D3D device, which deadlocks against GPU
// driver threads that are exiting too (Quit Bunbun used to hang forever).
// Owners release them while their target is alive: ReleaseThreadResources().
BrushCache& Brushes() {
    thread_local BrushCache* cache = new BrushCache;
    return *cache;
}

float Smooth(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }

// Working: a look direction that reads along a line, a little low (at the
// screen or the page), then flicks back to the start of the next one.
D2D1_POINT_2F ReadingLook(double t) {
    const float ph = std::fmod(static_cast<float>(t), 2.2f) / 2.2f;
    return D2D1::Point2F(ph < 0.82f ? -0.9f + 1.8f * Smooth(ph / 0.82f) : 0.9f - 1.8f * Smooth((ph - 0.82f) / 0.18f), 0.35f);
}

// 0 = open, 1 = closed: a quick blink every `period` seconds.
// How open a talking mouth is, 0..1: quick syllables in phrases, with a short
// pause between phrases so it reads as speech rather than chewing.
float TalkOpen(double t) {
    const float tp = static_cast<float>(t);
    const float phrase = std::fmod(tp, 1.9f) < 1.5f ? 1.0f : 0.15f;
    return phrase * std::fabs(std::sin(tp * 9.0f)) * (0.55f + 0.45f * std::fabs(std::sin(tp * 2.3f)));
}

float BlinkAmount(double t, float period) {
    if (period <= 0.0f) return 0.0f;
    const double phase = std::fmod(t + 1.3, period);
    if (phase > 0.16) return 0.0f;
    return std::sin(static_cast<float>(phase / 0.16) * kPi);
}

void EyeLine(ID2D1RenderTarget* rt, ID2D1Brush* brush, D2D1_POINT_2F c, float r, float curve, float stroke) {
    const D2D1_POINT_2F m = D2D1::Point2F(c.x, c.y - curve);
    rt->DrawLine(D2D1::Point2F(c.x - r, c.y), m, brush, stroke);
    rt->DrawLine(m, D2D1::Point2F(c.x + r, c.y), brush, stroke);
}

void Triangle(ID2D1RenderTarget* rt, ID2D1Brush* brush, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c) {
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(&f);
    ComPtr<ID2D1PathGeometry> g;
    if (!f || FAILED(f->CreatePathGeometry(&g))) return;
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(&sink);
    sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(b);
    sink->AddLine(c);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(g.Get(), brush);
}

// ── Custom skin packs ───────────────────────────────────────────────────────

const wchar_t* MoodFile(Mood m) {
    switch (m) {
        case Mood::Working: return L"working.png";
        case Mood::Thinking: return L"thinking.png";
        case Mood::Alert: return L"alert.png";
        case Mood::Happy: return L"happy.png";
        case Mood::Sad: return L"sad.png";
        case Mood::Sleepy: return L"sleepy.png";
        case Mood::Music: return L"music.png";
        case Mood::Snack: return L"snack.png";
        case Mood::Listening: return L"listening.png";
        case Mood::Talking: return L"talking.png";
        case Mood::Confused: return L"confused.png";
        case Mood::Streaming: return L"streaming.png";
        case Mood::Dnd: return L"dnd.png";
        case Mood::Live: return L"live.png";
        case Mood::Drinking: return L"drinking.png";
        default: return L"idle.png";
    }
}

// Packs made before a mood existed: the closest older image stands in.
const wchar_t* FallbackFile(Mood m) {
    switch (m) {
        case Mood::Talking: return L"happy.png";
        case Mood::Confused: return L"thinking.png";
        case Mood::Streaming: return L"snack.png";  // a pack without popcorn frames still munches
        case Mood::Dnd: return L"sleepy.png";       // eyes closed, settled in
        case Mood::Live: return L"working.png";     // at the laptop
        case Mood::Drinking: return L"snack.png";   // sipping instead of munching
        default: return L"idle.png";
    }
}

// Decoded once per (render target, file); D2D bitmaps belong to their target.
// A pack image, the part of it that's actually drawn on (packs come with any
// amount of empty margin), and for front-facing frames an eye rig: the frame
// with its irises painted out, the irises alone, and where they may show.
struct PackImage {
    bool loaded = false;
    ComPtr<ID2D1Bitmap> bitmap;
    std::vector<ComPtr<ID2D1Bitmap>> mips;  // half, quarter… size: see DrawImageHQ
    D2D1_RECT_F visible{};
    D2D1_SIZE_F size{};
    ComPtr<ID2D1Bitmap> base, irises, mask;  // set when the eyes were found
    float maxDx = 0, maxDy = 0;
};

// One step of a mood's animation: a frame number (1-based) and how many frame
// lengths it stays up.
struct FrameStep {
    int frame = 1;
    float length = 1.0f;
};

// What a pack folder holds. Old packs have one <mood>.png each, trimmed to
// their visible part; packs made from a character sheet have <mood>_1.png,
// <mood>_2.png… on one shared canvas, played in turn, and can have look
// frames (look_left.png…) the character turns to toward the cursor.
// A face rig (pack.ini [Rig]): an eyeless face picture the eyes and brows are
// drawn on live, in the face picture's pixels. See DrawRig.
struct RigEye {
    D2D1_POINT_2F c{};
    float rx = 0, ry = 0;
};
struct RigBrow {
    D2D1_POINT_2F innerTop{}, outer{};  // the square end by the nose, the tapered tip
    float innerBottom = 0;
};
struct RigInfo {
    bool on = false;
    std::wstring face;                 // file name
    std::vector<std::wstring> moods;   // mood stems drawn with the rig
    RigEye eyes[2];                    // left one first
    RigBrow brows[2];
    bool hasBrows = false;
    float iris = 12, pupil = 6, converge = 4;
    D2D1_COLOR_F ink = Hex(0x150E0B), irisColor = Hex(0x4A2E1F);
};

struct PackInfo {
    bool frames = false;
    float frameSeconds = 0.4f;
    float fadeSeconds = 0.12f;            // cross-fade between frames and moods
    std::map<std::wstring, std::vector<FrameStep>> sequences;  // mood file stem → its steps
    std::vector<std::wstring> eyeFrames;  // files that get an eye rig
    bool look = false;                    // look_left.png and look_right.png exist
    D2D1_RECT_F ref{};                    // idle's first frame's visible part: the pack's scale
    RigInfo rig;
};

struct PackCache {
    ID2D1RenderTarget* owner = nullptr;
    unsigned generation = 0;
    std::map<std::wstring, PackImage> images;
    std::map<std::wstring, PackInfo> packs;
    ComPtr<ID2D1Layer> layer, fadeLayer;
    ComPtr<ID2D1DeviceContext> dc;  // for high-quality scaling, when the target has one
    bool dcChecked = false;
    ComPtr<ID2D1RadialGradientBrush> iris;
};
std::atomic<unsigned> g_packGeneration{0};  // bumped by SetStyle: pick up edited packs
PackCache& Packs(ID2D1RenderTarget* rt) {
    thread_local PackCache* cache = new PackCache;  // see Brushes()
    const unsigned gen = g_packGeneration.load();
    if (cache->owner != rt || cache->generation != gen) {
        cache->images.clear();
        cache->packs.clear();
        cache->layer.Reset();
        cache->fadeLayer.Reset();
        cache->dc.Reset();
        cache->dcChecked = false;
        cache->iris.Reset();
        cache->owner = rt;
        cache->generation = gen;
    }
    return *cache;
}

std::wstring Stem(Mood m) {
    std::wstring f = MoodFile(m);
    return f.substr(0, f.size() - 4);  // without ".png"
}

bool FileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

ComPtr<ID2D1Bitmap> ToBitmap(ID2D1RenderTarget* rt, const Image& img) {
    std::vector<BYTE> bgra(img.px.size());
    for (size_t i = 0; i < img.px.size(); i += 4) {
        const unsigned a = img.px[i + 3];
        bgra[i] = static_cast<BYTE>(img.px[i + 2] * a / 255);
        bgra[i + 1] = static_cast<BYTE>(img.px[i + 1] * a / 255);
        bgra[i + 2] = static_cast<BYTE>(img.px[i] * a / 255);
        bgra[i + 3] = static_cast<BYTE>(a);
    }
    ComPtr<ID2D1Bitmap> bmp;
    rt->CreateBitmap(D2D1::SizeU(img.w, img.h), bgra.data(), img.w * 4,
                     D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bmp);
    return bmp;
}

// Half, quarter… size copies, each a 2x2 average of the one before (in
// premultiplied colour, so edges don't darken). Drawing a 300-pixel frame as a
// 40-pixel mascot straight from the full bitmap samples only a few of its
// pixels, which looks both jagged and soft; a level near the target size doesn't.
std::vector<ComPtr<ID2D1Bitmap>> MipChain(ID2D1RenderTarget* rt, const Image& img) {
    std::vector<ComPtr<ID2D1Bitmap>> out;
    int w = img.w, h = img.h;
    std::vector<float> cur(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < cur.size(); i += 4) {
        const float a = img.px[i + 3] / 255.0f;
        cur[i] = img.px[i + 2] * a;  // B
        cur[i + 1] = img.px[i + 1] * a;
        cur[i + 2] = img.px[i] * a;
        cur[i + 3] = img.px[i + 3];
    }
    while (w >= 24 && h >= 24) {
        const int nw = (w + 1) / 2, nh = (h + 1) / 2;
        std::vector<float> next(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                float sum[4] = {};
                int n = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = 2 * x + dx, sy = 2 * y + dy;
                        if (sx >= w || sy >= h) continue;
                        const float* p = &cur[(static_cast<size_t>(sy) * w + sx) * 4];
                        for (int k = 0; k < 4; ++k) sum[k] += p[k];
                        ++n;
                    }
                for (int k = 0; k < 4; ++k) next[(static_cast<size_t>(y) * nw + x) * 4 + k] = sum[k] / n;
            }
        std::vector<BYTE> bytes(next.size());
        for (size_t i = 0; i < next.size(); ++i) bytes[i] = static_cast<BYTE>(std::clamp(next[i] + 0.5f, 0.0f, 255.0f));
        ComPtr<ID2D1Bitmap> bmp;
        rt->CreateBitmap(D2D1::SizeU(nw, nh), bytes.data(), nw * 4,
                         D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bmp);
        if (!bmp) break;
        out.push_back(bmp);
        cur.swap(next);
        w = nw;
        h = nh;
    }
    return out;
}

PackImage* LoadPackImage(ID2D1RenderTarget* rt, PackCache& cache, const std::wstring& path, bool eyes) {
    PackImage& im = cache.images[path];
    if (!im.loaded) {
        im.loaded = true;
        Image img;
        if (DecodeImageFile(path, &img) && img.w <= 4096 && img.h <= 4096) {
            im.bitmap = ToBitmap(rt, img);
            im.mips = MipChain(rt, img);
            im.size =D2D1::SizeF(static_cast<float>(img.w), static_cast<float>(img.h));
            int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
            for (int y = 0; y < img.h; ++y)
                for (int x = 0; x < img.w; ++x)
                    if (img.at(x, y)[3] > 8) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
            im.visible = x1 < 0 ? D2D1::RectF(0, 0, im.size.width, im.size.height)
                                : D2D1::RectF(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 + 1), static_cast<float>(y1 + 1));
            if (eyes) {
                const EyeRig rig = FindEyes(img);
                if (rig.found) {
                    im.base = ToBitmap(rt, rig.base);
                    im.irises = ToBitmap(rt, rig.irises);
                    im.mask = ToBitmap(rt, rig.mask);
                    im.maxDx = rig.maxDx;
                    im.maxDy = rig.maxDy;
                }
            }
        }
    }
    return im.bitmap ? &im : nullptr;
}

// "1*4, 3, 2*2": frame 1 for four frame lengths, then 3, then 2 for two.
std::vector<FrameStep> ParseSteps(const std::wstring& text, int count) {
    std::vector<FrameStep> steps;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L',', pos);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring item = text.substr(pos, end - pos);
        FrameStep st;
        st.frame = _wtoi(item.c_str());
        if (const size_t star = item.find(L'*'); star != std::wstring::npos) st.length = static_cast<float>(_wtof(item.c_str() + star + 1));
        if (st.frame >= 1 && st.frame <= count && st.length > 0.0f && st.length <= 100.0f) steps.push_back(st);
        pos = end + 1;
    }
    return steps;
}

const PackInfo& LoadPackInfo(ID2D1RenderTarget* rt, PackCache& cache, const std::wstring& pack) {
    auto it = cache.packs.find(pack);
    if (it != cache.packs.end()) return it->second;
    PackInfo& info = cache.packs[pack];
    const std::wstring dir = PacksFolder() + L"\\" + pack + L"\\";
    const std::wstring ini = dir + L"pack.ini";
    auto read = [&](const wchar_t* section, const wchar_t* key) {
        wchar_t buf[512] = {};
        GetPrivateProfileStringW(section, key, L"", buf, 512, ini.c_str());
        return std::wstring(buf);
    };
    if (const std::wstring fs = read(L"Pack", L"FrameSeconds"); !fs.empty())
        info.frameSeconds = std::clamp(static_cast<float>(_wtof(fs.c_str())), 0.05f, 5.0f);
    info.frames = FileExists(dir + L"idle_1.png");
    if (info.frames) {
        for (int m = 0; m <= static_cast<int>(Mood::Live); ++m) {  // every mood, the newest last
            const std::wstring stem = Stem(static_cast<Mood>(m));
            int count = 0;
            while (count < 32 && FileExists(dir + stem + L"_" + std::to_wstring(count + 1) + L".png")) ++count;
            if (!count) continue;
            std::vector<FrameStep> steps = ParseSteps(read(L"Frames", stem.c_str()), count);
            if (steps.empty()) {
                // Idle rests on its first frame and plays the others now and then.
                if (stem == L"idle") steps.push_back({1, count > 1 ? 10.0f : 1.0f});
                for (int i = stem == L"idle" ? 2 : 1; i <= count; ++i) steps.push_back({i, 1.0f});
            }
            info.sequences[stem] = std::move(steps);
        }
        info.look = FileExists(dir + L"look_left.png") && FileExists(dir + L"look_right.png");
    }
    if (const std::wstring fade = read(L"Pack", L"FadeSeconds"); !fade.empty())
        info.fadeSeconds = std::clamp(static_cast<float>(_wtof(fade.c_str())), 0.0f, 1.0f);
    // A face rig: eyes and brows drawn live over an eyeless face (see DrawRig).
    if (const std::wstring face = read(L"Rig", L"Face"); info.frames && !face.empty() && FileExists(dir + face)) {
        RigInfo& rig = info.rig;
        auto nums = [&](const wchar_t* key) {
            std::vector<float> v;
            const std::wstring text = read(L"Rig", key);
            for (size_t pos = 0; pos < text.size();) {
                size_t end = text.find(L',', pos);
                if (end == std::wstring::npos) end = text.size();
                v.push_back(static_cast<float>(_wtof(text.substr(pos, end - pos).c_str())));
                pos = end + 1;
            }
            return v;
        };
        auto color = [&](const wchar_t* key, D2D1_COLOR_F* out) {
            const std::wstring text = read(L"Rig", key);
            if (text.size() == 7 && text[0] == L'#') *out = Hex(static_cast<UINT32>(wcstoul(text.c_str() + 1, nullptr, 16)));
        };
        const std::vector<float> l = nums(L"LeftEye"), r = nums(L"RightEye");
        if (l.size() == 4 && r.size() == 4 && l[2] > 1 && l[3] > 1 && r[2] > 1 && r[3] > 1) {
            rig.on = true;
            rig.face = face;
            rig.eyes[0] = {D2D1::Point2F(l[0], l[1]), l[2], l[3]};
            rig.eyes[1] = {D2D1::Point2F(r[0], r[1]), r[2], r[3]};
            const std::vector<float> lb = nums(L"LeftBrow"), rb = nums(L"RightBrow");
            if (lb.size() == 5 && rb.size() == 5) {
                rig.hasBrows = true;
                rig.brows[0] = {D2D1::Point2F(lb[0], lb[1]), D2D1::Point2F(lb[3], lb[4]), lb[2]};
                rig.brows[1] = {D2D1::Point2F(rb[0], rb[1]), D2D1::Point2F(rb[3], rb[4]), rb[2]};
            }
            const float eyeR = std::min({l[2], l[3], r[2], r[3]});
            if (const auto v = nums(L"Iris"); v.size() == 1) rig.iris = std::clamp(v[0], 1.0f, eyeR);
            if (const auto v = nums(L"Pupil"); v.size() == 1) rig.pupil = std::clamp(v[0], 0.5f, rig.iris);
            if (const auto v = nums(L"Converge"); v.size() == 1) rig.converge = std::clamp(v[0], 0.0f, eyeR);
            color(L"Ink", &rig.ink);
            color(L"IrisColor", &rig.irisColor);
            std::wstring moods = read(L"Rig", L"Moods");
            if (moods.empty()) moods = L"idle";
            for (size_t pos = 0; pos < moods.size();) {
                size_t end = moods.find(L',', pos);
                if (end == std::wstring::npos) end = moods.size();
                std::wstring m = moods.substr(pos, end - pos);
                m.erase(0, m.find_first_not_of(L' '));
                m.erase(m.find_last_not_of(L' ') + 1);
                if (!m.empty()) rig.moods.push_back(m);
                pos = end + 1;
            }
        }
    }
    // Eye tracking: front-facing frames whose irises can follow the cursor. Packs
    // from a sheet get it on idle's resting frame unless pack.ini says otherwise.
    std::wstring eyes = read(L"Eyes", L"Frames");
    if (eyes.empty() && info.frames && read(L"Eyes", L"Tracking") != L"0") eyes = L"idle_1";
    for (size_t pos = 0; pos < eyes.size();) {
        size_t end = eyes.find(L',', pos);
        if (end == std::wstring::npos) end = eyes.size();
        std::wstring name = eyes.substr(pos, end - pos);
        name.erase(0, name.find_first_not_of(L' '));
        name.erase(name.find_last_not_of(L' ') + 1);
        if (!name.empty()) info.eyeFrames.push_back(name.find(L'.') == std::wstring::npos ? name + L".png" : name);
        pos = end + 1;
    }
    if (PackImage* ref = LoadPackImage(rt, cache, dir + (info.frames ? L"idle_1.png" : L"idle.png"), false)) info.ref = ref->visible;
    return info;
}

// Draws `src` of a pack image into `dst` from the mip level nearest the size it
// lands at on screen, with high-quality cubic filtering where the target offers
// it. Pack art is drawn far smaller than it's made, and plain linear filtering
// at that ratio is what made packs look blurry and jagged.
void DrawImageHQ(ID2D1RenderTarget* rt, PackCache& cache, const PackImage& im, D2D1_RECT_F dst, D2D1_RECT_F src,
                 float opacity) {
    if (!cache.dcChecked) {
        cache.dcChecked = true;
        rt->QueryInterface(IID_PPV_ARGS(&cache.dc));
    }
    D2D1_MATRIX_3X2_F m;
    rt->GetTransform(&m);
    float dpiX = 96.0f, dpiY = 96.0f;
    rt->GetDpi(&dpiX, &dpiY);
    const float srcW = std::max(1.0f, src.right - src.left);
    // Device pixels per source pixel.
    const float scale = (dst.right - dst.left) / srcW * std::sqrt(m._11 * m._11 + m._12 * m._12) * dpiX / 96.0f;
    ID2D1Bitmap* bmp = im.bitmap.Get();
    float k = 1.0f;
    for (size_t level = 0; level < im.mips.size() && scale * k * 2.0f <= 1.0f; ++level) {
        bmp = im.mips[level].Get();
        k *= 2.0f;
    }
    const D2D1_RECT_F s = D2D1::RectF(src.left / k, src.top / k, src.right / k, src.bottom / k);
    if (cache.dc)
        cache.dc->DrawBitmap(bmp, &dst, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &s, nullptr);
    else
        rt->DrawBitmap(bmp, dst, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, s);
}

// Draws a pack image into `dst` (showing `src` of it). With an eye rig, the
// irises are drawn shifted toward `look`, only where the eye whites are.
void DrawPackImage(ID2D1RenderTarget* rt, PackCache& cache, const PackImage& im, D2D1_RECT_F dst, D2D1_RECT_F src,
                   D2D1_POINT_2F look, bool track, float opacity = 1.0f) {
    if (!track || !im.base || !im.irises || !im.mask) {
        DrawImageHQ(rt, cache, im, dst, src, opacity);
        return;
    }
    const float k = (dst.right - dst.left) / std::max(1.0f, src.right - src.left);
    const float dx = std::clamp(look.x, -1.0f, 1.0f) * im.maxDx * k, dy = std::clamp(look.y, -1.0f, 1.0f) * im.maxDy * k;
    ComPtr<ID2D1BitmapBrush> maskBrush;
    if (!cache.layer) rt->CreateLayer(&cache.layer);
    if (!cache.layer || FAILED(rt->CreateBitmapBrush(im.mask.Get(), &maskBrush))) {
        rt->DrawBitmap(im.bitmap.Get(), dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
        return;
    }
    rt->DrawBitmap(im.base.Get(), dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
    maskBrush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    maskBrush->SetTransform(D2D1::Matrix3x2F::Translation(-src.left, -src.top) * D2D1::Matrix3x2F::Scale(k, k) *
                            D2D1::Matrix3x2F::Translation(dst.left, dst.top));
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                        1.0f, maskBrush.Get()),
                  cache.layer.Get());
    rt->DrawBitmap(im.irises.Get(), D2D1::RectF(dst.left + dx, dst.top + dy, dst.right + dx, dst.bottom + dy), 1.0f,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
    rt->PopLayer();
}

struct Motion {
    float bob = 0, tilt = 0, squash = 1;
};

Motion BodyMotion(const Style& st, Mood mood, double t, float since, float s) {
    Motion m;
    const float k = st.motion;
    m.bob = std::sin(static_cast<float>(t) * 2.2f) * s * 0.018f * k;
    switch (mood) {
        case Mood::Working: m.bob = std::sin(static_cast<float>(t) * 5.0f) * s * 0.025f * k; break;
        case Mood::Thinking: m.tilt = (-8.0f + std::sin(static_cast<float>(t) * 1.5f) * 2.0f) * std::min(k, 1.5f); break;
        case Mood::Alert:
            if (st.character == Character::Spiderman) {
                // Spidey-Sense tingling shudder
                m.bob = -std::fabs(std::sin(static_cast<float>(t) * 9.0f)) * s * 0.04f * k;
                m.tilt = std::sin(static_cast<float>(t) * 22.0f) * 2.8f * k;
            } else {
                // "Hey!": two quick hops with a side wiggle, then a short pause, so it
                // reads as a call for attention rather than a constant jitter.
                const float cycle = std::fmod(static_cast<float>(t), 1.3f);
                if (cycle < 0.5f) {
                    const float hop = std::fabs(std::sin(cycle / 0.5f * kPi * 2.0f));
                    m.bob = -hop * s * 0.06f * k;
                    m.squash = 1.0f + 0.05f * (1.0f - hop) * k;
                    m.tilt = std::sin(cycle / 0.5f * kPi * 2.0f) * 7.0f * k;
                } else {
                    m.bob = -std::sin(static_cast<float>(t) * 3.0f) * s * 0.008f * k;
                }
            }
            break;
        case Mood::Happy:
            if (st.celebrate && since < 2.0f) {
                if (st.character == Character::Spiderman) {
                    // Spider-Man web-swing arc: smooth pendulum swing + slight tilt
                    const float swingPhase = std::fmod(since, 0.95f) / 0.95f;
                    m.bob = -std::sin(swingPhase * kPi) * s * 0.28f * st.hop;
                    m.tilt = std::cos(swingPhase * kPi * 2.0f) * 16.0f * st.hop;
                    m.squash = 1.0f + 0.08f * std::sin(swingPhase * kPi * 2.0f);
                } else {
                    const float hop = std::fmod(since, 0.62f) / 0.62f;
                    m.bob = -std::sin(hop * kPi) * s * 0.20f * st.hop;
                    m.squash = 1.0f - 0.10f * std::max(0.0f, 1.0f - hop * 6.0f) * std::min(st.hop, 1.5f);
                }
            }
            break;
        case Mood::Sleepy: m.bob = std::sin(static_cast<float>(t) * 1.1f) * s * 0.02f * k; break;
        case Mood::Listening:
            // Leaning in toward what you type, with a small attentive nod now and then.
            m.tilt = (7.0f + std::sin(static_cast<float>(t) * 3.0f) * 1.5f) * std::min(k, 1.5f);
            m.bob = -std::fabs(std::sin(static_cast<float>(t) * 1.4f)) * s * 0.012f * k;
            break;
        case Mood::Talking: {
            // A little bounce on each "syllable" and a slow sway, like chatting.
            const float syl = std::fabs(std::sin(static_cast<float>(t) * 9.0f));
            m.bob = -syl * s * 0.012f * k;
            m.squash = 1.0f + 0.02f * syl * k;
            m.tilt = std::sin(static_cast<float>(t) * 1.7f) * 4.0f * k;
            break;
        }
        case Mood::Confused: {
            // Head cocked to the side, with a quick double "huh?" shake every 2.4 s.
            m.tilt = -13.0f * std::min(k, 1.5f);
            const float huh = std::fmod(since, 2.4f);
            if (huh < 0.5f) m.tilt += std::sin(huh * 25.0f) * 5.0f * (1.0f - huh / 0.5f) * k;
            break;
        }
        case Mood::Sad: {
            // Slumped a little low, breathing in slow sighs, with a small "no"
            // head shake every 3 s so the error reads even at a glance.
            const float sigh = 0.5f + 0.5f * std::sin(static_cast<float>(t) * 1.6f);
            m.bob = s * (0.025f + 0.012f * sigh) * k;
            m.squash = 1.0f - 0.03f * sigh * k;
            const float shake = std::fmod(static_cast<float>(t), 3.0f);
            if (shake < 0.6f) m.tilt = std::sin(shake * 26.0f) * 6.0f * (1.0f - shake / 0.6f) * k;
            break;
        }
        case Mood::Snack: {
            // Content chewing: a quick nod per bite and a little squash.
            const float chew = std::fabs(std::sin(static_cast<float>(t) * 7.0f));
            m.bob = chew * s * 0.015f * k;
            m.squash = 1.0f - 0.03f * chew * k;
            m.tilt = std::sin(static_cast<float>(t) * 1.3f) * 4.0f * k;
            break;
        }
        case Mood::Music: {
            // Nodding on the beat (~120 bpm) and swaying every other beat;
            // louder music, bigger nods.
            const float e = 0.45f + 0.55f * g_musicEnergy.load(std::memory_order_relaxed);
            const float beat = std::fabs(std::sin(static_cast<float>(t) * kPi * 2.0f));
            m.bob = -beat * s * 0.05f * k * e;
            m.tilt = std::sin(static_cast<float>(t) * kPi) * 8.0f * k * e;
            m.squash = 1.0f - 0.04f * (1.0f - beat) * e;
            break;
        }
        default: break;
    }
    return m;
}

// ── Face rig ────────────────────────────────────────────────────────────────
// Live eyes and brows over an eyeless face picture. The gaze glides toward the
// cursor on a spring and, when the cursor rests, wanders in small glances; the
// upper lids follow the gaze down; blinks come every few seconds, sometimes
// twice; the brows lift when the eyes look up. Everything is vector, in the face
// picture's pixels, so it stays sharp at any size.

ComPtr<ID2D1PathGeometry> NewPath(ID2D1RenderTarget* rt, ComPtr<ID2D1GeometrySink>* sink);

float Hash01(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (n & 0xFFFFFF) / 16777215.0f;
}

// How closed the lids are from blinking (0 open, 1 shut). One blink per period
// at a varying moment in it; about one in five is a double blink.
float RigBlink(double t, float period) {
    if (period <= 0.0f) return 0.0f;
    float shut = 0.0f;
    const long long n = static_cast<long long>(std::floor(t / period));
    for (long long i = n - 1; i <= n; ++i) {
        const uint32_t key = static_cast<uint32_t>(i * 2654435761LL);
        const double start = i * static_cast<double>(period) + Hash01(key) * period * 0.55;
        const bool twice = Hash01(key ^ 0x9E3779B9u) < 0.22f;
        for (int b = 0; b < (twice ? 2 : 1); ++b) {
            const float dt = static_cast<float>(t - (start + b * 0.3));
            float c = 0.0f;
            if (dt >= 0.0f && dt < 0.07f) c = Smooth(dt / 0.07f);
            else if (dt >= 0.07f && dt < 0.11f) c = 1.0f;
            else if (dt >= 0.11f && dt < 0.25f) c = 1.0f - Smooth((dt - 0.11f) / 0.14f);
            shut = std::max(shut, c);
        }
    }
    return shut;
}

// Per mascot on screen (keyed by where it's drawn): the gaze spring, and the
// mood it showed last for the cross-fade.
struct RigState {
    double lastT = -1.0;
    D2D1_POINT_2F gaze{}, vel{}, lastLook{};
    double stillSince = 0.0;
    std::wstring mood, prevMood;
    double moodAt = -10.0, moodStart = 0.0, prevStart = 0.0;
};

RigState& RigStateFor(D2D1_POINT_2F c, float s, double t) {
    thread_local auto* states = new std::map<long long, RigState>;  // see Brushes()
    const long long key = (static_cast<long long>(std::lround(c.x / 6.0f)) << 32) ^
                          (static_cast<long long>(std::lround(c.y / 6.0f)) << 12) ^ std::lround(s);
    for (auto it = states->begin(); it != states->end();)  // mascots that went away
        it = t - it->second.lastT > 10.0 && it->second.lastT >= 0.0 ? states->erase(it) : std::next(it);
    return (*states)[key];
}

// Read by NeedsFastFrames: a rig wants full frame rate while the gaze moves or a blink plays.
std::atomic<double> g_rigDrawnAt{-100.0};
std::atomic<double> g_rigSettleUntil{-100.0};
std::atomic<float> g_rigBlinkPeriod{0.0f};

void UpdateGaze(RigState& rs, D2D1_POINT_2F look, double t) {
    look = D2D1::Point2F(std::clamp(look.x, -1.0f, 1.0f), std::clamp(look.y, -1.0f, 1.0f));
    if (std::fabs(look.x - rs.lastLook.x) > 0.03f || std::fabs(look.y - rs.lastLook.y) > 0.03f) {
        rs.lastLook = look;
        rs.stillSince = t;
    }
    D2D1_POINT_2F target = look;
    // Left alone, the eyes don't stare: every couple of seconds they may glance
    // somewhere nearby and come back.
    const float idle = Smooth(static_cast<float>(t - rs.stillSince - 2.5) / 0.6f);
    if (idle > 0.0f) {
        const uint32_t n = static_cast<uint32_t>(static_cast<long long>(std::floor(t / 1.9 + 0.37)) * 2246822519LL);
        if (Hash01(n) > 0.5f) {
            target.x += (Hash01(n * 3 + 1) * 2.0f - 1.0f) * 0.55f * idle;
            target.y += (Hash01(n * 7 + 2) * 2.0f - 1.0f) * 0.35f * idle;
        }
    }
    // Tiny fixational jitter, so even a steady gaze isn't frozen.
    const uint32_t m = static_cast<uint32_t>(static_cast<long long>(std::floor(t / 0.7)) * 3266489917LL);
    target.x = std::clamp(target.x + (Hash01(m) * 2.0f - 1.0f) * 0.035f, -1.0f, 1.0f);
    target.y = std::clamp(target.y + (Hash01(m + 11) * 2.0f - 1.0f) * 0.035f, -1.0f, 1.0f);

    if (rs.lastT < 0.0 || t - rs.lastT > 2.0) {
        rs.gaze = target;
        rs.vel = {};
    } else {
        // A critically damped spring, stepped finely so it's the same at any frame rate.
        const float w = 24.0f;
        float left = static_cast<float>(std::min(t - rs.lastT, 0.25));
        while (left > 0.0f) {
            const float h = std::min(left, 1.0f / 240.0f);
            rs.vel.x += (w * w * (target.x - rs.gaze.x) - 2.0f * w * rs.vel.x) * h;
            rs.vel.y += (w * w * (target.y - rs.gaze.y) - 2.0f * w * rs.vel.y) * h;
            rs.gaze.x += rs.vel.x * h;
            rs.gaze.y += rs.vel.y * h;
            left -= h;
        }
    }
    rs.lastT = t;
    if (std::fabs(target.x - rs.gaze.x) + std::fabs(target.y - rs.gaze.y) > 0.01f ||
        std::fabs(rs.vel.x) + std::fabs(rs.vel.y) > 0.05f)
        g_rigSettleUntil.store(t + 0.15, std::memory_order_relaxed);
}

// The eye's outline as two curves between its corners (their Bézier handles'
// height): the upper lid, which drops from the top of the eye (open = 1) to a
// shallow closed arc (0), and the lower lid, which rises to meet it only at the
// very end, so a shut eye is the drawings' gentle "︶" rather than a deep "U".
void EyeCurves(const RigEye& e, float open, D2D1_POINT_2F* l, D2D1_POINT_2F* r, float* lidY, float* lowY) {
    const float cy = e.c.y + e.ry * 0.04f;
    *l = D2D1::Point2F(e.c.x - e.rx, cy);
    *r = D2D1::Point2F(e.c.x + e.rx, cy);
    const float top = cy - e.ry * 1.33f, bottom = cy + e.ry * 1.33f, shut = cy + e.ry * 0.62f;
    *lidY = shut + (top - shut) * open;
    *lowY = shut + (bottom - shut) * std::min(1.0f, open * 3.0f);
}

void DrawRigEye(ID2D1RenderTarget* rt, PackCache& cache, const RigInfo& rig, const RigEye& e, bool leftEye,
                D2D1_POINT_2F gaze, float open, bool happy, double t) {
    BrushCache& bc = Brushes();
    ComPtr<ID2D1GeometrySink> sink;
    const float stroke = e.ry * 0.13f;
    if (happy) {
        // Content "^" eyes: a thick arc bowed up.
        ComPtr<ID2D1PathGeometry> arc = NewPath(rt, &sink);
        if (!arc) return;
        const float cy = e.c.y + e.ry * 0.25f;
        sink->BeginFigure(D2D1::Point2F(e.c.x - e.rx * 0.82f, cy), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(e.c.x - e.rx * 0.55f, cy - e.ry * 1.15f),
                                            D2D1::Point2F(e.c.x + e.rx * 0.55f, cy - e.ry * 1.15f),
                                            D2D1::Point2F(e.c.x + e.rx * 0.82f, cy)));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        ComPtr<ID2D1Factory> f;
        rt->GetFactory(&f);
        ComPtr<ID2D1StrokeStyle> round;
        f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                         D2D1_LINE_JOIN_ROUND), nullptr, 0, &round);
        rt->DrawGeometry(arc.Get(), bc.Get(rt, rig.ink), e.ry * 0.26f, round.Get());
        return;
    }
    open = std::clamp(open, 0.0f, 1.0f);
    D2D1_POINT_2F l, r;
    float lidY, lowY;
    EyeCurves(e, open, &l, &r, &lidY, &lowY);

    ComPtr<ID2D1PathGeometry> lid = NewPath(rt, &sink);
    if (!lid) return;
    sink->BeginFigure(l, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(l.x, lidY), D2D1::Point2F(r.x, lidY), r));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    ComPtr<ID2D1PathGeometry> low = NewPath(rt, &sink);
    if (!low) return;
    sink->BeginFigure(r, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(r.x, lowY), D2D1::Point2F(l.x, lowY), l));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    if (open > 0.04f) {
        ComPtr<ID2D1PathGeometry> opening = NewPath(rt, &sink);
        if (!opening) return;
        sink->BeginFigure(l, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(l.x, lidY), D2D1::Point2F(r.x, lidY), r));
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(r.x, lowY), D2D1::Point2F(l.x, lowY), l));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        if (!cache.layer) rt->CreateLayer(&cache.layer);
        rt->FillGeometry(opening.Get(), bc.Get(rt, Hex(0xFBF8F6)));
        if (cache.layer) {
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), opening.Get()), cache.layer.Get());
            // The iris: toward the gaze, each eye clamped to its own white, and a
            // little toward the nose when looking ahead, as in the drawings.
            const float maxX = std::max(1.0f, e.rx - rig.iris * 0.62f), maxY = std::max(1.0f, e.ry - rig.iris * 0.70f);
            float ox = gaze.x * maxX + (leftEye ? 1.0f : -1.0f) * rig.converge * (1.0f - std::fabs(gaze.x) * 0.7f);
            float oy = gaze.y * maxY;
            const float d = std::sqrt((ox / maxX) * (ox / maxX) + (oy / maxY) * (oy / maxY));
            if (d > 1.0f) { ox /= d; oy /= d; }
            const D2D1_POINT_2F ic = D2D1::Point2F(e.c.x + ox, e.c.y + oy);
            const float R = rig.iris;
            if (!cache.iris) {
                const D2D1_GRADIENT_STOP stops[] = {{0.0f, Shade(rig.irisColor, 1.45f)},
                                                    {0.6f, rig.irisColor},
                                                    {1.0f, Shade(rig.irisColor, 0.45f)}};
                ComPtr<ID2D1GradientStopCollection> coll;
                rt->CreateGradientStopCollection(stops, 3, &coll);
                if (coll) rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(ic, {}, R, R), coll.Get(), &cache.iris);
            }
            if (cache.iris) {
                cache.iris->SetCenter(D2D1::Point2F(ic.x, ic.y + R * 0.15f));
                cache.iris->SetRadiusX(R);
                cache.iris->SetRadiusY(R);
                rt->FillEllipse(D2D1::Ellipse(ic, R, R), cache.iris.Get());
            } else {
                rt->FillEllipse(D2D1::Ellipse(ic, R, R), bc.Get(rt, rig.irisColor));
            }
            // A warm glow low in the iris, the pupil (breathing a touch), the rim.
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ic.x, ic.y + R * 0.42f), R * 0.58f, R * 0.36f),
                            bc.Get(rt, Hex(0xB07A50, 0.35f)));
            const float pr = rig.pupil * (1.0f + 0.04f * std::sin(static_cast<float>(t) * 0.9f));
            rt->FillEllipse(D2D1::Ellipse(ic, pr, pr), bc.Get(rt, Hex(0x090605)));
            rt->DrawEllipse(D2D1::Ellipse(ic, R - stroke * 0.3f, R - stroke * 0.3f), bc.Get(rt, Shade(rig.ink, 1.0f)), stroke * 0.55f);
            // Catchlights: they ride with the iris, as drawn.
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ic.x - R * 0.42f, ic.y - R * 0.45f), R * 0.24f, R * 0.24f),
                            bc.Get(rt, Hex(0xFFFFFF)));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ic.x + R * 0.48f, ic.y - R * 0.40f), R * 0.13f, R * 0.13f),
                            bc.Get(rt, Hex(0xFFFFFF, 0.9f)));
            // The lid's shadow on the white.
            rt->DrawGeometry(lid.Get(), bc.Get(rt, Hex(0x3A2420, 0.22f)), e.ry * 0.5f);
            rt->PopLayer();
        }
        rt->DrawGeometry(low.Get(), bc.Get(rt, rig.ink), stroke * 0.95f);
    }
    // The lash line: heavy, and heaviest toward the outer corner, where it flicks up.
    rt->DrawGeometry(lid.Get(), bc.Get(rt, rig.ink), stroke * (1.5f + 0.4f * (1.0f - open)));
    const float dir = leftEye ? -1.0f : 1.0f;
    const D2D1_POINT_2F outer = leftEye ? l : r;
    ComPtr<ID2D1PathGeometry> wing = open > 0.3f ? NewPath(rt, &sink) : nullptr;  // folds away as the lid closes
    if (wing) {
        const float q = Smooth((open - 0.3f) / 0.4f);
        sink->BeginFigure(D2D1::Point2F(outer.x - dir * e.rx * 0.30f * q, outer.y - e.ry * 0.62f * q), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(outer.x + dir * e.rx * 0.12f * q, outer.y - e.ry * 0.28f * q));
        sink->AddLine(D2D1::Point2F(outer.x - dir * e.rx * 0.04f * q, outer.y + e.ry * 0.10f * q));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(wing.Get(), bc.Get(rt, rig.ink));
    }
}

// A thick brow: square by the nose, arching out to a tip that turns down.
void DrawRigBrow(ID2D1RenderTarget* rt, const RigInfo& rig, const RigBrow& b, float lift, float frown) {
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (!g) return;
    const float dir = b.outer.x > b.innerTop.x ? 1.0f : -1.0f;
    const float w = std::fabs(b.outer.x - b.innerTop.x);
    const float ix = b.innerTop.x, it = b.innerTop.y + lift + frown, ib = b.innerBottom + lift + frown;
    const D2D1_POINT_2F tip = D2D1::Point2F(b.outer.x, b.outer.y + lift);
    sink->BeginFigure(D2D1::Point2F(ix, it), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(ix + dir * w * 0.35f, it - 1.6f), D2D1::Point2F(ix + dir * w * 0.86f, it - 1.0f), tip));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(ix + dir * w * 0.82f, ib - 3.0f), D2D1::Point2F(ix + dir * w * 0.40f, ib - 1.2f),
                                        D2D1::Point2F(ix, ib)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(g.Get(), Brushes().Get(rt, rig.ink));
}

// The face and its live features, in face-picture pixels under the current transform.
void DrawRig(ID2D1RenderTarget* rt, PackCache& cache, const RigInfo& rig, const PackImage& face, const Style& st,
             const std::wstring& mood, const RigState& rs, double t) {
    const D2D1_POINT_2F g = rs.gaze;
    const bool happy = mood == L"happy";
    D2D1_MATRIX_3X2_F m;
    rt->GetTransform(&m);
    // The head turns a touch toward where the eyes look.
    rt->SetTransform(D2D1::Matrix3x2F::Translation(g.x * 1.6f, g.y * 0.9f) * m);
    DrawImageHQ(rt, cache, face, D2D1::RectF(0, 0, face.size.width, face.size.height),
                D2D1::RectF(0, 0, face.size.width, face.size.height), 1.0f);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(g.x * 2.4f, g.y * 1.4f) * m);
    const float blink = happy ? 0.0f : RigBlink(t, st.blinkSeconds);
    // Lids drop as the eyes look down, and drift a little on their own.
    const float open = 1.0f - 0.32f * std::max(0.0f, g.y) - 0.05f * (0.5f + 0.5f * std::sin(static_cast<float>(t) * 0.6f));
    for (int i = 0; i < 2; ++i)
        DrawRigEye(rt, cache, rig, rig.eyes[i], i == 0, g, open * (1.0f - blink), happy, t + i * 0.02);
    if (rig.hasBrows) {
        const float lift = happy ? -3.0f : -2.6f * std::max(0.0f, -g.y) + 1.2f * std::max(0.0f, g.y) + 0.8f * blink;
        for (int i = 0; i < 2; ++i) DrawRigBrow(rt, rig, rig.brows[i], lift, 0.0f);
    }
    rt->SetTransform(m);
}

bool RigBusy(double t) {
    if (t - g_rigDrawnAt.load(std::memory_order_relaxed) > 1.0) return false;
    if (t < g_rigSettleUntil.load(std::memory_order_relaxed)) return true;
    // A blink is playing or about to.
    const float period = g_rigBlinkPeriod.load(std::memory_order_relaxed);
    return RigBlink(t, period) > 0.0f || RigBlink(t + 0.12, period) > 0.0f;
}

// One mood of a framed pack: this moment's frame, cross-faded from the one
// before for its first moments, or the face rig.
void DrawPackMood(ID2D1RenderTarget* rt, PackCache& cache, const PackInfo& info, const std::wstring& dir, const Style& st,
                  D2D1_POINT_2F c, float s, Mood mood, const std::wstring& stemIn, double t, float since, D2D1_POINT_2F look,
                  const RigState& rs, float opacity) {
    const D2D1_POINT_2F base = D2D1::Point2F(c.x, c.y + s * 0.5f);  // squash from the feet
    D2D1_MATRIX_3X2_F saved;
    rt->GetTransform(&saved);
    std::wstring stem = stemIn;
    const bool rigged = info.rig.on && std::find(info.rig.moods.begin(), info.rig.moods.end(), stem) != info.rig.moods.end();
    if (!rigged && !info.sequences.count(stem)) {
        std::wstring fb = FallbackFile(mood);
        stem = fb.substr(0, fb.size() - 4);
        if (!info.sequences.count(stem)) stem = L"idle";
    }
    const D2D1_RECT_F ref = info.ref;
    const float refW = ref.right - ref.left, refH = ref.bottom - ref.top;
    if (refW < 1.0f || refH < 1.0f) return;
    // Scale from idle's first frame, so every frame and mood keeps one size; the
    // frames carry their own motion, so the body moves only a little on top.
    // Drawn frames carry all their own motion, the celebration included:
    // tilting, bobbing or hopping them on top only makes them wobble. The live
    // face breathes.
    Motion m = BodyMotion(st, mood, t, since, s);
    if (rigged) {
        m.bob *= 0.4f;
        m.tilt = 0.0f;
    } else {
        m = Motion{};
    }
    const float fit = s / std::max(refW, refH);
    const float left = c.x - (ref.left + refW * 0.5f) * fit;
    const float top = base.y - ref.bottom * fit + m.bob;
    const D2D1_MATRIX_3X2_F canvas = D2D1::Matrix3x2F::Scale(fit, fit) * D2D1::Matrix3x2F::Translation(left, top) *
                                     D2D1::Matrix3x2F::Rotation(m.tilt, base) * saved;
    rt->SetTransform(canvas);
    const bool layered = opacity < 0.999f;
    if (layered) {
        if (!cache.fadeLayer) rt->CreateLayer(&cache.fadeLayer);
        if (cache.fadeLayer)
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::IdentityMatrix(), opacity),
                          cache.fadeLayer.Get());
    }

    if (rigged) {
        if (const PackImage* face = LoadPackImage(rt, cache, dir + info.rig.face, false))
            DrawRig(rt, cache, info.rig, *face, st, stem, rs, t);
    } else if (const auto seqIt = info.sequences.find(stem); seqIt != info.sequences.end() && !seqIt->second.empty()) {
        const std::vector<FrameStep>& seq = seqIt->second;
        float total = 0.0f;
        for (const FrameStep& stp : seq) total += stp.length;
        // Idle runs on the clock so it never restarts; other moods start at their first frame.
        const float clock = static_cast<float>(mood == Mood::Idle ? t : since) / info.frameSeconds;
        float pos = std::fmod(clock, total);
        if (pos < 0.0f) pos += total;
        size_t index = 0;
        for (; index + 1 < seq.size() && pos >= seq[index].length; ++index) pos -= seq[index].length;
        const int frame = seq[index].frame;
        const int before = seq[(index + seq.size() - 1) % seq.size()].frame;
        // Fade in over the first moments of a step (not the very first step of a
        // mood), never more than a third of it: quick frames that blend for
        // most of their time look smeared and sluggish.
        const float fade = std::min(info.fadeSeconds / info.frameSeconds, seq[index].length * 0.3f);
        const float a = fade > 0.0f && before != frame && (mood == Mood::Idle || clock >= fade) ? std::min(1.0f, pos / fade) : 1.0f;
        std::wstring file = stem + L"_" + std::to_wstring(frame) + L".png";
        const bool restful = (mood == Mood::Idle || mood == Mood::Listening) && frame == seq.front().frame;
        bool looked = false;
        if (restful && info.look && (std::fabs(look.x) > 0.3f || std::fabs(look.y) > 0.3f)) {
            const wchar_t* dirName = std::fabs(look.x) >= std::fabs(look.y) * 1.2f ? (look.x < 0 ? L"look_left" : L"look_right")
                                                                                   : (look.y < 0 ? L"look_up" : L"look_down");
            if (FileExists(dir + dirName + L".png")) { file = std::wstring(dirName) + L".png"; looked = true; }
        }
        const bool track = !looked && restful && std::find(info.eyeFrames.begin(), info.eyeFrames.end(), file) != info.eyeFrames.end();
        if (a < 1.0f && !looked) {
            // Old frame underneath until halfway, then fading out under the new one,
            // so the character never turns see-through mid-change.
            if (PackImage* prev = LoadPackImage(rt, cache, dir + stem + L"_" + std::to_wstring(before) + L".png", false))
                DrawImageHQ(rt, cache, *prev, D2D1::RectF(0, 0, prev->size.width, prev->size.height),
                            D2D1::RectF(0, 0, prev->size.width, prev->size.height), std::min(1.0f, 2.0f * (1.0f - a)));
        }
        if (PackImage* im = LoadPackImage(rt, cache, dir + file, track))
            DrawPackImage(rt, cache, *im, D2D1::RectF(0, 0, im->size.width, im->size.height),
                          D2D1::RectF(0, 0, im->size.width, im->size.height), look, track, a < 1.0f ? std::min(1.0f, 2.0f * a) : 1.0f);
    }

    if (layered && cache.fadeLayer) rt->PopLayer();
    rt->SetTransform(saved);
}

void DrawPack(ID2D1RenderTarget* rt, const Style& st, D2D1_POINT_2F c, float s, Mood mood, double t, float since,
              D2D1_POINT_2F look) {
    PackCache& cache = Packs(rt);
    const PackInfo& info = LoadPackInfo(rt, cache, st.customPack);
    const std::wstring dir = PacksFolder() + L"\\" + st.customPack + L"\\";
    if (!st.eyesFollow) look = D2D1::Point2F(0, 0);
    const D2D1_POINT_2F base = D2D1::Point2F(c.x, c.y + s * 0.5f);  // squash from the feet
    D2D1_MATRIX_3X2_F saved;
    rt->GetTransform(&saved);

    if (!info.frames) {
        // One picture per mood: fit its visible part, so margins don't shrink a mood.
        PackImage* im = nullptr;
        std::wstring file;
        for (const wchar_t* f : {MoodFile(mood), FallbackFile(mood), L"idle.png"})
            if ((im = LoadPackImage(rt, cache, dir + f, std::find(info.eyeFrames.begin(), info.eyeFrames.end(), f) != info.eyeFrames.end()))) {
                file = f;
                break;
            }
        if (!im) return;
        const D2D1_RECT_F src = im->visible;
        const D2D1_SIZE_F bs = D2D1::SizeF(src.right - src.left, src.bottom - src.top);
        if (bs.width < 1.0f || bs.height < 1.0f) return;
        const Motion m = BodyMotion(st, mood, t, since, s);
        const float fit = s / std::max(bs.width, bs.height);
        const float w = bs.width * fit, h = bs.height * fit * m.squash;
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(m.tilt, base) * saved);
        // Bitmaps are created at 96 DPI, so their DIPs equal pixels and `src` maps directly.
        DrawPackImage(rt, cache, *im, D2D1::RectF(c.x - w * 0.5f, base.y - h + m.bob, c.x + w * 0.5f, base.y + m.bob), src, look,
                      mood == Mood::Idle || mood == Mood::Listening);
        rt->SetTransform(saved);
        return;
    }

    // Frames on a shared canvas. Moods cross-fade into each other: the state
    // remembers the mood this mascot showed before.
    RigState& rs = RigStateFor(c, s, t);
    if (info.rig.on) {
        UpdateGaze(rs, look, t);
        g_rigDrawnAt.store(t, std::memory_order_relaxed);
        g_rigBlinkPeriod.store(st.blinkSeconds, std::memory_order_relaxed);
    } else {
        rs.lastT = t;
    }
    const std::wstring stem = Stem(mood);
    if (stem != rs.mood) {
        if (!rs.mood.empty() && t - rs.moodAt > info.fadeSeconds) {
            rs.prevMood = rs.mood;
            rs.prevStart = rs.moodStart;
        }
        rs.mood = stem;
        rs.moodAt = t;
    }
    rs.moodStart = t - since;
    const float a = info.fadeSeconds > 0.0f ? static_cast<float>((t - rs.moodAt) / (info.fadeSeconds * 1.5f)) : 1.0f;
    if (a < 1.0f && !rs.prevMood.empty()) {
        Mood prev = Mood::Idle;
        for (int m = 0; m <= static_cast<int>(Mood::Confused); ++m)
            if (Stem(static_cast<Mood>(m)) == rs.prevMood) prev = static_cast<Mood>(m);
        DrawPackMood(rt, cache, info, dir, st, c, s, prev, rs.prevMood, t, static_cast<float>(t - rs.prevStart), look, rs,
                     std::min(1.0f, 2.0f * (1.0f - a)));
        DrawPackMood(rt, cache, info, dir, st, c, s, mood, stem, t, since, look, rs, std::min(1.0f, 2.0f * a));
    } else {
        DrawPackMood(rt, cache, info, dir, st, c, s, mood, stem, t, since, look, rs, 1.0f);
    }
}

// ── Spidey ──────────────────────────────────────────────────────────────────
// A chibi Spider-Man mask: a slightly tall, shaded red head under a real web
// (radial lines from the centre of the face, bowed rings between them) and the
// big white lenses with heavy black frames that carry every expression.

ComPtr<ID2D1PathGeometry> NewPath(ID2D1RenderTarget* rt, ComPtr<ID2D1GeometrySink>* sink) {
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(&f);
    ComPtr<ID2D1PathGeometry> g;
    if (!f || FAILED(f->CreatePathGeometry(&g)) || FAILED(g->Open(sink->GetAddressOf()))) return nullptr;
    return g;
}

float SnackBite(double t);

// Spidey's suit colours. A colour picked for him is a Venom suit in that shade:
// the suit goes dark, the black web lines turn grey and the spider logo white.
struct SpideyPalette {
    D2D1_COLOR_F suit, rim, shine, web, outline, roll, mouth, logo;
};

SpideyPalette SpideyPaletteFor(const Style& st) {
    if (st.character != Character::Spiderman || !st.hasColor)
        return {Hex(0xD3222C), Hex(0x8E1219), Hex(0xFF5A63, 0.30f), Hex(0x4A0A10, 0.62f), Hex(0x5E0B12, 0.9f),
                Hex(0xB51B26), Hex(0x7A0F14, 0.85f), Hex(0x14111A)};
    const D2D1_COLOR_F c = st.color;
    // A grey-blue edge keeps the dark suit from melting into the dark island.
    return {c, Shade(c, 0.55f), Hex(0x9AA4C8, 0.20f), Hex(0x9AA0AC, 0.70f), Hex(0x5A6070, 0.9f),
            Shade(c, 0.85f), Hex(0x000000, 0.75f), Hex(0xF2F4F8)};
}

thread_local SpideyPalette t_spidey = SpideyPaletteFor(Style{});  // set by DrawStyled for each frame
// Streaming is drawn as Snack (the same chewing face and motion) with the eyes
// open on the screen and popcorn instead of each character's own snack.
thread_local bool t_streaming = false;  // set by DrawStyled for each frame
thread_local bool t_drinking = false;   // Mood::Drinking, drawn as Snack with a glass (DrawStyled)
// Do Not Disturb is drawn as Sleepy with headphones and a moon badge; Live as
// Working (at the laptop) with a pulsing REC badge and broadcast waves.
thread_local bool t_dnd = false;
thread_local bool t_live = false;

void SpideyHead(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, Mood mood,
                double t, const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    rt->SetTransform(world);
    const D2D1_ELLIPSE head = D2D1::Ellipse(c, rx, ry);
    // Egg-shaped mask: a full brow narrowing to the chin.
    ComPtr<ID2D1GeometrySink> eggSink;
    ComPtr<ID2D1PathGeometry> egg = NewPath(rt, &eggSink);
    if (egg) {
        const float topY = c.y - ry, chinY = c.y + ry;
        eggSink->BeginFigure(D2D1::Point2F(c.x, topY), D2D1_FIGURE_BEGIN_FILLED);
        eggSink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx * 0.58f, topY), D2D1::Point2F(c.x + rx, c.y - ry * 0.52f),
                                               D2D1::Point2F(c.x + rx, c.y - ry * 0.02f)));
        eggSink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx, c.y + ry * 0.55f), D2D1::Point2F(c.x + rx * 0.48f, chinY),
                                               D2D1::Point2F(c.x, chinY)));
        eggSink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - rx * 0.48f, chinY), D2D1::Point2F(c.x - rx, c.y + ry * 0.55f),
                                               D2D1::Point2F(c.x - rx, c.y - ry * 0.02f)));
        eggSink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - rx, c.y - ry * 0.52f), D2D1::Point2F(c.x - rx * 0.58f, topY),
                                               D2D1::Point2F(c.x, topY)));
        eggSink->EndFigure(D2D1_FIGURE_END_CLOSED);
        eggSink->Close();
    }

    // Shaded mask: deep rim below, the suit, a soft highlight top-left.
    const SpideyPalette& pal = t_spidey;
    if (egg) {
        rt->SetTransform(D2D1::Matrix3x2F::Translation(0, ry * 0.05f) * world);
        rt->FillGeometry(egg.Get(), brush(pal.rim));
        rt->SetTransform(D2D1::Matrix3x2F::Scale(0.985f, 0.97f, D2D1::Point2F(c.x, c.y - ry * 0.02f)) * world);
        rt->FillGeometry(egg.Get(), brush(pal.suit));
        rt->SetTransform(world);
    }
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - rx * 0.30f, c.y - ry * 0.52f), rx * 0.42f, ry * 0.24f),
                    brush(pal.shine));

    // The web, clipped to the head.
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(&f);
    ComPtr<ID2D1Layer> layer;
    if (egg && SUCCEEDED(rt->CreateLayer(&layer))) {
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), egg.Get()), layer.Get());
        const D2D1_COLOR_F webCol = pal.web;
        const float webW = std::max(0.8f, s * 0.011f);
        const D2D1_POINT_2F hub = D2D1::Point2F(c.x, c.y + ry * 0.10f);
        constexpr int kSpokes = 12;
        const float reach = std::max(rx, ry) * 1.6f;
        D2D1_POINT_2F dir[kSpokes];
        for (int i = 0; i < kSpokes; ++i) {
            const float a = (i + 0.5f) * 2.0f * kPi / kSpokes;
            dir[i] = D2D1::Point2F(std::sin(a), -std::cos(a));
            rt->DrawLine(hub, D2D1::Point2F(hub.x + dir[i].x * reach, hub.y + dir[i].y * reach), brush(webCol), webW);
        }
        // Rings: each segment between two spokes bows toward the hub, like real silk.
        for (float r : {0.30f, 0.56f, 0.84f, 1.14f}) {
            const float rr = r * rx;
            ComPtr<ID2D1GeometrySink> sink;
            ComPtr<ID2D1PathGeometry> ring = NewPath(rt, &sink);
            if (!ring) continue;
            sink->BeginFigure(D2D1::Point2F(hub.x + dir[0].x * rr, hub.y + dir[0].y * rr), D2D1_FIGURE_BEGIN_HOLLOW);
            for (int i = 1; i <= kSpokes; ++i) {
                const D2D1_POINT_2F a = dir[(i - 1) % kSpokes], b = dir[i % kSpokes];
                const D2D1_POINT_2F mid = D2D1::Point2F((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
                    D2D1::Point2F(hub.x + mid.x * rr * 0.82f, hub.y + mid.y * rr * 0.82f),
                    D2D1::Point2F(hub.x + b.x * rr, hub.y + b.y * rr)));
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->DrawGeometry(ring.Get(), brush(webCol), webW);
        }
        rt->PopLayer();
    }
    if (egg) rt->DrawGeometry(egg.Get(), brush(pal.outline), std::max(0.8f, s * 0.012f));
    (void)head;

    // Snack: the mask is rolled up to the nose so he can eat, showing a chewing
    // mouth under a thick rolled band of suit fabric.
    if (mood != Mood::Snack || !egg || !f) return;
    const float rollY = c.y + ry * 0.36f;
    auto clipped = [&](float top, float bottom) {
        ComPtr<ID2D1RectangleGeometry> band;
        ComPtr<ID2D1GeometrySink> sink;
        ComPtr<ID2D1PathGeometry> out = NewPath(rt, &sink);
        if (!out || FAILED(f->CreateRectangleGeometry(D2D1::RectF(c.x - rx * 2.0f, top, c.x + rx * 2.0f, bottom), &band)))
            return ComPtr<ID2D1PathGeometry>();
        egg->CombineWithGeometry(band.Get(), D2D1_COMBINE_MODE_INTERSECT, nullptr, sink.Get());
        sink->Close();
        return out;
    };
    if (ComPtr<ID2D1PathGeometry> chin = clipped(rollY, c.y + ry * 2.0f)) {
        rt->FillGeometry(chin.Get(), brush(Hex(0xF0C4A0)));
        rt->DrawGeometry(chin.Get(), brush(Hex(0xA8704F)), std::max(0.8f, s * 0.012f));
    }
    // Chewing: the mouth opens as the slice drops away and closes on each bite.
    const float chew = SnackBite(t);
    const D2D1_POINT_2F mouth = D2D1::Point2F(c.x - rx * 0.06f, c.y + ry * 0.68f);
    rt->FillEllipse(D2D1::Ellipse(mouth, rx * 0.13f, ry * (0.025f + 0.07f * chew)), brush(Hex(0x5A1F22)));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + rx * 0.30f, c.y + ry * 0.62f), rx * 0.15f, ry * 0.10f * (1.0f - chew * 0.5f)),
                    brush(Hex(0xF59A8A, 0.55f)));  // a full cheek
    if (ComPtr<ID2D1PathGeometry> roll = clipped(rollY - ry * 0.09f, rollY + ry * 0.07f)) {
        rt->SetTransform(D2D1::Matrix3x2F::Scale(1.04f, 1.0f, c) * world);
        rt->FillGeometry(roll.Get(), brush(pal.roll));
        rt->DrawGeometry(roll.Get(), brush(pal.outline), std::max(0.8f, s * 0.012f));
        for (float x : {-0.45f, 0.05f, 0.5f})
            rt->DrawLine(D2D1::Point2F(c.x + rx * x, rollY - ry * 0.06f), D2D1::Point2F(c.x + rx * (x + 0.08f), rollY + ry * 0.04f),
                         brush(pal.web), std::max(0.7f, s * 0.008f));
        rt->SetTransform(world);
    }
}

void SpiralEye(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float r, double t, D2D1_COLOR_F ink, bool whiteBack,
               float lw, const D2D1_MATRIX_3X2_F& world);

void SpideyLenses(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, Mood mood,
                  double t, float since, const Style& st, D2D1_POINT_2F look, const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    if (mood == Mood::Working) {
        look = ReadingLook(t);
    } else if (mood == Mood::Thinking) {
        look = D2D1::Point2F(0.7f, -0.9f);  // gazing up toward the "?"
    }
    const float lookX = std::clamp(look.x, -1.0f, 1.0f) * s * 0.022f;
    const float lookY = std::clamp(look.y, -1.0f, 1.0f) * s * 0.016f;

    for (int side = -1; side <= 1; side += 2) {
        // Lens shape in mask space: a broad teardrop, pointed toward the bridge of
        // the nose, sweeping up and out to the temple.
        const D2D1_POINT_2F in = D2D1::Point2F(c.x + side * rx * 0.21f, c.y + ry * 0.07f);
        const D2D1_POINT_2F top = D2D1::Point2F(c.x + side * rx * 0.40f, c.y - ry * 0.40f);
        const D2D1_POINT_2F out = D2D1::Point2F(c.x + side * rx * 0.84f, c.y - ry * 0.30f);
        const D2D1_POINT_2F bot = D2D1::Point2F(c.x + side * rx * 0.60f, c.y + ry * 0.15f);

        ComPtr<ID2D1GeometrySink> sink;
        ComPtr<ID2D1PathGeometry> lens = NewPath(rt, &sink);
        if (!lens) return;
        sink->BeginFigure(in, D2D1_FIGURE_BEGIN_FILLED);
        // Inner edge: slightly concave, rising from the point by the nose.
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(in.x + side * rx * 0.02f, c.y - ry * 0.14f),
                                            D2D1::Point2F(top.x - side * rx * 0.20f, top.y + ry * 0.02f), top));
        // Upper outside: a full, round sweep to the temple.
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(top.x + side * rx * 0.22f, top.y - ry * 0.05f),
                                            D2D1::Point2F(out.x + side * rx * 0.06f, out.y - ry * 0.18f), out));
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(out.x + side * rx * 0.04f, out.y + ry * 0.22f),
                                            D2D1::Point2F(bot.x + side * rx * 0.16f, bot.y + ry * 0.03f), bot));
        // Lower edge back to the point.
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(bot.x - side * rx * 0.20f, bot.y),
                                            D2D1::Point2F(in.x + side * rx * 0.12f, in.y + ry * 0.04f), in));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        // Expressions are all in how the lenses squash, lift and tilt.
        const D2D1_POINT_2F mid = D2D1::Point2F((in.x + out.x) * 0.5f, (top.y + bot.y) * 0.5f);
        float sy = 1.0f, sx = 1.0f, lift = 0.0f, rot = 0.0f;
        const float blink = BlinkAmount(t + (side > 0 ? 0.02 : 0.0), st.blinkSeconds);
        switch (mood) {
            case Mood::Happy:
            case Mood::Music: sy = 0.42f; lift = -ry * 0.04f; break;   // smiling squint
            case Mood::Sleepy: sy = 0.16f; lift = ry * 0.06f; break;   // nearly shut
            case Mood::Sad: {
                // Outer corners droop, and the lenses sag a little lower on each sigh.
                const float sigh = 0.5f + 0.5f * std::sin(static_cast<float>(t) * 1.6f);
                rot = side * (32.0f + 4.0f * sigh); sy = 0.66f - 0.06f * sigh; lift = ry * (0.06f + 0.03f * sigh);
                break;
            }
            case Mood::Alert: sx = sy = 1.14f; break;                  // wide open
            case Mood::Thinking:
                // Skeptical: one lens squinted, the other raised, with a slow "hmm" pulse.
                if (side < 0) { sy = 0.55f + 0.08f * std::sin(static_cast<float>(t) * 1.5f); }
                else { sy = 1.04f; lift = -ry * 0.10f; rot = -5.0f; }
                break;
            case Mood::Working: sy = 0.80f; break;                     // focused
            case Mood::Snack: if (!t_streaming) { sy = 0.50f; lift = -ry * 0.03f; } break;   // content squint
            case Mood::Listening: sx = sy = 1.08f; lift = -ry * 0.04f; break;  // wide, attentive
            case Mood::Talking: sy = 0.80f + 0.14f * TalkOpen(t); break;       // lenses bob as he talks
            case Mood::Confused: sx = sy = 1.06f; break;  // wide, with spirals (below)
            default: break;
        }
        sy *= 1.0f - 0.88f * Smooth(blink);
        rt->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy, D2D1::Point2F(mid.x, bot.y)) *
                         D2D1::Matrix3x2F::Rotation(rot, in) *
                         D2D1::Matrix3x2F::Translation(lookX, lookY + lift) * world);

        rt->DrawGeometry(lens.Get(), brush(Hex(0x0E0C12)), std::max(2.0f, s * 0.058f));  // heavy frame
        rt->FillGeometry(lens.Get(), brush(Hex(0xF7F9FC)));
        // A cool sheen low in the lens, and a glint up top, clipped to the lens.
        ComPtr<ID2D1Layer> layer;
        if (sy > 0.3f && SUCCEEDED(rt->CreateLayer(&layer))) {
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), lens.Get()), layer.Get());
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mid.x, bot.y + ry * 0.02f), rx * 0.40f, ry * 0.16f),
                            brush(Hex(0xDCE3EE, 0.75f)));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(top.x + side * rx * 0.05f, top.y + ry * 0.12f), rx * 0.10f, ry * 0.06f),
                            brush(Hex(0xFFFFFF)));
            rt->PopLayer();
        }
        if (mood == Mood::Confused) {  // dizzy: a spiral spinning in each lens
            D2D1_MATRIX_3X2_F lensSpace;
            rt->GetTransform(&lensSpace);
            SpiralEye(rt, cache, D2D1::Point2F(mid.x, mid.y + ry * 0.02f), rx * 0.21f, t + side * 0.15, Hex(0x0E0C12), false,
                      std::max(1.0f, s * 0.022f), lensSpace);
        }
    }
    rt->SetTransform(world);
}

// Strand of web silk from `a` to `b`, fading out toward `a` so a line that runs
// out of the frame never ends in a hard cut.
void Silk(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F a, D2D1_POINT_2F b, float s) {
    constexpr int kSteps = 5;
    for (int i = 0; i < kSteps; ++i) {
        const float u0 = static_cast<float>(i) / kSteps, u1 = static_cast<float>(i + 1) / kSteps;
        rt->DrawLine(D2D1::Point2F(a.x + (b.x - a.x) * u0, a.y + (b.y - a.y) * u0),
                     D2D1::Point2F(a.x + (b.x - a.x) * u1, a.y + (b.y - a.y) * u1),
                     cache.Get(rt, Hex(0xE8ECF4, 0.9f * u1)), std::max(1.0f, s * 0.016f));
    }
}

// A bobbing "?" beside the head while thinking: a soft white with a dark drop shadow.
// Spidey's (`onWeb`) hangs from a strand of silk instead and swings on it.
void ThinkQuestion(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t,
                   const D2D1_MATRIX_3X2_F& world, D2D1_COLOR_F ink, D2D1_COLOR_F shadow, bool onWeb = false) {
    const float bob = onWeb ? 0.0f : std::sin(static_cast<float>(t) * 2.4f) * s * 0.03f;
    const float q = s * 0.17f;
    const D2D1_POINT_2F o = D2D1::Point2F(c.x + rx * 1.15f, c.y - ry * 1.05f + bob);
    D2D1_MATRIX_3X2_F frame = world;
    if (onWeb) {
        const D2D1_POINT_2F knot = D2D1::Point2F(o.x + q * 0.02f, o.y - q * 0.66f);
        const D2D1_POINT_2F anchor = D2D1::Point2F(knot.x, knot.y - q * 1.1f);
        frame = D2D1::Matrix3x2F::Rotation(std::sin(static_cast<float>(t) * 1.8f) * 12.0f, anchor) * world;
        rt->SetTransform(frame);
        Silk(rt, cache, anchor, knot, s);
    }
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> hook = NewPath(rt, &sink);
    if (!hook) return;
    sink->BeginFigure(D2D1::Point2F(o.x - q * 0.38f, o.y - q * 0.25f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(o.x - q * 0.38f, o.y - q * 0.75f),
                                        D2D1::Point2F(o.x + q * 0.42f, o.y - q * 0.80f),
                                        D2D1::Point2F(o.x + q * 0.40f, o.y - q * 0.30f)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(o.x + q * 0.38f, o.y),
                                        D2D1::Point2F(o.x, o.y + q * 0.02f),
                                        D2D1::Point2F(o.x, o.y + q * 0.30f)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    const float w = std::max(1.4f, q * 0.22f);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(q * 0.05f, q * 0.05f) * frame);
    rt->DrawGeometry(hook.Get(), cache.Get(rt, shadow), w);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y + q * 0.58f), w * 0.62f, w * 0.62f), cache.Get(rt, shadow));
    rt->SetTransform(frame);
    rt->DrawGeometry(hook.Get(), cache.Get(rt, ink), w);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y + q * 0.58f), w * 0.62f, w * 0.62f), cache.Get(rt, ink));
    rt->SetTransform(world);
}

// "Needs you" badge: an amber bubble with a "!" that pops in on each hop cycle.
void AlertBadge(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t,
                const D2D1_MATRIX_3X2_F& world) {
    const float cycle = std::fmod(static_cast<float>(t), 1.3f);
    // Overshooting pop at the start of each cycle, then a gentle breathe.
    const float pop = cycle < 0.25f ? 1.0f + 0.35f * std::sin(cycle / 0.25f * kPi) : 1.0f + 0.04f * std::sin(cycle * 6.0f);
    const D2D1_POINT_2F o = D2D1::Point2F(c.x + rx * 1.05f, c.y - ry * 1.05f);
    rt->SetTransform(D2D1::Matrix3x2F::Scale(pop, pop, o) * D2D1::Matrix3x2F::Rotation(std::sin(static_cast<float>(t) * 5.0f) * 6.0f, o) * world);
    const float r = s * 0.085f;
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x + s * 0.008f, o.y + s * 0.012f), r, r), cache.Get(rt, Hex(0x1A1622, 0.45f)));
    rt->FillEllipse(D2D1::Ellipse(o, r, r), cache.Get(rt, Hex(0xFFB627)));
    Triangle(rt, cache.Get(rt, Hex(0xFFB627)), D2D1::Point2F(o.x - r * 0.55f, o.y + r * 0.62f),
             D2D1::Point2F(o.x - r * 1.05f, o.y + r * 1.15f), D2D1::Point2F(o.x - r * 0.05f, o.y + r * 0.85f));  // speech tail
    const D2D1_COLOR_F ink = Hex(0xFFFFFF);
    const float w = r * 0.26f;
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(o.x - w * 0.5f, o.y - r * 0.62f, o.x + w * 0.5f, o.y + r * 0.22f), w * 0.5f, w * 0.5f),
                             cache.Get(rt, ink));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y + r * 0.50f), w * 0.58f, w * 0.58f), cache.Get(rt, ink));
    rt->SetTransform(world);
}

// Listening: two arcs pulsing in toward the ear on the side you're typing on.
void ListenWaves(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t,
                 const D2D1_MATRIX_3X2_F& world) {
    rt->SetTransform(world);
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(&f);
    for (int i = 0; i < 2; ++i) {
        // Each arc travels inward and fades as it reaches the head.
        const float ph = std::fmod(static_cast<float>(t) * 0.9f + i * 0.5f, 1.0f);
        const float r = rx * (1.55f - 0.35f * ph);
        const float a = std::sin(ph * kPi) * 0.85f;
        ComPtr<ID2D1PathGeometry> arc;
        if (!f || FAILED(f->CreatePathGeometry(&arc))) return;
        ComPtr<ID2D1GeometrySink> sink;
        arc->Open(&sink);
        const D2D1_POINT_2F o = D2D1::Point2F(c.x, c.y - ry * 0.10f);
        sink->BeginFigure(D2D1::Point2F(o.x + r * 0.81f, o.y - r * 0.59f), D2D1_FIGURE_BEGIN_HOLLOW);  // -36°
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(o.x + r * 0.81f, o.y + r * 0.59f), D2D1::SizeF(r, r), 0.0f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        rt->DrawGeometry(arc.Get(), cache.Get(rt, Hex(0x7FD4FF, a)), std::max(1.2f, s * 0.03f), nullptr);
    }
}

// Talking: a speech bubble whose three dots light up in turn.
void SpeechBubble(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t,
                  const D2D1_MATRIX_3X2_F& world) {
    const float bob = std::sin(static_cast<float>(t) * 2.4f) * s * 0.012f;
    const D2D1_POINT_2F o = D2D1::Point2F(c.x + rx * 1.12f, c.y - ry * 1.05f + bob);
    rt->SetTransform(world);
    const float w = s * 0.15f, h = s * 0.10f;
    const D2D1_ROUNDED_RECT bubble = D2D1::RoundedRect(D2D1::RectF(o.x - w, o.y - h, o.x + w, o.y + h), h * 0.9f, h * 0.9f);
    const D2D1_ROUNDED_RECT shadow = D2D1::RoundedRect(D2D1::RectF(o.x - w + s * 0.008f, o.y - h + s * 0.012f, o.x + w + s * 0.008f,
                                                                   o.y + h + s * 0.012f), h * 0.9f, h * 0.9f);
    rt->FillRoundedRectangle(shadow, cache.Get(rt, Hex(0x1A1622, 0.45f)));
    rt->FillRoundedRectangle(bubble, cache.Get(rt, Hex(0xFFFFFF)));
    Triangle(rt, cache.Get(rt, Hex(0xFFFFFF)), D2D1::Point2F(o.x - w * 0.55f, o.y + h * 0.6f),
             D2D1::Point2F(o.x - w * 1.05f, o.y + h * 1.55f), D2D1::Point2F(o.x - w * 0.10f, o.y + h * 0.85f));  // tail
    for (int i = 0; i < 3; ++i) {
        const float lit = 0.5f + 0.5f * std::sin(static_cast<float>(t) * 7.0f - i * 1.1f);
        const float r = s * 0.021f * (0.8f + 0.3f * lit);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x + (i - 1) * w * 0.55f, o.y - lit * s * 0.008f), r, r),
                        cache.Get(rt, Hex(0x5B5868, 0.45f + 0.55f * lit)));
    }
}

// A five-point star centred on `c`.
// Colours, not brushes: the cache hands out one brush, recoloured on each Get.
void Star(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_COLOR_F fill, D2D1_COLOR_F edge, D2D1_POINT_2F c, float r, float spin, float lw) {
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (!g) return;
    for (int i = 0; i < 10; ++i) {
        const float a = spin + i * kPi / 5.0f - kPi / 2.0f;
        const float rr = i % 2 ? r * 0.45f : r;
        const D2D1_POINT_2F p = D2D1::Point2F(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
        if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED);
        else sink->AddLine(p);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(g.Get(), cache.Get(rt, fill));
    rt->DrawGeometry(g.Get(), cache.Get(rt, edge), lw);
}

// Confused: dizzy. A golden ring circles the head with stars riding on it. Drawn
// in two halves, so the back half and its stars go behind the head (`front` false,
// before the head) and the front half over it.
void DizzyRing(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F head, float rx, float ry, float s, double t,
               const D2D1_MATRIX_3X2_F& world, bool front) {
    const D2D1_POINT_2F o = D2D1::Point2F(head.x, head.y - ry * 0.80f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-8.0f, o) * world);  // tipped a little, like a wobbling halo
    const float a = rx * 1.32f, b = rx * 0.30f;
    const float w = std::max(1.4f, s * 0.035f);
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> arc = NewPath(rt, &sink);
    if (!arc) return;
    // In screen space a clockwise sweep from left to right passes over the top: the back.
    sink->BeginFigure(D2D1::Point2F(o.x - a, o.y), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(o.x + a, o.y), D2D1::SizeF(a, b), 0.0f,
                                  front ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE : D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    const float alpha = front ? 1.0f : 0.75f;
    rt->DrawGeometry(arc.Get(), cache.Get(rt, Hex(0x1A1622, 0.55f * alpha)), w * 1.9f);
    rt->DrawGeometry(arc.Get(), cache.Get(rt, Hex(0xFFD23F, alpha)), w);
    // Three stars ride the ring; the ones behind the head are a little smaller.
    for (int i = 0; i < 3; ++i) {
        const float th = static_cast<float>(t) * 2.2f + i * 2.0f * kPi / 3.0f;
        const bool inFront = std::sin(th) > 0.0f;
        if (inFront != front) continue;
        const D2D1_POINT_2F p = D2D1::Point2F(o.x + std::cos(th) * a, o.y + std::sin(th) * b);
        Star(rt, cache, Hex(0xFFD23F, alpha), Hex(0x1A1622, 0.6f * alpha), p,
             s * (inFront ? 0.08f : 0.06f), static_cast<float>(t) * 3.0f + i, std::max(0.8f, s * 0.012f));
    }
    rt->SetTransform(world);
}

// Confused eyes: spirals spinning, on white (or straight on the visor for Bolt).
void SpiralEye(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float r, double t, D2D1_COLOR_F ink, bool whiteBack,
               float lw, const D2D1_MATRIX_3X2_F& world) {
    rt->SetTransform(world);
    if (whiteBack) {
        rt->FillEllipse(D2D1::Ellipse(c, r, r), cache.Get(rt, Hex(0xFFFFFF)));
        rt->DrawEllipse(D2D1::Ellipse(c, r, r), cache.Get(rt, ink), lw);
    }
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-static_cast<float>(t) * 320.0f, c) * world);
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (g) {
        sink->BeginFigure(c, D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 1; i <= 36; ++i) {  // two and a half turns
            const float u = i / 36.0f;
            const float a = u * kPi * 5.0f;
            sink->AddLine(D2D1::Point2F(c.x + std::cos(a) * r * 0.82f * u, c.y + std::sin(a) * r * 0.82f * u));
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        rt->DrawGeometry(g.Get(), cache.Get(rt, ink), lw);
    }
    rt->SetTransform(world);
}

// Confused mouth: a little wavy line.
void WavyMouth(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F m, float halfW, float amp, float lw, D2D1_COLOR_F ink) {
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (!g) return;
    sink->BeginFigure(D2D1::Point2F(m.x - halfW, m.y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i <= 16; ++i) {
        const float u = i / 16.0f;
        sink->AddLine(D2D1::Point2F(m.x - halfW + 2.0f * halfW * u, m.y + std::sin(u * kPi * 3.0f) * amp));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    rt->DrawGeometry(g.Get(), cache.Get(rt, ink), lw);
}

// Error: a tear that wells at the eye's lower edge, rolls down the cheek and fades.
void Tear(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F eye, float eyeR, float s, double t) {
    constexpr float kCycle = 2.2f;
    const float ph = std::fmod(static_cast<float>(t), kCycle) / kCycle;
    if (ph < 0.35f) {
        const float grow = Smooth(ph / 0.35f);  // welling up
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(eye.x + eyeR * 0.5f, eye.y + eyeR * 0.9f), s * 0.018f * grow, s * 0.018f * grow),
                        cache.Get(rt, Hex(0x8FD3FF, 0.9f)));
        return;
    }
    const float fall = (ph - 0.35f) / 0.65f;
    const D2D1_POINT_2F c = D2D1::Point2F(eye.x + eyeR * 0.5f + fall * s * 0.02f, eye.y + eyeR * 0.9f + fall * s * 0.16f);
    const float a = 0.9f * (1.0f - fall * fall);
    const float r = s * 0.02f;
    Triangle(rt, cache.Get(rt, Hex(0x8FD3FF, a)), D2D1::Point2F(c.x - r * 0.85f, c.y), D2D1::Point2F(c.x, c.y - r * 2.0f),
             D2D1::Point2F(c.x + r * 0.85f, c.y));
    rt->FillEllipse(D2D1::Ellipse(c, r, r), cache.Get(rt, Hex(0x8FD3FF, a)));
}

// Error: a small grey cloud drizzling above the head.
void RainCloud(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F head, float rx, float ry, float s, double t) {
    const float drift = std::sin(static_cast<float>(t) * 0.9f) * s * 0.02f;
    const D2D1_POINT_2F c = D2D1::Point2F(head.x + rx * 0.55f + drift, head.y - ry * 1.30f);
    const D2D1_COLOR_F grey = Hex(0x9AA0AE), dark = Hex(0x6E7383);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + s * 0.012f), s * 0.11f, s * 0.045f), cache.Get(rt, dark));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - s * 0.055f, c.y), s * 0.050f, s * 0.042f), cache.Get(rt, grey));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + s * 0.010f, c.y - s * 0.022f), s * 0.060f, s * 0.052f), cache.Get(rt, grey));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + s * 0.065f, c.y + s * 0.002f), s * 0.045f, s * 0.038f), cache.Get(rt, grey));
    const float lw = std::max(1.0f, s * 0.012f);
    for (int i = 0; i < 3; ++i) {
        const float ph = std::fmod(static_cast<float>(t) * 1.6f + i * 0.37f, 1.0f);
        const float x = c.x + (i - 1) * s * 0.05f - ph * s * 0.01f;
        const float y = c.y + s * 0.05f + ph * s * 0.10f;
        rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x - s * 0.006f, y + s * 0.025f),
                     cache.Get(rt, Hex(0x8FD3FF, 0.85f * (1.0f - ph))), lw);
    }
}

// Shared bite motion for every snack: raised to the mouth, then lowered a touch.
float SnackBite(double t) { return std::fabs(std::sin(static_cast<float>(t) * 3.5f)); }

// Bun: a carrot, leafy end out, a bite missing from the tip.
void Carrot(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, double t,
            const D2D1_MATRIX_3X2_F& world) {
    const float bite = SnackBite(t);
    const D2D1_POINT_2F c = D2D1::Point2F(mouth.x + s * 0.10f, mouth.y + s * 0.03f + bite * s * 0.02f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-30.0f - bite * 6.0f, c) * world);
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> body = NewPath(rt, &sink);
    if (body) {
        sink->BeginFigure(D2D1::Point2F(c.x - s * 0.10f, c.y), D2D1_FIGURE_BEGIN_FILLED);  // bitten tip
        sink->AddLine(D2D1::Point2F(c.x + s * 0.16f, c.y - s * 0.055f));
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + s * 0.19f, c.y - s * 0.03f), D2D1::Point2F(c.x + s * 0.19f, c.y + s * 0.03f),
                                            D2D1::Point2F(c.x + s * 0.16f, c.y + s * 0.055f)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(body.Get(), cache.Get(rt, Hex(0xF08A2C)));
    }
    for (int i = 0; i < 3; ++i) {
        const float x = c.x + s * (0.0f + i * 0.05f);
        rt->DrawLine(D2D1::Point2F(x, c.y - s * (0.015f + i * 0.012f)), D2D1::Point2F(x + s * 0.02f, c.y - s * (0.010f + i * 0.012f)),
                     cache.Get(rt, Hex(0xC9631A)), std::max(0.8f, s * 0.008f));
    }
    for (int i = -1; i <= 1; ++i) {
        const D2D1_POINT_2F leaf = D2D1::Point2F(c.x + s * 0.23f, c.y + i * s * 0.035f);
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(-30.0f - bite * 6.0f + i * 25.0f, c) * world);
        rt->FillEllipse(D2D1::Ellipse(leaf, s * 0.055f, s * 0.018f), cache.Get(rt, Hex(0x5DBB63)));
    }
    rt->SetTransform(world);
}

// Miso: a little fish, tail flicking.
void Fish(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, double t,
          const D2D1_MATRIX_3X2_F& world) {
    const float bite = SnackBite(t);
    const D2D1_POINT_2F c = D2D1::Point2F(mouth.x + s * 0.14f, mouth.y + s * 0.04f + bite * s * 0.02f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-20.0f - bite * 6.0f, c) * world);
    const D2D1_COLOR_F body = Hex(0x8DB7E8), dark = Hex(0x5B86BF);
    const float flick = std::sin(static_cast<float>(t) * 9.0f) * s * 0.015f;
    Triangle(rt, cache.Get(rt, dark), D2D1::Point2F(c.x + s * 0.08f, c.y),
             D2D1::Point2F(c.x + s * 0.16f, c.y - s * 0.05f + flick), D2D1::Point2F(c.x + s * 0.16f, c.y + s * 0.05f + flick));
    rt->FillEllipse(D2D1::Ellipse(c, s * 0.10f, s * 0.05f), cache.Get(rt, body));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - s * 0.055f, c.y - s * 0.01f), s * 0.011f, s * 0.011f), cache.Get(rt, Hex(0x1A1622)));
    rt->DrawLine(D2D1::Point2F(c.x - s * 0.02f, c.y - s * 0.035f), D2D1::Point2F(c.x - s * 0.02f, c.y + s * 0.035f),
                 cache.Get(rt, dark), std::max(0.8f, s * 0.008f));
    rt->SetTransform(world);
}

// Biscuit: a classic dog bone, held crosswise.
void Bone(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, double t,
          const D2D1_MATRIX_3X2_F& world) {
    const float bite = SnackBite(t);
    const D2D1_POINT_2F c = D2D1::Point2F(mouth.x + s * 0.03f, mouth.y + s * 0.10f + bite * s * 0.02f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-8.0f + std::sin(static_cast<float>(t) * 2.0f) * 4.0f, c) * world);
    const D2D1_COLOR_F bone = Hex(0xFFF6E6), shade = Hex(0xE2D3BC);
    const float half = s * 0.17f;
    rt->FillRectangle(D2D1::RectF(c.x - half, c.y - s * 0.022f, c.x + half, c.y + s * 0.022f), cache.Get(rt, bone));
    for (int side = -1; side <= 1; side += 2)
        for (int k = -1; k <= 1; k += 2) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + side * half, c.y + k * s * 0.026f + s * 0.004f), s * 0.032f, s * 0.032f),
                            cache.Get(rt, shade));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + side * half, c.y + k * s * 0.026f), s * 0.030f, s * 0.030f), cache.Get(rt, bone));
        }
    rt->SetTransform(world);
}

// Bao: a bamboo stalk with a couple of leaves.
void Bamboo(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, double t,
            const D2D1_MATRIX_3X2_F& world) {
    const float bite = SnackBite(t);
    const D2D1_POINT_2F c = D2D1::Point2F(mouth.x + s * 0.02f, mouth.y + s * 0.05f + bite * s * 0.02f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-12.0f - bite * 5.0f, c) * world);
    const D2D1_COLOR_F stalk = Hex(0x7CC46A), joint = Hex(0x4E9440), leaf = Hex(0x9AD98A);
    const float len = s * 0.32f, w = s * 0.026f;
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x, c.y - w, c.x + len, c.y + w), w, w), cache.Get(rt, stalk));
    for (int i = 1; i <= 2; ++i) {
        const float x = c.x + len * i / 3.0f;
        rt->DrawLine(D2D1::Point2F(x, c.y - w), D2D1::Point2F(x, c.y + w), cache.Get(rt, joint), std::max(1.0f, s * 0.012f));
    }
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-40.0f - bite * 5.0f, c) * world);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + len * 0.70f, c.y - s * 0.05f), s * 0.06f, s * 0.018f), cache.Get(rt, leaf));
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(15.0f - bite * 5.0f, c) * world);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + len * 0.55f, c.y + s * 0.05f), s * 0.05f, s * 0.016f), cache.Get(rt, leaf));
    rt->SetTransform(world);
}

// Bolt: the visor becomes a tiny terminal. A title bar with three dots, output
// lines that scroll up smoothly, and a "$" prompt typing a command with a
// blinking cursor; when the command "runs" its output scrolls in and a new one starts.
void TerminalVisor(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_RECT_F visor, float s, double t) {
    const D2D1_COLOR_F green = Hex(0x4CF08A), dim = Hex(0x4CF08A, 0.42f), faint = Hex(0x4CF08A, 0.18f);
    const float pad = s * 0.03f;
    const float barH = std::max(1.0f, s * 0.016f);
    const float lineH = s * 0.040f;
    rt->FillRectangle(visor, cache.Get(rt, Hex(0x0C1A12, 0.85f)));
    rt->PushAxisAlignedClip(visor, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Title bar
    const float titleH = s * 0.032f;
    rt->FillRectangle(D2D1::RectF(visor.left, visor.top, visor.right, visor.top + titleH), cache.Get(rt, faint));
    for (int i = 0; i < 3; ++i)
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(visor.left + pad + i * s * 0.025f, visor.top + titleH * 0.5f), s * 0.007f, s * 0.007f),
                        cache.Get(rt, i == 0 ? Hex(0xFF7A7A) : i == 1 ? Hex(0xFFD36B) : green));

    // Each 1.6 s: type a command (first 1.1 s), then its output scrolls in.
    constexpr double kCmd = 1.6;
    const int cmd = static_cast<int>(t / kCmd);
    const float phase = static_cast<float>(std::fmod(t, kCmd) / kCmd);
    const float scrollFrac = phase > 0.7f ? Smooth((phase - 0.7f) / 0.3f) : 0.0f;
    auto lengthOf = [](int n) { return 0.25f + 0.65f * static_cast<float>((n * 7919 + 13) % 97) / 96.0f; };

    const float bodyTop = visor.top + titleH + pad;
    const float promptY = visor.bottom - pad - lineH * 0.6f;
    const int rows = std::max(1, static_cast<int>((promptY - bodyTop) / lineH));
    const float width = visor.right - visor.left - pad * 2.0f;
    for (int r = 0; r <= rows; ++r) {
        const float y = promptY - (rows - r + 1) * lineH - scrollFrac * lineH;
        if (y < bodyTop - lineH) continue;
        const int n = cmd * 3 + r;
        rt->FillRectangle(D2D1::RectF(visor.left + pad, y, visor.left + pad + width * lengthOf(n), y + barH),
                          cache.Get(rt, n % 4 == 0 ? green : dim));
    }

    // Prompt line: "$" then the command typing out, cursor blinking at its end.
    const float px = visor.left + pad;
    rt->DrawLine(D2D1::Point2F(px, promptY - barH), D2D1::Point2F(px + s * 0.012f, promptY + barH * 0.5f), cache.Get(rt, green), barH);
    rt->DrawLine(D2D1::Point2F(px + s * 0.012f, promptY + barH * 0.5f), D2D1::Point2F(px, promptY + barH * 2.0f), cache.Get(rt, green), barH);
    const float typed = std::min(1.0f, phase / 0.68f) * width * 0.55f * lengthOf(cmd + 50);
    const float cmdLeft = px + s * 0.03f;
    if (typed > 0.5f)
        rt->FillRectangle(D2D1::RectF(cmdLeft, promptY, cmdLeft + typed, promptY + barH), cache.Get(rt, green));
    if (phase > 0.68f || std::fmod(t, 0.5) < 0.3)
        rt->FillRectangle(D2D1::RectF(cmdLeft + typed + s * 0.006f, promptY - barH * 1.4f, cmdLeft + typed + s * 0.022f, promptY + barH * 1.4f),
                          cache.Get(rt, green));
    rt->PopAxisAlignedClip();
}

// ── Do Not Disturb: a purple rounded tile with a crescent moon ─────────────
void DndMoonBadge(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float s, double t, float lw) {
    const float h = s * 0.10f;  // half the badge
    // A slow breath: it eases a little larger and back every 3 s.
    const float k = 1.0f + 0.035f * std::sin(static_cast<float>(t) * 2.1f);
    const float hk = h * k;
    // Soft shadow, then the purple tile with a lighter top edge.
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - hk, c.y - hk + h * 0.12f, c.x + hk, c.y + hk + h * 0.12f), hk * 0.42f, hk * 0.42f),
                             cache.Get(rt, Hex(0x000000, 0.25f)));
    const D2D1_ROUNDED_RECT box = D2D1::RoundedRect(D2D1::RectF(c.x - hk, c.y - hk, c.x + hk, c.y + hk), hk * 0.42f, hk * 0.42f);
    rt->FillRoundedRectangle(box, cache.Get(rt, Hex(0x8B5CF6)));
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - hk, c.y - hk, c.x + hk, c.y - hk * 0.2f), hk * 0.42f, hk * 0.42f),
                             cache.Get(rt, Hex(0xA78BFA, 0.55f)));
    rt->DrawRoundedRectangle(box, cache.Get(rt, Hex(0x3B1F7A)), std::max(0.8f, lw * 0.7f));
    // A crescent moon, the usual sign for Do Not Disturb: it stays readable at
    // island size, where a struck-through bell is only a few pixels wide.
    // A small star twinkles beside it.
    ID2D1Brush* white = cache.Get(rt, Hex(0xFFFFFF));
    const D2D1_POINT_2F mc = D2D1::Point2F(c.x - h * 0.10f, c.y + h * 0.05f);
    rt->FillEllipse(D2D1::Ellipse(mc, h * 0.56f, h * 0.56f), white);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mc.x + h * 0.30f, mc.y - h * 0.22f), h * 0.46f, h * 0.46f), cache.Get(rt, Hex(0x8B5CF6)));
    const float tw = 0.55f + 0.45f * std::sin(static_cast<float>(t) * 2.6f);
    const D2D1_POINT_2F star = D2D1::Point2F(c.x + h * 0.48f, c.y - h * 0.42f);
    ID2D1Brush* sb = cache.Get(rt, Hex(0xFFFFFF, 0.5f + 0.5f * tw));
    const float sr = h * (0.13f + 0.05f * tw), sw = std::max(0.8f, h * 0.08f);
    rt->DrawLine(D2D1::Point2F(star.x - sr, star.y), D2D1::Point2F(star.x + sr, star.y), sb, sw);
    rt->DrawLine(D2D1::Point2F(star.x, star.y - sr), D2D1::Point2F(star.x, star.y + sr), sb, sw);
}

// ── Live: a dark REC badge with a pulsing red dot and broadcast waves ───────
void RecBadge(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float s, double t, float lw) {
    const float h = s * 0.085f;
    const D2D1_ROUNDED_RECT box = D2D1::RoundedRect(D2D1::RectF(c.x - h, c.y - h, c.x + h, c.y + h), h * 0.40f, h * 0.40f);
    rt->FillRoundedRectangle(box, cache.Get(rt, Hex(0x2B2933)));
    rt->DrawRoundedRectangle(box, cache.Get(rt, Hex(0x0F0E13)), std::max(0.8f, lw * 0.8f));
    // Viewfinder corners.
    ID2D1Brush* white = cache.Get(rt, Hex(0xFFFFFF, 0.9f));
    const float o = h * 0.62f, len = h * 0.30f, w = std::max(0.8f, h * 0.10f);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            const D2D1_POINT_2F p = D2D1::Point2F(c.x + sx * o, c.y + sy * o);
            rt->DrawLine(p, D2D1::Point2F(p.x - sx * len, p.y), white, w);
            rt->DrawLine(p, D2D1::Point2F(p.x, p.y - sy * len), white, w);
        }
    // The dot breathes about once a second, with a soft glow at its brightest.
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(t) * 5.5f);
    rt->FillEllipse(D2D1::Ellipse(c, h * (0.42f + 0.12f * pulse), h * (0.42f + 0.12f * pulse)), cache.Get(rt, Hex(0xFF3B30, 0.25f * pulse)));
    rt->FillEllipse(D2D1::Ellipse(c, h * 0.34f, h * 0.34f), cache.Get(rt, Hex(0xFF3B30, 0.75f + 0.25f * pulse)));
    // Broadcast waves ripple out on the free side, one after the other.
    for (int i = 0; i < 2; ++i) {
        const float ph = std::fmod(static_cast<float>(t) * 0.9f + i * 0.5f, 1.0f);
        const float r = h * (1.25f + ph * 0.9f);
        ID2D1Brush* wave = cache.Get(rt, Hex(0xFF3B30, (1.0f - ph) * 0.85f));
        ComPtr<ID2D1GeometrySink> sink;
        if (ComPtr<ID2D1PathGeometry> arc = NewPath(rt, &sink)) {
            sink->BeginFigure(D2D1::Point2F(c.x + r * 0.6f, c.y - r * 0.8f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(c.x + r * 0.6f, c.y + r * 0.8f), D2D1::SizeF(r, r), 0.0f,
                                          D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            rt->DrawGeometry(arc.Get(), wave, std::max(0.9f, h * 0.16f));
        }
    }
}

// ── Streaming: a striped popcorn bucket by the chin, a kernel on its way up ──
void Popcorn(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F headC, float headRx, float headRy, float s, double t,
             const D2D1_MATRIX_3X2_F& world) {
    rt->SetTransform(world);
    const float bite = SnackBite(t);
    // The bucket: wider at the top, sitting low on the right of the face.
    const float w = s * 0.21f, h = s * 0.23f;
    const D2D1_POINT_2F top = D2D1::Point2F(headC.x + headRx * 0.58f, headC.y + headRy * 0.55f);
    const float topHalf = w * 0.5f, bottomHalf = w * 0.36f;
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> bucket = NewPath(rt, &sink);
    if (bucket) {
        sink->BeginFigure(D2D1::Point2F(top.x - topHalf, top.y), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(top.x + topHalf, top.y));
        sink->AddLine(D2D1::Point2F(top.x + bottomHalf, top.y + h));
        sink->AddLine(D2D1::Point2F(top.x - bottomHalf, top.y + h));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
    }
    // Popcorn heaped over the rim, drawn first so the bucket's rim sits in front.
    const D2D1_COLOR_F corn = Hex(0xFFF3C4), cornShade = Hex(0xF2D27A);
    const float r = s * 0.038f;
    const float puffs[][2] = {{-0.36f, 0.10f}, {-0.12f, -0.20f}, {0.14f, -0.12f}, {0.38f, 0.06f}, {0.02f, 0.18f}, {-0.24f, -0.02f}, {0.26f, -0.30f}};
    for (const auto& p : puffs) {
        const D2D1_POINT_2F pc = D2D1::Point2F(top.x + p[0] * w, top.y - r * 0.4f + p[1] * w);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pc.x, pc.y + r * 0.25f), r, r), cache.Get(rt, cornShade));
        rt->FillEllipse(D2D1::Ellipse(pc, r * 0.9f, r * 0.9f), cache.Get(rt, corn));
    }
    if (bucket) {
        rt->FillGeometry(bucket.Get(), cache.Get(rt, Hex(0xFFFFFF)));
        // Red stripes, clipped to the bucket by drawing them as narrow trapezoids.
        for (int i = 0; i < 3; ++i) {
            const float u0 = -1.0f + (0.18f + i * 0.66f), u1 = u0 + 0.30f;
            ComPtr<ID2D1GeometrySink> ss;
            ComPtr<ID2D1PathGeometry> stripe = NewPath(rt, &ss);
            if (!stripe) continue;
            ss->BeginFigure(D2D1::Point2F(top.x + u0 * topHalf, top.y), D2D1_FIGURE_BEGIN_FILLED);
            ss->AddLine(D2D1::Point2F(top.x + u1 * topHalf, top.y));
            ss->AddLine(D2D1::Point2F(top.x + u1 * bottomHalf, top.y + h));
            ss->AddLine(D2D1::Point2F(top.x + u0 * bottomHalf, top.y + h));
            ss->EndFigure(D2D1_FIGURE_END_CLOSED);
            ss->Close();
            rt->FillGeometry(stripe.Get(), cache.Get(rt, Hex(0xE5322D)));
        }
        rt->DrawGeometry(bucket.Get(), cache.Get(rt, Hex(0x7A1F1C)), std::max(0.8f, s * 0.010f));
        rt->FillRectangle(D2D1::RectF(top.x - topHalf, top.y - s * 0.006f, top.x + topHalf, top.y + s * 0.012f), cache.Get(rt, Hex(0xC9231F)));
    }
    // One kernel rises from the bucket to the mouth on each bite.
    const D2D1_POINT_2F mouth = D2D1::Point2F(headC.x + headRx * 0.10f, headC.y + headRy * 0.42f);
    const float k = 1.0f - bite;  // 0 at the mouth, 1 in the bucket
    const D2D1_POINT_2F kc = D2D1::Point2F(mouth.x + (top.x - mouth.x) * k, mouth.y + (top.y - r - mouth.y) * k - std::sin(k * kPi) * s * 0.05f);
    if (bite < 0.93f) rt->FillEllipse(D2D1::Ellipse(kc, r * 0.75f, r * 0.75f), cache.Get(rt, corn));
}

// ── Snack (do not disturb): a cartoon drumstick held up to the mouth ──────
void Drumstick(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, double t,
               const D2D1_MATRIX_3X2_F& world) {
    // Raised to the mouth on each bite, then lowered a touch.
    const float bite = std::fabs(std::sin(static_cast<float>(t) * 3.5f));
    const D2D1_POINT_2F c = D2D1::Point2F(mouth.x + s * 0.13f, mouth.y + s * 0.02f + bite * s * 0.025f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-35.0f - bite * 8.0f, c) * world);
    const D2D1_COLOR_F bone = Hex(0xFFF6E6), boneShade = Hex(0xE6D6BE);
    const D2D1_COLOR_F meat = Hex(0xB5532E), meatLight = Hex(0xD9783F), meatDark = Hex(0x8A3A1E);
    // Bone sticks out to the side, with a knobbly end.
    const float bx = c.x + s * 0.17f;
    rt->FillRectangle(D2D1::RectF(c.x, c.y - s * 0.018f, bx, c.y + s * 0.018f), cache.Get(rt, bone));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx, c.y - s * 0.022f), s * 0.026f, s * 0.026f), cache.Get(rt, boneShade));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx, c.y + s * 0.022f), s * 0.026f, s * 0.026f), cache.Get(rt, boneShade));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx - s * 0.004f, c.y - s * 0.020f), s * 0.021f, s * 0.021f), cache.Get(rt, bone));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx - s * 0.004f, c.y + s * 0.020f), s * 0.021f, s * 0.021f), cache.Get(rt, bone));
    // The meat: a big round lump with a bite taken out.
    const D2D1_POINT_2F m = D2D1::Point2F(c.x - s * 0.05f, c.y);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(m.x, m.y + s * 0.008f), s * 0.105f, s * 0.085f), cache.Get(rt, meatDark));
    rt->FillEllipse(D2D1::Ellipse(m, s * 0.10f, s * 0.08f), cache.Get(rt, meat));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(m.x - s * 0.02f, m.y - s * 0.03f), s * 0.05f, s * 0.025f), cache.Get(rt, meatLight));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(m.x - s * 0.095f, m.y + s * 0.01f), s * 0.03f, s * 0.03f), cache.Get(rt, Hex(0xF3D9B8)));
    rt->SetTransform(world);
    // Crumbs fly off now and then.
    const float ph = std::fmod(static_cast<float>(t) * 1.4f, 1.0f);
    if (ph < 0.6f) {
        const float a = 1.0f - ph / 0.6f;
        for (int i = 0; i < 2; ++i)
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mouth.x - s * (0.04f + i * 0.05f) - ph * s * 0.08f,
                                                        mouth.y + ph * s * (0.10f + i * 0.04f)),
                                          s * 0.012f, s * 0.012f), cache.Get(rt, Hex(0xD9783F, a)));
    }
}

// Charging: a small plug held up by the cheek, its cable curling away, and a
// spark that flickers at the prongs.
void ChargingPlug(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float s, double t, const D2D1_MATRIX_3X2_F& world) {
    s *= 1.3f;  // drawn a little larger than its geometry so it reads on the island
    const float bob = std::sin(static_cast<float>(t) * 2.0f) * s * 0.008f;
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-28.0f, c) * world);
    const float lw = std::max(1.0f, s * 0.018f);
    const D2D1_POINT_2F p = D2D1::Point2F(c.x, c.y + bob);
    // Cable first, so the plug sits over its end.
    ComPtr<ID2D1GeometrySink> sink;
    if (ComPtr<ID2D1PathGeometry> cable = NewPath(rt, &sink)) {
        sink->BeginFigure(D2D1::Point2F(p.x, p.y + s * 0.05f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(p.x + s * 0.02f, p.y + s * 0.16f), D2D1::Point2F(p.x - s * 0.10f, p.y + s * 0.12f),
                                            D2D1::Point2F(p.x - s * 0.08f, p.y + s * 0.24f)));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        rt->DrawGeometry(cable.Get(), cache.Get(rt, Hex(0x3A3744)), lw * 1.3f);
    }
    const D2D1_ROUNDED_RECT body = D2D1::RoundedRect(D2D1::RectF(p.x - s * 0.045f, p.y - s * 0.04f, p.x + s * 0.045f, p.y + s * 0.06f),
                                                     s * 0.02f, s * 0.02f);
    rt->FillRoundedRectangle(body, cache.Get(rt, Hex(0xF2F0F7)));
    rt->DrawRoundedRectangle(body, cache.Get(rt, Hex(0x5E5A6B)), lw * 0.7f);
    for (int side = -1; side <= 1; side += 2)
        rt->FillRectangle(D2D1::RectF(p.x + side * s * 0.02f - s * 0.008f, p.y - s * 0.08f, p.x + side * s * 0.02f + s * 0.008f, p.y - s * 0.04f),
                          cache.Get(rt, Hex(0xC9A54A)));
    rt->SetTransform(world);
    // Spark: on for a beat every ~1.2 s.
    const float ph = std::fmod(static_cast<float>(t), 1.2f) / 1.2f;
    if (ph < 0.25f) {
        const float a = 1.0f - ph / 0.25f;
        const D2D1_POINT_2F k = D2D1::Point2F(c.x + s * 0.05f, c.y - s * 0.10f);
        ID2D1Brush* y = cache.Get(rt, Hex(0xFFD84A, a));
        rt->DrawLine(D2D1::Point2F(k.x, k.y), D2D1::Point2F(k.x - s * 0.025f, k.y + s * 0.03f), y, lw);
        rt->DrawLine(D2D1::Point2F(k.x - s * 0.025f, k.y + s * 0.03f), D2D1::Point2F(k.x + s * 0.005f, k.y + s * 0.03f), y, lw);
        rt->DrawLine(D2D1::Point2F(k.x + s * 0.005f, k.y + s * 0.03f), D2D1::Point2F(k.x - s * 0.02f, k.y + s * 0.065f), y, lw);
    }
}

// Bolt's snack: a battery he's charging from, its level filling up bar by bar.
void Battery(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, double t,
             const D2D1_MATRIX_3X2_F& world) {
    const float bite = SnackBite(t);
    const D2D1_POINT_2F c = D2D1::Point2F(mouth.x + s * 0.12f, mouth.y + s * 0.03f + bite * s * 0.02f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-20.0f - bite * 6.0f, c) * world);
    const float w = s * 0.11f, h = s * 0.055f, lw = std::max(1.0f, s * 0.014f);
    const D2D1_ROUNDED_RECT body = D2D1::RoundedRect(D2D1::RectF(c.x - w, c.y - h, c.x + w, c.y + h), s * 0.015f, s * 0.015f);
    rt->FillRoundedRectangle(body, cache.Get(rt, Hex(0x2D2B36)));
    rt->FillRectangle(D2D1::RectF(c.x + w, c.y - h * 0.4f, c.x + w + s * 0.022f, c.y + h * 0.4f), cache.Get(rt, Hex(0x8A8796)));
    const int bars = 1 + static_cast<int>(std::fmod(static_cast<float>(t) * 1.5f, 4.0f));
    const float bw = (w * 2.0f - s * 0.03f) / 4.0f;
    for (int i = 0; i < bars; ++i)
        rt->FillRectangle(D2D1::RectF(c.x - w + s * 0.015f + i * bw + s * 0.004f, c.y - h + s * 0.015f,
                                      c.x - w + s * 0.015f + (i + 1) * bw - s * 0.004f, c.y + h - s * 0.015f),
                          cache.Get(rt, Hex(0x4CF08A)));
    rt->DrawRoundedRectangle(body, cache.Get(rt, Hex(0x8A8796)), lw);
    rt->SetTransform(world);
}

// Spidey's snack: a pizza slice, tip first, crust out, with pepperoni.
void Pizza(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, double t,
           const D2D1_MATRIX_3X2_F& world) {
    const float bite = SnackBite(t);
    const D2D1_POINT_2F c = D2D1::Point2F(mouth.x + s * 0.10f, mouth.y + s * 0.01f + bite * s * 0.025f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-18.0f - bite * 7.0f, c) * world);
    const float tipX = c.x - s * 0.09f, crustX = c.x + s * 0.13f, half = s * 0.085f;
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> slice = NewPath(rt, &sink);
    if (slice) {
        // The tip is already bitten off: a scalloped edge instead of a point.
        sink->BeginFigure(D2D1::Point2F(tipX, c.y - half * 0.20f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(crustX, c.y - half));
        sink->AddLine(D2D1::Point2F(crustX, c.y + half));
        sink->AddLine(D2D1::Point2F(tipX, c.y + half * 0.20f));
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(tipX + s * 0.025f, c.y),
                                                              D2D1::Point2F(tipX, c.y - half * 0.20f)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(slice.Get(), cache.Get(rt, Hex(0xFFCB4F)));
        rt->DrawGeometry(slice.Get(), cache.Get(rt, Hex(0xE09A2E)), std::max(0.8f, s * 0.010f));
    }
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(crustX - s * 0.02f, c.y - half * 1.08f, crustX + s * 0.025f, c.y + half * 1.08f),
                                               s * 0.02f, s * 0.02f), cache.Get(rt, Hex(0xC9803A)));
    for (const auto& p : {D2D1::Point2F(0.055f, -0.035f), D2D1::Point2F(0.075f, 0.040f), D2D1::Point2F(0.0f, 0.008f)})
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + s * p.x, c.y + s * p.y), s * 0.022f, s * 0.022f), cache.Get(rt, Hex(0xC8372D)));
    // A cheese drip that stretches and shrinks with each bite.
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - s * 0.02f, c.y + half * 0.62f + bite * s * 0.012f), s * 0.010f, s * (0.014f + bite * 0.012f)),
                    cache.Get(rt, Hex(0xFFCB4F)));
    rt->SetTransform(world);
}

// Drinking: a glass of water tipped up to the mouth. It tilts further as it empties,
// the water's surface stays level, and a drop or two falls from the rim.
void WaterGlass(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F mouth, float s, float since,
                const D2D1_MATRIX_3X2_F& world) {
    const float p = std::clamp(since / 2.4f, 0.0f, 1.0f);           // how far through the drink
    const float raise = std::min(1.0f, since / 0.35f);                 // up to the mouth first
    const float sip = 0.5f + 0.5f * std::sin(since * 9.0f);            // little gulps
    // Degrees about the rim: upright below the mouth at first, then the base swings out
    // and up as it empties (a positive angle turns clockwise, so negative tips it to the right).
    const float tilt = -(70.0f * raise + 45.0f * p) - sip * 4.0f;
    const float hw = s * 0.095f, bw = s * 0.074f, h = s * 0.28f;      // half widths at the rim and base, height
    const D2D1_POINT_2F rim = D2D1::Point2F(mouth.x + s * 0.05f, mouth.y + s * 0.03f);
    const D2D1_POINT_2F c = D2D1::Point2F(rim.x, rim.y + h * 0.5f);
    const float top = rim.y, bottom = rim.y + h;
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(tilt, rim) * world);
    const float lw = std::max(0.8f, s * 0.012f);
    // The water: from the base up to a level that drops as it's drunk.
    const float level = 0.85f - 0.70f * p;
    const float wy = bottom - h * level;
    auto edgeAt = [&](float y) { const float k = (y - top) / h; return hw + (bw - hw) * k; };
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> water = NewPath(rt, &sink);
    if (water) {
        sink->BeginFigure(D2D1::Point2F(c.x - edgeAt(wy), wy), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(c.x + edgeAt(wy), wy));
        sink->AddLine(D2D1::Point2F(c.x + bw, bottom));
        sink->AddLine(D2D1::Point2F(c.x - bw, bottom));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(water.Get(), cache.Get(rt, Hex(0x5AC8FA, 0.85f)));
    }
    rt->DrawLine(D2D1::Point2F(c.x - edgeAt(wy), wy), D2D1::Point2F(c.x + edgeAt(wy), wy), cache.Get(rt, Hex(0xBFEAFF)), lw * 0.8f);
    // The glass: clear, with a bright outline and a glint.
    ComPtr<ID2D1PathGeometry> glass = NewPath(rt, &sink);
    if (glass) {
        sink->BeginFigure(D2D1::Point2F(c.x - hw, top), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(c.x + hw, top));
        sink->AddLine(D2D1::Point2F(c.x + bw, bottom));
        sink->AddLine(D2D1::Point2F(c.x - bw, bottom));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(glass.Get(), cache.Get(rt, Hex(0xFFFFFF, 0.14f)));
        rt->DrawGeometry(glass.Get(), cache.Get(rt, Hex(0xE6F6FF, 0.95f)), lw);
    }
    rt->DrawLine(D2D1::Point2F(c.x + hw * 0.45f, top + h * 0.15f), D2D1::Point2F(c.x + bw * 0.45f, bottom - h * 0.2f),
                 cache.Get(rt, Hex(0xFFFFFF, 0.55f)), lw * 0.8f);
    rt->SetTransform(world);
    // A drop falling from the rim now and then.
    const float d = std::fmod(since, 0.8f) / 0.8f;
    if (since > 0.4f && p < 0.95f)
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mouth.x - s * 0.01f, mouth.y + s * (0.05f + 0.16f * d)), s * 0.013f, s * 0.018f),
                        cache.Get(rt, Hex(0x5AC8FA, 0.9f * (1.0f - d))));
}

// Spidey's error: his web line has snapped. The upper half dangles limply from
// above (drawn in `frame`, which doesn't tilt with him) and a frayed stub is
// left on top of his head (drawn in `world`).
void SnappedWeb(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F head, float rx, float topY, float s, double t,
                const D2D1_MATRIX_3X2_F& frame, const D2D1_MATRIX_3X2_F& world) {
    const float w = std::max(1.0f, s * 0.016f);
    ID2D1Brush* silk = cache.Get(rt, Hex(0xE8ECF4, 0.9f));
    auto fray = [&](D2D1_POINT_2F p, float dir) {
        for (float a : {-0.6f, 0.0f, 0.6f})
            rt->DrawLine(p, D2D1::Point2F(p.x + std::sin(a) * s * 0.022f, p.y + dir * std::cos(a) * s * 0.022f), silk, w * 0.6f);
    };
    rt->SetTransform(frame);
    const float limp = std::sin(static_cast<float>(t) * 1.3f) * s * 0.02f;
    const D2D1_POINT_2F end = D2D1::Point2F(head.x - rx * 0.10f + limp, topY - s * 0.10f);
    Silk(rt, cache, D2D1::Point2F(head.x - rx * 0.30f, topY - s * 0.36f), end, s);
    fray(end, 1.0f);
    rt->SetTransform(world);
    const D2D1_POINT_2F stub = D2D1::Point2F(head.x, topY - s * 0.045f);
    rt->DrawLine(D2D1::Point2F(head.x, topY + s * 0.005f), stub, silk, w);
    fray(stub, -1.0f);
}

// Closed, filled path through `pts` (offsets in units of rx / ry from `c`).
ComPtr<ID2D1PathGeometry> Polygon(ID2D1RenderTarget* rt, D2D1_POINT_2F c, float rx, float ry,
                                  std::initializer_list<D2D1_POINT_2F> pts) {
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (!g) return nullptr;
    bool first = true;
    for (const D2D1_POINT_2F& p : pts) {
        const D2D1_POINT_2F q = D2D1::Point2F(c.x + p.x * rx, c.y + p.y * ry);
        if (first) sink->BeginFigure(q, D2D1_FIGURE_BEGIN_FILLED);
        else sink->AddLine(q);
        first = false;
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return g;
}

// Little "busy" ticks either side of something, flashing left and right in turn.
void ActionTicks(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float halfW, float s, double t) {
    const float flash = std::sin(static_cast<float>(t) * 9.0f);
    const float w = std::max(1.0f, s * 0.016f);
    for (int side = -1; side <= 1; side += 2) {
        const float a = std::clamp(side * flash * 1.5f + 0.35f, 0.0f, 1.0f);
        if (a <= 0.0f) continue;
        ID2D1Brush* b = cache.Get(rt, Hex(0xFFC233, a));
        const float x = c.x + side * halfW;
        for (int i = -1; i <= 1; ++i) {
            const D2D1_POINT_2F p0 = D2D1::Point2F(x + side * s * 0.015f, c.y + i * s * 0.045f);
            rt->DrawLine(p0, D2D1::Point2F(p0.x + side * s * 0.05f, p0.y + i * s * 0.018f), b, w);
        }
    }
}

// Working: the character peeks over an open laptop, typing away. The lid carries
// a logo per character (Spidey's spider, Luffy's straw-hat Jolly Roger, a little
// Bunbun head for the originals).
// `scale` shrinks it for the originals, whose faces sit lower in their slot.
void Laptop(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F top, float s, double t, Character who,
            const D2D1_MATRIX_3X2_F& world, float scale = 1.0f) {
    rt->SetTransform(world);
    s *= scale;
    const float w = s * 0.25f, h = s * 0.21f, lw = std::max(1.0f, s * 0.014f);
    const D2D1_COLOR_F ink = Hex(0x24202E);
    const D2D1_ROUNDED_RECT lid = D2D1::RoundedRect(D2D1::RectF(top.x - w, top.y, top.x + w, top.y + h), s * 0.03f, s * 0.03f);
    rt->FillRoundedRectangle(lid, cache.Get(rt, Hex(0xC6C2DC)));
    rt->FillRectangle(D2D1::RectF(top.x - w + s * 0.02f, top.y + s * 0.015f, top.x + w - s * 0.02f, top.y + s * 0.03f),
                      cache.Get(rt, Hex(0xDDDAEC)));  // a highlight along the top edge
    rt->DrawRoundedRectangle(lid, cache.Get(rt, ink), lw);
    const D2D1_ROUNDED_RECT base = D2D1::RoundedRect(D2D1::RectF(top.x - w * 1.12f, top.y + h, top.x + w * 1.12f, top.y + h + s * 0.03f),
                                                     s * 0.012f, s * 0.012f);
    rt->FillRoundedRectangle(base, cache.Get(rt, Hex(0x8F8AAB)));
    rt->DrawRoundedRectangle(base, cache.Get(rt, ink), lw);

    const D2D1_POINT_2F o = D2D1::Point2F(top.x, top.y + h * 0.52f);
    const float u = s * 0.042f;
    if (who == Character::Spiderman) {
        ID2D1Brush* black = cache.Get(rt, t_spidey.logo);  // the spider on the lid: black, white for Venom
        for (int side = -1; side <= 1; side += 2)
            for (int i = 0; i < 4; ++i) {
                const float y = o.y - u * 0.35f + i * u * 0.3f;
                const D2D1_POINT_2F knee = D2D1::Point2F(o.x + side * u * 0.9f, y - u * (0.55f - i * 0.25f));
                rt->DrawLine(D2D1::Point2F(o.x, y), knee, black, u * 0.16f);
                rt->DrawLine(knee, D2D1::Point2F(knee.x + side * u * 0.35f, knee.y + u * (0.5f + i * 0.2f)), black, u * 0.16f);
            }
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y + u * 0.2f), u * 0.30f, u * 0.6f), black);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y - u * 0.55f), u * 0.24f, u * 0.24f), black);
    } else if (who != Character::Luffy) {
        // The Bunbun logo: a bunny head with two long ears.
        ID2D1Brush* white = cache.Get(rt, Hex(0xFFFFFF));
        for (int side = -1; side <= 1; side += 2) {
            const D2D1_POINT_2F e = D2D1::Point2F(o.x + side * u * 0.32f, o.y - u * 0.62f);
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(side * 12.0f, D2D1::Point2F(e.x, e.y + u * 0.4f)) * world);
            rt->FillEllipse(D2D1::Ellipse(e, u * 0.2f, u * 0.55f), white);
        }
        rt->SetTransform(world);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y + u * 0.25f), u * 0.68f, u * 0.56f), white);
    } else {
        // Jolly Roger: crossbones, skull, and a tiny straw hat on top.
        ID2D1Brush* bone = cache.Get(rt, Hex(0xFFFFFF));
        ID2D1Brush* dark = cache.Get(rt, ink);
        for (int side = -1; side <= 1; side += 2) {
            const D2D1_POINT_2F a = D2D1::Point2F(o.x - side * u * 1.25f, o.y - u * 0.35f);
            const D2D1_POINT_2F b = D2D1::Point2F(o.x + side * u * 1.25f, o.y + u * 1.0f);
            rt->DrawLine(a, b, dark, u * 0.5f);
            rt->DrawLine(a, b, bone, u * 0.28f);
            for (const D2D1_POINT_2F& p : {a, b}) rt->FillEllipse(D2D1::Ellipse(p, u * 0.2f, u * 0.2f), bone);
        }
        const D2D1_POINT_2F sk = D2D1::Point2F(o.x, o.y + u * 0.1f);
        rt->FillEllipse(D2D1::Ellipse(sk, u * 0.66f, u * 0.6f), bone);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sk.x - u * 0.36f, sk.y + u * 0.2f, sk.x + u * 0.36f, sk.y + u * 0.78f),
                                                   u * 0.12f, u * 0.12f), bone);
        rt->DrawEllipse(D2D1::Ellipse(sk, u * 0.66f, u * 0.6f), dark, u * 0.08f);
        for (int side = -1; side <= 1; side += 2)
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sk.x + side * u * 0.26f, sk.y + u * 0.05f), u * 0.17f, u * 0.19f), dark);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sk.x, sk.y - u * 0.52f), u * 0.95f, u * 0.2f), cache.Get(rt, Hex(0xF1B43C)));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sk.x, sk.y - u * 0.72f), u * 0.5f, u * 0.32f), cache.Get(rt, Hex(0xF1B43C)));
        rt->DrawLine(D2D1::Point2F(sk.x - u * 0.48f, sk.y - u * 0.6f), D2D1::Point2F(sk.x + u * 0.48f, sk.y - u * 0.6f),
                     cache.Get(rt, Hex(0xD0302C)), u * 0.14f);
    }
    ActionTicks(rt, cache, D2D1::Point2F(top.x, top.y + h * 0.55f), w * 1.12f, s, t);
}

// ── Luffy ───────────────────────────────────────────────────────────────────
// A chibi straw-hat pirate: outlined skin, spiky black fringe under a straw hat
// with a red band, the stitched scar under his left eye and a huge grin. Working
// is his Gear 5 (a white cloud of hair, laughing) behind a laptop. The eyes are
// the shared ones, so they follow the cursor like every other mascot's.

const D2D1_COLOR_F kLuffySkin = {0.965f, 0.78f, 0.62f, 1.0f};
const D2D1_COLOR_F kLuffyInk = {0.17f, 0.11f, 0.08f, 1.0f};

void LuffyHead(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, Mood mood,
               double t, const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    const float lw = std::max(1.0f, s * 0.014f);
    const bool gear5 = mood == Mood::Working;
    const D2D1_COLOR_F hair = gear5 ? Hex(0xF7F5FF) : Hex(0x17141B);
    rt->SetTransform(world);

    if (gear5) {
        // Gear 5: a billowing white cloud of hair with a purple rim; the puffs
        // breathe at different rates so it looks alive.
        constexpr int kPuffs = 11;
        D2D1_ELLIPSE puffs[kPuffs];
        for (int i = 0; i < kPuffs; ++i) {
            const float a = (-125.0f + i * 25.0f) * (kPi / 180.0f);
            const float r = s * (0.11f + 0.025f * std::sin(static_cast<float>(t) * 3.2f + i * 1.7f)) * (i == 5 ? 1.25f : 1.0f);
            puffs[i] = D2D1::Ellipse(D2D1::Point2F(c.x + std::sin(a) * rx * 1.02f, c.y - ry * 0.18f - std::cos(a) * ry * 0.85f), r, r);
        }
        for (const D2D1_ELLIPSE& p : puffs)
            rt->FillEllipse(D2D1::Ellipse(p.point, p.radiusX + lw * 1.6f, p.radiusY + lw * 1.6f), brush(Hex(0x6B3FA0)));
        for (const D2D1_ELLIPSE& p : puffs) rt->FillEllipse(p, brush(hair));
        for (int i = 1; i < kPuffs; i += 2)  // curls
            rt->DrawEllipse(D2D1::Ellipse(puffs[i].point, puffs[i].radiusX * 0.45f, puffs[i].radiusY * 0.45f), brush(Hex(0xC9BEEA)), lw);
    } else {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y - ry * 0.22f), rx * 1.06f, ry * 0.9f), brush(hair));
        for (int side = -1; side <= 1; side += 2)
            if (auto tuft = Polygon(rt, c, rx, ry, {{side * 0.9f, -0.55f}, {side * 1.22f, -0.10f}, {side * 0.96f, -0.18f}}))
                rt->FillGeometry(tuft.Get(), brush(hair));
    }

    // Ears, then the face over them.
    for (int side = -1; side <= 1; side += 2) {
        const D2D1_ELLIPSE ear = D2D1::Ellipse(D2D1::Point2F(c.x + side * rx * 0.97f, c.y + ry * 0.12f), rx * 0.17f, ry * 0.24f);
        rt->FillEllipse(ear, brush(kLuffySkin));
        rt->DrawEllipse(ear, brush(kLuffyInk), lw);
        rt->DrawLine(D2D1::Point2F(ear.point.x - side * rx * 0.02f, ear.point.y - ry * 0.08f),
                     D2D1::Point2F(ear.point.x + side * rx * 0.03f, ear.point.y + ry * 0.08f), brush(Shade(kLuffySkin, 0.75f)), lw);
    }
    const D2D1_ELLIPSE face = D2D1::Ellipse(c, rx * 0.96f, ry);
    rt->FillEllipse(face, brush(kLuffySkin));
    rt->DrawEllipse(face, brush(kLuffyInk), lw);

    if (gear5) {
        // Gear 5: white hair covers the whole scalp (merging into the cloud
        // behind), with a back row of curls and a front row over the forehead.
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y - ry * 0.80f), rx * 0.98f, ry * 0.42f), brush(hair));
        for (int i = -1; i <= 1; i += 2)
            for (float x : {0.18f, 0.52f}) {
                const float r = s * (0.07f + 0.01f * std::sin(static_cast<float>(t) * 3.0f + x * 9.0f + i));
                const D2D1_POINT_2F p = D2D1::Point2F(c.x + i * x * rx, c.y - ry * (0.94f - 0.10f * x));
                rt->FillEllipse(D2D1::Ellipse(p, r + lw * 1.2f, r + lw * 1.2f), brush(Hex(0xD9D0F2)));
                rt->FillEllipse(D2D1::Ellipse(p, r, r), brush(hair));
            }
        for (int i = -2; i <= 2; ++i) {
            const float r = s * (0.075f + 0.012f * std::sin(static_cast<float>(t) * 3.6f + i));
            const D2D1_ELLIPSE curl = D2D1::Ellipse(D2D1::Point2F(c.x + i * rx * 0.34f, c.y - ry * (0.62f - 0.06f * (2 - std::abs(i)))), r, r);
            rt->FillEllipse(D2D1::Ellipse(curl.point, r + lw * 1.4f, r + lw * 1.4f), brush(Hex(0x6B3FA0)));
            rt->FillEllipse(curl, brush(hair));
            rt->DrawEllipse(D2D1::Ellipse(curl.point, r * 0.45f, r * 0.45f), brush(Hex(0xC9BEEA)), lw);
        }
    } else {
        // Spiky black fringe from under the brim down to the brows.
        if (auto fringe = Polygon(rt, c, rx, ry, {{-1.0f, -0.72f}, {-1.0f, -0.25f}, {-0.86f, 0.02f}, {-0.74f, -0.36f},
                                                  {-0.60f, -0.12f}, {-0.44f, -0.40f}, {-0.20f, -0.16f}, {-0.04f, -0.42f},
                                                  {0.12f, -0.18f}, {0.30f, -0.42f}, {0.56f, -0.14f}, {0.70f, -0.36f},
                                                  {0.86f, 0.02f}, {1.0f, -0.25f}, {1.0f, -0.72f}, {0.0f, -1.0f}}))
            rt->FillGeometry(fringe.Get(), brush(hair));
    }

    // The straw hat: it hops on Alert, tips back when happy and slips down a
    // little when things go wrong. In Gear 5 it perches on the cloud of hair,
    // jiggling as he laughs.
    float lift = 0.0f, tip = 0.0f;
    if (gear5) {
        lift = -s * (0.03f + 0.012f * std::fabs(std::sin(static_cast<float>(t) * 6.0f)));
        tip = 7.0f;
    }
    if (mood == Mood::Alert) lift = -std::fabs(std::sin(static_cast<float>(t) * 9.0f)) * s * 0.05f;
    if (mood == Mood::Happy) tip = -10.0f;
    if (mood == Mood::Sad) lift = s * 0.025f;
    const D2D1_POINT_2F hc = D2D1::Point2F(c.x, c.y - ry * 0.80f + lift);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(tip, hc) * world);
    const D2D1_COLOR_F straw = Hex(0xF1B43C), strawDark = Hex(0xC98A1F);
    const D2D1_ELLIPSE brim = D2D1::Ellipse(hc, rx * 1.32f, ry * 0.22f);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(hc.x, hc.y + ry * 0.04f), brim.radiusX, brim.radiusY), brush(strawDark));
    rt->FillEllipse(brim, brush(straw));
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(hc.x, hc.y + ry * 0.02f), brim.radiusX, brim.radiusY + ry * 0.02f), brush(kLuffyInk), lw);
    for (int i = -3; i <= 3; ++i) {  // straw weave on the brim
        if (i == 0) continue;
        const float x = hc.x + i * rx * 0.36f;
        rt->DrawLine(D2D1::Point2F(x, hc.y + ry * 0.08f), D2D1::Point2F(x + i * rx * 0.04f, hc.y + ry * 0.16f), brush(strawDark), lw * 0.8f);
    }
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> crown = NewPath(rt, &sink);
    if (crown) {
        const float cw = rx * 0.70f, top = hc.y - ry * 0.66f, bottom = hc.y + ry * 0.02f;
        sink->BeginFigure(D2D1::Point2F(hc.x - cw, bottom), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(hc.x - cw, top + ry * 0.10f), D2D1::Point2F(hc.x - cw * 0.55f, top),
                                            D2D1::Point2F(hc.x, top)));
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(hc.x + cw * 0.55f, top), D2D1::Point2F(hc.x + cw, top + ry * 0.10f),
                                            D2D1::Point2F(hc.x + cw, bottom)));
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(hc.x, bottom + ry * 0.10f), D2D1::Point2F(hc.x - cw, bottom)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(crown.Get(), brush(straw));
        ComPtr<ID2D1Layer> layer;
        if (SUCCEEDED(rt->CreateLayer(&layer))) {
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), crown.Get()), layer.Get());
            rt->FillRectangle(D2D1::RectF(hc.x - cw, hc.y - ry * 0.24f, hc.x + cw, hc.y + ry * 0.2f), brush(Hex(0xD0302C)));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(hc.x - cw * 0.35f, top + ry * 0.18f), cw * 0.32f, ry * 0.10f),
                            brush(Hex(0xFFD97A, 0.7f)));  // sheen
            rt->PopLayer();
        }
        rt->DrawGeometry(crown.Get(), brush(kLuffyInk), lw);
    }
    rt->SetTransform(world);
}

// ── Chibi face pieces ───────────────────────────────────────────────────────
// Outlined-anime bits shared by Luffy and Dazai. `ink` is the outline colour,
// `skin` the hand colour.

const D2D1_COLOR_F kMouthDark = {0.43f, 0.10f, 0.12f, 1.0f};
const D2D1_COLOR_F kTongue = {0.94f, 0.49f, 0.53f, 1.0f};

// A fist; `bandaged` wraps the wrist below it (Dazai).
void Fist(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F p, float rx, float ry, float lw, D2D1_COLOR_F skin,
          D2D1_COLOR_F ink, bool bandaged) {
    if (bandaged) {
        const D2D1_ROUNDED_RECT wrap = D2D1::RoundedRect(D2D1::RectF(p.x - rx * 0.15f, p.y + ry * 0.08f, p.x + rx * 0.15f, p.y + ry * 0.36f),
                                                         rx * 0.04f, rx * 0.04f);
        rt->FillRoundedRectangle(wrap, cache.Get(rt, Hex(0xF6F4F0)));
        rt->DrawRoundedRectangle(wrap, cache.Get(rt, ink), lw * 0.8f);
        for (float k : {0.17f, 0.26f})
            rt->DrawLine(D2D1::Point2F(p.x - rx * 0.15f, p.y + ry * k), D2D1::Point2F(p.x + rx * 0.15f, p.y + ry * (k + 0.04f)),
                         cache.Get(rt, Hex(0xBDB6AC)), lw * 0.7f);
    }
    const D2D1_ELLIPSE f = D2D1::Ellipse(p, rx * 0.20f, ry * 0.17f);
    rt->FillEllipse(f, cache.Get(rt, skin));
    rt->DrawEllipse(f, cache.Get(rt, ink), lw);
    for (float k : {-0.05f, 0.05f})
        rt->DrawLine(D2D1::Point2F(p.x + rx * 0.02f, p.y + ry * k), D2D1::Point2F(p.x + rx * 0.16f, p.y + ry * k), cache.Get(rt, ink), lw * 0.7f);
}

// Happy: a staged cheer, like the hand-drawn celebrations. Sparks pop out beside
// the head, then pop again as confetti bursts from above it and drifts down.
// The confetti stays close to the head, so the island's edges don't cut it off.
// Drawn twice: `behind` (before the head) for the confetti, so it never covers
// the face, then in front for the sparks.
void Celebrate(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F head, float rx, float ry, float s, float since,
               const D2D1_MATRIX_3X2_F& world, float lw, bool behind) {
    // Sparks: three yellow strokes each side, popping in at the start and again with the punch.
    auto pop = [](float x) { return x < 0.0f || x > 0.5f ? 0.0f : x < 0.1f ? Smooth(x / 0.1f) : 1.0f - Smooth((x - 0.3f) / 0.2f); };
    const float spark = std::max(pop(since - 0.08f), pop(since - 0.6f));
    if (!behind && spark > 0.0f) {
        for (int side = -1; side <= 1; side += 2)
            for (float deg : {-38.0f, -12.0f, 14.0f}) {
                const float a = deg * kPi / 180.0f;
                const float r0 = rx * 1.08f, r1 = r0 + s * 0.085f * spark;
                const D2D1_POINT_2F p0 = D2D1::Point2F(head.x + side * std::cos(a) * r0, head.y - ry * 0.35f + std::sin(a) * r0);
                const D2D1_POINT_2F p1 = D2D1::Point2F(head.x + side * std::cos(a) * r1, head.y - ry * 0.35f + std::sin(a) * r1);
                rt->DrawLine(p0, p1, cache.Get(rt, Hex(0x2A1F14, spark)), lw * 3.4f);
                rt->DrawLine(p0, p1, cache.Get(rt, Hex(0xFFC233, spark)), lw * 2.0f);
            }
    }

    // Confetti: launched with the punch, out to both sides and over the head,
    // falling with a little spin, gone by 2.2 s.
    const float tau = since - 0.6f;
    if (behind && tau > 0.0f && tau < 1.6f) {
        static const UINT32 kColors[] = {0x8B5CF6, 0xFFC233, 0xFF5FA2, 0xF4F1FF, 0x4FD1C5, 0xB069FF};
        const float fade = 1.0f - Smooth((tau - 1.2f) / 0.4f);
        for (int i = 0; i < 16; ++i) {
            const float h1 = Hash01(i * 7919u + 1), h2 = Hash01(i * 104729u + 7), h3 = Hash01(i * 1299709u + 3);
            // From the head's top corners, outward, so it clears the head as it falls.
            const float side = i % 2 ? 1.0f : -1.0f;
            // Air drag (k) slows the burst, so pieces stay within about half the
            // mascot's size either side and then flutter down at a gentle speed.
            const float k = 3.0f, drift = (1.0f - std::exp(-k * tau)) / k;
            const float vx = side * (0.3f + 0.7f * h1) * s, vy = -(0.6f + 0.9f * h2) * s, g = s * 3.0f;
            const D2D1_POINT_2F p = D2D1::Point2F(
                head.x + side * rx * 0.55f + vx * drift + std::sin(tau * 7.0f + h3 * 6.0f) * s * 0.02f,
                head.y - ry * 0.85f + vy * drift + g * (tau - drift) / k);
            const float w = s * (0.055f + 0.02f * h3), h = s * 0.038f;
            rt->SetTransform(D2D1::Matrix3x2F::Rotation((h3 * 360.0f) + tau * (h1 > 0.5f ? 420.0f : -380.0f), p) * world);
            const D2D1_RECT_F piece = D2D1::RectF(p.x - w * 0.5f, p.y - h * 0.5f, p.x + w * 0.5f, p.y + h * 0.5f);
            rt->FillRectangle(piece, cache.Get(rt, Hex(kColors[i % std::size(kColors)], fade)));
            rt->DrawRectangle(piece, cache.Get(rt, Hex(0x241A2E, fade * 0.55f)), lw * 0.45f);
        }
        rt->SetTransform(world);
    }

}

// Thinking: a hand on the chin, index finger tapping.
void ChinHand(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float lw, double t,
              D2D1_COLOR_F skin, D2D1_COLOR_F ink, bool bandaged) {
    const float tap = std::fabs(std::sin(static_cast<float>(t) * 4.0f)) * ry * 0.04f;
    const D2D1_POINT_2F fist = D2D1::Point2F(c.x + rx * 0.22f, c.y + ry * 1.0f);
    const D2D1_POINT_2F tipPt = D2D1::Point2F(c.x + rx * 0.06f, c.y + ry * 0.74f - tap);
    rt->DrawLine(fist, tipPt, cache.Get(rt, ink), rx * 0.13f + lw * 2.0f);
    rt->DrawLine(fist, tipPt, cache.Get(rt, skin), rx * 0.13f);
    Fist(rt, cache, fist, rx, ry, lw, skin, ink, bandaged);
}

// A short curved line for a mouth: `bend` > 0 smiles, < 0 frowns.
void MouthLine(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F m, float halfW, float bend, float lw, D2D1_COLOR_F ink) {
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (!g) return;
    sink->BeginFigure(D2D1::Point2F(m.x - halfW, m.y), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(m.x, m.y + bend), D2D1::Point2F(m.x + halfW, m.y)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    rt->DrawGeometry(g.Get(), cache.Get(rt, ink), lw * 1.2f);
}

// A small round open mouth with a tongue (surprised, or calling out).
void OMouth(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F m, float rx, float ry, float lw, D2D1_COLOR_F ink) {
    const D2D1_ELLIPSE o = D2D1::Ellipse(m, rx, ry);
    rt->FillEllipse(o, cache.Get(rt, kMouthDark));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(m.x, m.y + ry * 0.45f), rx * 0.6f, ry * 0.4f), cache.Get(rt, kTongue));
    rt->DrawEllipse(o, cache.Get(rt, ink), lw);
}

// Sleepy: mouth hanging open and a snot bubble that swells with each breath and pops.
void SnotBubble(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t, float lw,
                D2D1_COLOR_F ink) {
    OMouth(rt, cache, D2D1::Point2F(c.x - rx * 0.05f, c.y + ry * 0.60f), rx * 0.10f, ry * 0.11f, lw, ink);
    const float ph = std::fmod(static_cast<float>(t), 2.6f) / 2.6f;
    const float r = s * (ph < 0.9f ? 0.025f + 0.06f * Smooth(ph / 0.9f) : 0.085f * (1.0f - (ph - 0.9f) / 0.1f));
    const D2D1_POINT_2F b = D2D1::Point2F(c.x + rx * 0.10f + r * 0.8f, c.y + ry * 0.30f + r * 0.3f);
    rt->FillEllipse(D2D1::Ellipse(b, r, r), cache.Get(rt, Hex(0xDDEFFF, 0.8f)));
    rt->DrawEllipse(D2D1::Ellipse(b, r, r), cache.Get(rt, Hex(0x8CBDE6)), lw * 0.8f);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(b.x - r * 0.35f, b.y - r * 0.35f), r * 0.2f, r * 0.2f), cache.Get(rt, Hex(0xFFFFFF)));
}

// Error: a wobbling, wailing mouth, a plaster on the left cheek and gloom lines.
void Wail(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, double t, float lw, D2D1_COLOR_F ink,
          const D2D1_MATRIX_3X2_F& world) {
    const float tp = static_cast<float>(t);
    const float wob = std::sin(tp * 11.0f) * ry * 0.03f;
    ComPtr<ID2D1GeometrySink> sink;
    if (ComPtr<ID2D1PathGeometry> m = NewPath(rt, &sink)) {
        const float y = c.y + ry * 0.66f;
        sink->BeginFigure(D2D1::Point2F(c.x - rx * 0.24f, y + wob), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - rx * 0.20f, y - ry * 0.26f), D2D1::Point2F(c.x + rx * 0.20f, y - ry * 0.26f),
                                            D2D1::Point2F(c.x + rx * 0.24f, y - wob)));
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx * 0.10f, y - ry * 0.06f), D2D1::Point2F(c.x - rx * 0.10f, y + ry * 0.04f),
                                            D2D1::Point2F(c.x - rx * 0.24f, y + wob)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(m.Get(), cache.Get(rt, kMouthDark));
        rt->DrawGeometry(m.Get(), cache.Get(rt, ink), lw * 1.2f);
    }
    const D2D1_POINT_2F p = D2D1::Point2F(c.x - rx * 0.55f, c.y + ry * 0.45f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(-35.0f, p) * world);
    const D2D1_ROUNDED_RECT plaster = D2D1::RoundedRect(D2D1::RectF(p.x - rx * 0.16f, p.y - ry * 0.07f, p.x + rx * 0.16f, p.y + ry * 0.07f),
                                                        ry * 0.05f, ry * 0.05f);
    rt->FillRoundedRectangle(plaster, cache.Get(rt, Hex(0xE7B07F)));
    rt->DrawRoundedRectangle(plaster, cache.Get(rt, Hex(0xA8704F)), lw * 0.8f);
    rt->DrawLine(D2D1::Point2F(p.x - rx * 0.03f, p.y), D2D1::Point2F(p.x + rx * 0.03f, p.y), cache.Get(rt, Hex(0xA8704F)), lw * 0.8f);
    rt->SetTransform(world);
    for (int i = 0; i < 3; ++i) {
        const float a = 0.55f + 0.45f * std::sin(tp * 3.0f + i * 1.2f);
        const float x = c.x + rx * (0.98f + i * 0.13f), y0 = c.y - ry * (0.62f - i * 0.10f);
        rt->DrawLine(D2D1::Point2F(x, y0), D2D1::Point2F(x, y0 + ry * (0.55f - i * 0.12f)), cache.Get(rt, Hex(0xF4F1FA, a)), lw * 1.3f);
    }
}

// Luffy's face below the eyes: the scar, a mouth per mood and the extras the
// reference art gives each mood (snot bubble, thinking hand, plaster, gloom).
void LuffyFace(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, Mood mood,
               double t, const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    const float lw = std::max(1.0f, s * 0.014f);
    rt->SetTransform(world);

    // Stitched scar under his left eye (our right).
    const D2D1_POINT_2F sc = D2D1::Point2F(c.x + rx * 0.42f, c.y + ry * 0.33f);
    rt->DrawLine(D2D1::Point2F(sc.x - rx * 0.15f, sc.y + ry * 0.02f), D2D1::Point2F(sc.x + rx * 0.15f, sc.y - ry * 0.03f), brush(kLuffyInk), lw);
    for (float k : {-0.07f, 0.06f})
        rt->DrawLine(D2D1::Point2F(sc.x + rx * k, sc.y - ry * 0.07f), D2D1::Point2F(sc.x + rx * (k + 0.02f), sc.y + ry * 0.07f),
                     brush(kLuffyInk), lw * 0.8f);

    // A wide D-shaped mouth; `open` shows the inside (laughing) instead of teeth.
    auto bigMouth = [&](bool open, float width, float depth) {
        ComPtr<ID2D1GeometrySink> sink;
        ComPtr<ID2D1PathGeometry> m = NewPath(rt, &sink);
        if (!m) return;
        const float top = c.y + ry * 0.34f;
        sink->BeginFigure(D2D1::Point2F(c.x - rx * width, top - ry * 0.04f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(c.x, top + ry * 0.05f), D2D1::Point2F(c.x + rx * width, top - ry * 0.04f)));
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx * width * 0.9f, top + ry * depth * 0.8f),
                                            D2D1::Point2F(c.x + rx * width * 0.4f, top + ry * depth), D2D1::Point2F(c.x, top + ry * depth)));
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - rx * width * 0.4f, top + ry * depth),
                                            D2D1::Point2F(c.x - rx * width * 0.9f, top + ry * depth * 0.8f), D2D1::Point2F(c.x - rx * width, top - ry * 0.04f)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(m.Get(), brush(open ? kMouthDark : Hex(0xFFFFFF)));
        ComPtr<ID2D1Layer> layer;
        if (SUCCEEDED(rt->CreateLayer(&layer))) {
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), m.Get()), layer.Get());
            if (open) {
                rt->FillRectangle(D2D1::RectF(c.x - rx, top - ry * 0.1f, c.x + rx, top + ry * 0.11f), brush(Hex(0xFFFFFF)));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, top + ry * depth), rx * width * 0.6f, ry * depth * 0.42f), brush(kTongue));
            } else {
                const float mid = top + ry * depth * 0.45f;
                rt->DrawLine(D2D1::Point2F(c.x - rx, mid), D2D1::Point2F(c.x + rx, mid), brush(kLuffyInk), lw * 0.8f);
                for (float x : {-0.5f, -0.17f, 0.17f, 0.5f})
                    rt->DrawLine(D2D1::Point2F(c.x + rx * width * x, top), D2D1::Point2F(c.x + rx * width * x, mid), brush(kLuffyInk), lw * 0.7f);
            }
            rt->PopLayer();
        }
        rt->DrawGeometry(m.Get(), brush(kLuffyInk), lw * 1.2f);
    };

    switch (mood) {
        case Mood::Working: bigMouth(true, 0.46f, 0.52f + 0.06f * std::fabs(std::sin(static_cast<float>(t) * 6.0f))); break;  // laughing
        case Mood::Snack: bigMouth(true, 0.30f, 0.30f + 0.12f * SnackBite(t)); break;                                          // chomping
        case Mood::Alert: OMouth(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.56f), rx * 0.15f, ry * 0.17f, lw, kLuffyInk); break;
        case Mood::Thinking:
            MouthLine(rt, cache, D2D1::Point2F(c.x + rx * 0.04f, c.y + ry * 0.55f), rx * 0.15f, ry * 0.04f, lw, kLuffyInk);
            ChinHand(rt, cache, c, rx, ry, lw, t, kLuffySkin, kLuffyInk, false);
            break;
        case Mood::Sleepy:
            if (t_dnd) MouthLine(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.55f), rx * 0.12f, ry * 0.03f, lw, kLuffyInk);  // calm
            else SnotBubble(rt, cache, c, rx, ry, s, t, lw, kLuffyInk);
            break;
        case Mood::Sad: Wail(rt, cache, c, rx, ry, t, lw, kLuffyInk, world); break;
        case Mood::Talking: bigMouth(true, 0.34f, 0.16f + 0.32f * TalkOpen(t)); break;  // chattering away
        case Mood::Confused:
            WavyMouth(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.55f), rx * 0.16f, ry * 0.035f, lw * 1.2f, kLuffyInk);
            ChinHand(rt, cache, c, rx, ry, lw, t, kLuffySkin, kLuffyInk, false);  // scratching his chin
            break;
        default: bigMouth(false, 0.42f, 0.46f); break;  // the trademark grin
    }
    if (mood == Mood::Snack) ActionTicks(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.62f), rx * 1.05f, s, t);
}

// ── Dazai ───────────────────────────────────────────────────────────────────
// A chibi bandaged detective: a big mop of wavy dark-brown hair falling over
// half-lidded eyes, gauze on his cheek, bandaged neck and wrists, white shirt,
// bolo tie and the collar of a sand-coloured coat. Working is reading a book;
// needs-you is pointing straight at you. The eyes are the shared, cursor-
// following ones with a heavy lid drawn over them.

const D2D1_COLOR_F kDazaiSkin = {0.976f, 0.863f, 0.761f, 1.0f};
const D2D1_COLOR_F kDazaiInk = {0.16f, 0.11f, 0.10f, 1.0f};
const D2D1_COLOR_F kDazaiHair = {0.23f, 0.165f, 0.14f, 1.0f};

void DazaiHead(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t,
               const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    const float lw = std::max(1.0f, s * 0.014f);
    rt->SetTransform(world);

    // Collar first, under the chin: coat lapels, shirt, bolo tie, bandaged neck.
    const D2D1_COLOR_F coat = Hex(0xB48A5E), gauze = Hex(0xF6F4F0);
    if (auto sh = Polygon(rt, c, rx, ry, {{-0.46f, 0.88f}, {0.46f, 0.88f}, {0.34f, 1.30f}, {-0.34f, 1.30f}})) {
        rt->FillGeometry(sh.Get(), brush(Hex(0xFFFFFF)));
        rt->DrawGeometry(sh.Get(), brush(kDazaiInk), lw);
    }
    for (int side = -1; side <= 1; side += 2) {
        rt->DrawLine(D2D1::Point2F(c.x + side * rx * 0.05f, c.y + ry * 1.02f), D2D1::Point2F(c.x + side * rx * 0.03f, c.y + ry * 1.30f),
                     brush(Hex(0x1A1A1E)), lw * 1.1f);
        if (auto lapel = Polygon(rt, c, rx, ry, {{side * 1.0f, 1.30f}, {side * 0.42f, 0.86f}, {side * 0.16f, 1.30f}})) {
            rt->FillGeometry(lapel.Get(), brush(coat));
            rt->DrawGeometry(lapel.Get(), brush(kDazaiInk), lw);
        }
    }
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + ry * 1.04f), rx * 0.05f, ry * 0.05f), brush(Hex(0x7A8C9A)));  // tie clasp
    const D2D1_RECT_F neck = D2D1::RectF(c.x - rx * 0.26f, c.y + ry * 0.70f, c.x + rx * 0.26f, c.y + ry * 0.98f);
    rt->FillRectangle(neck, brush(gauze));
    rt->DrawRectangle(neck, brush(kDazaiInk), lw * 0.8f);
    for (float k : {0.80f, 0.89f})
        rt->DrawLine(D2D1::Point2F(neck.left, c.y + ry * k), D2D1::Point2F(neck.right, c.y + ry * (k + 0.04f)), brush(Hex(0xBDB6AC)), lw * 0.7f);

    // A big mop of hair behind the face, rounded on top and falling in tufts
    // down both sides.
    const D2D1_ELLIPSE lumps[] = {
        D2D1::Ellipse(D2D1::Point2F(c.x, c.y - ry * 0.25f), rx * 1.15f, ry * 1.0f),
        D2D1::Ellipse(D2D1::Point2F(c.x - rx * 0.95f, c.y + ry * 0.22f), rx * 0.30f, ry * 0.44f),
        D2D1::Ellipse(D2D1::Point2F(c.x + rx * 0.95f, c.y + ry * 0.22f), rx * 0.30f, ry * 0.44f),
        D2D1::Ellipse(D2D1::Point2F(c.x - rx * 0.80f, c.y + ry * 0.58f), rx * 0.22f, ry * 0.28f),
        D2D1::Ellipse(D2D1::Point2F(c.x + rx * 0.80f, c.y + ry * 0.58f), rx * 0.22f, ry * 0.28f),
    };
    for (const D2D1_ELLIPSE& e : lumps)
        rt->FillEllipse(D2D1::Ellipse(e.point, e.radiusX + lw * 1.2f, e.radiusY + lw * 1.2f), brush(kDazaiInk));
    for (const D2D1_ELLIPSE& e : lumps) rt->FillEllipse(e, brush(kDazaiHair));

    const D2D1_ELLIPSE face = D2D1::Ellipse(D2D1::Point2F(c.x, c.y + ry * 0.05f), rx * 0.86f, ry * 0.92f);
    rt->FillEllipse(face, brush(kDazaiSkin));
    rt->DrawEllipse(face, brush(kDazaiInk), lw);

    // Messy fringe falling to the eyes.
    if (auto fringe = Polygon(rt, c, rx, ry, {{-1.0f, -0.80f}, {-1.0f, -0.05f}, {-0.84f, 0.34f}, {-0.78f, -0.22f},
                                              {-0.56f, 0.04f}, {-0.46f, -0.36f}, {-0.28f, 0.08f}, {-0.16f, -0.40f},
                                              {0.02f, -0.02f}, {0.10f, -0.42f}, {0.28f, 0.10f}, {0.40f, -0.34f},
                                              {0.56f, 0.02f}, {0.72f, -0.24f}, {0.84f, 0.32f}, {1.0f, -0.05f},
                                              {1.0f, -0.80f}, {0.0f, -1.10f}}))
        rt->FillGeometry(fringe.Get(), brush(kDazaiHair));
    const float sway = std::sin(static_cast<float>(t) * 1.4f) * rx * 0.02f;  // strands stir a little
    for (const auto& h : {D2D1::Point2F(-0.55f, -0.95f), D2D1::Point2F(0.05f, -1.08f), D2D1::Point2F(0.55f, -0.90f)}) {
        const D2D1_POINT_2F a = D2D1::Point2F(c.x + h.x * rx, c.y + h.y * ry);
        rt->DrawLine(a, D2D1::Point2F(a.x + rx * 0.10f + sway, a.y + ry * 0.38f), brush(Hex(0x5A4238)), lw * 1.1f);
    }

    // Gauze taped over his right cheek (our right).
    const D2D1_POINT_2F g = D2D1::Point2F(c.x + rx * 0.56f, c.y + ry * 0.42f);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(18.0f, g) * world);
    const D2D1_RECT_F pad = D2D1::RectF(g.x - rx * 0.11f, g.y - ry * 0.13f, g.x + rx * 0.11f, g.y + ry * 0.13f);
    rt->FillRectangle(pad, brush(gauze));
    rt->DrawRectangle(pad, brush(kDazaiInk), lw * 0.8f);
    rt->DrawLine(D2D1::Point2F(pad.left - rx * 0.04f, g.y - ry * 0.07f), D2D1::Point2F(pad.right + rx * 0.04f, g.y - ry * 0.07f),
                 brush(Hex(0xE2C9A8)), lw * 1.6f);
    rt->SetTransform(world);
}

// Dazai's mouth and hands per mood, and his book.
void DazaiFace(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, Mood mood,
               double t, const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    const float lw = std::max(1.0f, s * 0.014f);
    const float tp = static_cast<float>(t);
    rt->SetTransform(world);
    switch (mood) {
        case Mood::Working: {
            if (t_live) {  // Live: at the laptop (drawn with the others), a small smile
                MouthLine(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.50f), rx * 0.10f, ry * 0.04f, lw, kDazaiInk);
                break;
            }
            // Reading: an open book held up, its red cover toward us, a page
            // turning every few seconds.
            const float bob = std::sin(tp * 2.0f) * ry * 0.02f;
            const D2D1_COLOR_F cover = Hex(0x8E2B2B), gold = Hex(0xD9A848);
            const D2D1_POINT_2F bc = D2D1::Point2F(c.x, c.y + bob);
            if (auto pages = Polygon(rt, bc, rx, ry, {{-0.60f, 0.46f}, {0.0f, 0.55f}, {0.60f, 0.46f}, {0.60f, 0.56f}, {-0.60f, 0.56f}}))
                rt->FillGeometry(pages.Get(), brush(Hex(0xF7F2E6)));
            for (int side = -1; side <= 1; side += 2) {
                if (auto panel = Polygon(rt, bc, rx, ry, {{side * 0.02f, 0.58f}, {side * 0.64f, 0.50f}, {side * 0.64f, 1.14f}, {side * 0.02f, 1.22f}})) {
                    rt->FillGeometry(panel.Get(), brush(cover));
                    rt->DrawGeometry(panel.Get(), brush(kDazaiInk), lw);
                }
                if (auto trim = Polygon(rt, bc, rx, ry, {{side * 0.12f, 0.66f}, {side * 0.54f, 0.61f}, {side * 0.54f, 1.04f}, {side * 0.12f, 1.10f}}))
                    rt->DrawGeometry(trim.Get(), brush(gold), lw * 0.8f);
            }
            rt->DrawLine(D2D1::Point2F(bc.x, bc.y + ry * 0.58f), D2D1::Point2F(bc.x, bc.y + ry * 1.22f), brush(Hex(0x5E1A1A)), lw * 1.6f);
            const float ph = std::fmod(tp, 3.2f) / 3.2f;
            if (ph > 0.82f) {  // a page lifts from the right and flips over
                const float f = (ph - 0.82f) / 0.18f;
                const float x = 0.58f - 1.16f * Smooth(f);
                if (auto page = Polygon(rt, bc, rx, ry, {{0.0f, 0.55f}, {x, 0.42f - 0.12f * std::sin(f * kPi)}, {x, 0.52f}}))
                    rt->FillGeometry(page.Get(), brush(Hex(0xFFFFFF)));
            }
            Fist(rt, cache, D2D1::Point2F(bc.x - rx * 0.52f, bc.y + ry * 1.12f), rx, ry, lw, kDazaiSkin, kDazaiInk, true);
            ActionTicks(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.86f), rx * 0.72f, s, t);
            break;
        }
        case Mood::Snack: {
            // Holding a bun in both bandaged hands, munching.
            const float bite = SnackBite(t);
            MouthLine(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.48f), rx * 0.08f, ry * (0.04f + 0.06f * bite), lw, kDazaiInk);
            if (t_streaming || t_drinking) break;  // only the mouth; the popcorn or the glass comes after the face
            const D2D1_POINT_2F b = D2D1::Point2F(c.x, c.y + ry * (0.78f + 0.05f * bite));
            const D2D1_ELLIPSE bun = D2D1::Ellipse(b, rx * 0.30f, ry * 0.22f);
            rt->FillEllipse(bun, brush(Hex(0x9A5A2C)));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(b.x - rx * 0.06f, b.y - ry * 0.07f), rx * 0.16f, ry * 0.08f), brush(Hex(0xC07A3E)));
            rt->DrawEllipse(bun, brush(kDazaiInk), lw);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(b.x + rx * 0.12f, b.y - ry * 0.16f), rx * 0.10f, ry * 0.08f), brush(kDazaiSkin));  // bite
            for (int side = -1; side <= 1; side += 2)
                Fist(rt, cache, D2D1::Point2F(b.x + side * rx * 0.34f, b.y + ry * 0.12f), rx, ry, lw, kDazaiSkin, kDazaiInk, true);
            ActionTicks(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.78f), rx * 0.95f, s, t);
            break;
        }
        case Mood::Alert: {
            // "You!": a small open mouth and a bandaged hand pointing straight at us.
            OMouth(rt, cache, D2D1::Point2F(c.x + rx * 0.05f, c.y + ry * 0.58f), rx * 0.11f, ry * 0.13f, lw, kDazaiInk);
            const float jab = std::fabs(std::sin(tp * 5.0f)) * ry * 0.05f;
            const D2D1_POINT_2F fist = D2D1::Point2F(c.x - rx * 0.42f, c.y + ry * 1.0f - jab);
            const D2D1_POINT_2F tipPt = D2D1::Point2F(fist.x - rx * 0.20f, fist.y - ry * 0.22f);
            rt->DrawLine(fist, tipPt, brush(kDazaiInk), rx * 0.14f + lw * 2.0f);
            rt->DrawLine(fist, tipPt, brush(kDazaiSkin), rx * 0.14f);
            Fist(rt, cache, fist, rx, ry, lw, kDazaiSkin, kDazaiInk, true);
            rt->FillEllipse(D2D1::Ellipse(tipPt, rx * 0.075f, rx * 0.075f), brush(kDazaiSkin));
            rt->DrawEllipse(D2D1::Ellipse(tipPt, rx * 0.075f, rx * 0.075f), brush(kDazaiInk), lw);
            ActionTicks(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.55f), rx * 1.05f, s, t);
            break;
        }
        case Mood::Thinking:
            MouthLine(rt, cache, D2D1::Point2F(c.x + rx * 0.02f, c.y + ry * 0.56f), rx * 0.12f, -ry * 0.04f, lw, kDazaiInk);
            ChinHand(rt, cache, c, rx, ry, lw, t, kDazaiSkin, kDazaiInk, true);
            break;
        case Mood::Sleepy:
            if (t_dnd) MouthLine(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.50f), rx * 0.08f, ry * 0.025f, lw, kDazaiInk);  // calm
            else SnotBubble(rt, cache, c, rx, ry, s, t, lw, kDazaiInk);
            break;
        case Mood::Sad: Wail(rt, cache, c, rx, ry, t, lw, kDazaiInk, world); break;
        case Mood::Talking: {  // a lazy drawl: the mouth barely opens
            const float open = TalkOpen(t);
            OMouth(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.55f), rx * (0.10f - 0.02f * open), ry * (0.03f + 0.09f * open), lw, kDazaiInk);
            break;
        }
        case Mood::Confused:
            WavyMouth(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.55f), rx * 0.13f, ry * 0.03f, lw * 1.1f, kDazaiInk);
            break;
        default: MouthLine(rt, cache, D2D1::Point2F(c.x, c.y + ry * 0.54f), rx * 0.13f, ry * 0.07f, lw, kDazaiInk); break;  // easy smile
    }
}

}  // namespace

// ── Style ───────────────────────────────────────────────────────────────────

void SetStyle(const Style& style) {
    std::lock_guard lock(g_styleMutex);
    g_style = style;
    ++g_packGeneration;  // re-read packs: they may have been edited or re-imported
}

Style GetStyle() {
    std::lock_guard lock(g_styleMutex);
    return g_style;
}

const wchar_t* CharacterName(Character c) {
    switch (c) {
        case Character::Cat: return L"cat";
        case Character::Dog: return L"dog";
        case Character::Fox: return L"fox";
        case Character::Panda: return L"panda";
        case Character::Robot: return L"robot";
        case Character::Spiderman: return L"spiderman";
        case Character::Luffy: return L"luffy";
        case Character::Dazai: return L"dazai";
        case Character::Custom: return L"custom";
        default: return L"bunny";
    }
}

std::wstring CharacterId(const Style& s) {
    return s.character == Character::Custom ? L"custom:" + s.customPack : CharacterName(s.character);
}

Style StyleFromSettings(const std::wstring& character, const std::wstring& colorHex, int motionPercent,
                        int hopPercent, int blinkTenths, bool eyesFollow, bool celebrate) {
    Style s;
    std::wstring id = character;
    for (auto& ch : id) ch = static_cast<wchar_t>(towlower(ch));
    if (id.rfind(L"custom:", 0) == 0 && id.size() > 7) {
        s.character = Character::Custom;
        s.customPack = character.substr(7);  // keep the folder's case
        // A pack name is a single folder name: nothing that could walk the disk.
        if (s.customPack.find_first_of(L"\\/:") != std::wstring::npos || s.customPack.find(L"..") != std::wstring::npos)
            s.character = Character::Bunny;
    } else {
        for (Character c : {Character::Cat, Character::Dog, Character::Fox, Character::Panda, Character::Robot, Character::Spiderman, Character::Luffy, Character::Dazai})
            if (id == CharacterName(c)) s.character = c;
    }
    // Colours are kept per character: "bunny=#F4B6C8;cat=#5B5868". A plain hex
    // (settings from before per-character colours) applies to everyone.
    std::wstring hex = colorHex;
    if (hex.find(L'=') != std::wstring::npos) {
        const std::wstring want = std::wstring(CharacterName(s.character)) + L"=";
        std::wstring found;
        size_t pos = 0;
        while (pos <= hex.size()) {
            const size_t end = std::min(hex.find(L';', pos), hex.size());
            std::wstring entry = hex.substr(pos, end - pos);
            for (auto& ch : entry) ch = static_cast<wchar_t>(towlower(ch));
            if (entry.rfind(want, 0) == 0) found = entry.substr(want.size());
            pos = end + 1;
        }
        hex = found;
    }
    // Luffy and Dazai keep their own colours, and skin packs are images. Spidey's
    // colour is his Venom suit (SpideyPalette), so only one picked for him counts,
    // never an old everyone-colour.
    if (s.character == Character::Luffy || s.character == Character::Dazai || s.character == Character::Custom ||
        (s.character == Character::Spiderman && colorHex.find(L'=') == std::wstring::npos)) hex.clear();
    if (!hex.empty() && hex[0] == L'#') hex.erase(0, 1);
    if (hex.size() == 6) {
        wchar_t* end = nullptr;
        const unsigned long v = wcstoul(hex.c_str(), &end, 16);
        if (end && *end == 0) { s.hasColor = true; s.color = Hex(static_cast<UINT32>(v)); }
    }
    s.motion = std::clamp(motionPercent, 0, 200) / 100.0f;
    s.hop = std::clamp(hopPercent, 0, 200) / 100.0f;
    s.blinkSeconds = blinkTenths <= 0 ? 0.0f : std::clamp(blinkTenths, 10, 200) / 10.0f;
    s.eyesFollow = eyesFollow;
    s.celebrate = celebrate;
    return s;
}

std::wstring PacksFolder() {
    PWSTR roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) dir = roaming;
    CoTaskMemFree(roaming);
    return dir + L"\\Bunbun\\mascots";
}

std::vector<std::wstring> ListCustomPacks() {
    std::vector<std::wstring> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(PacksFolder(), ec)) {
        // Animated packs (made from a character sheet) have idle_1.png instead of idle.png.
        if (e.is_directory(ec) && (std::filesystem::exists(e.path() / L"idle.png", ec) ||
                                   std::filesystem::exists(e.path() / L"idle_1.png", ec)))
            out.push_back(e.path().filename().wstring());
    }
    std::sort(out.begin(), out.end());
    return out;
}

void ReleaseThreadResources() {
    BrushCache& b = Brushes();
    b.brush.Reset();
    b.owner = nullptr;
    PackCache& p = Packs(nullptr);  // a different owner empties it
    p.images.clear();
    p.packs.clear();
    p.layer.Reset();
}

void SetMusicEnergy(float energy) {
    g_musicEnergy.store(std::clamp(energy, 0.0f, 1.0f), std::memory_order_relaxed);
}

void SetCharging(bool charging) {
    g_charging.store(charging, std::memory_order_relaxed);
}

bool NeedsFastFrames(Mood mood, double t, double moodSince) {
    if (RigBusy(t)) return true;  // a face rig's gaze is moving or it's blinking
    switch (mood) {
        case Mood::Alert: return true;
        case Mood::Talking: return true;  // the mouth moves several times a second
        case Mood::Drinking: return t - moodSince < 2.8;
        case Mood::Happy: return t - moodSince < 2.2;
        default: return false;
    }
}

// Outline of each character in its resting pose, in units of `size` relative to
// the draw center: top and bottom of the silhouette, and its half width. Read
// off the drawing code: the head sits at +0.16, radius 0.36 x 0.30; Bun's ears
// reach up to -0.56, pointy ears to -0.27, the antenna to -0.36.
struct Extent { float top, bottom, halfWidth; };

Extent ExtentFor(Character c) {
    switch (c) {
        case Character::Bunny: return {-0.56f, 0.46f, 0.38f};
        case Character::Cat:
        case Character::Fox: return {-0.27f, 0.46f, 0.38f};
        case Character::Dog: return {-0.15f, 0.46f, 0.44f};     // floppy ears hang at the sides
        case Character::Panda: return {-0.19f, 0.46f, 0.38f};
        case Character::Robot: return {-0.36f, 0.46f, 0.37f};
        case Character::Spiderman: return {-0.19f, 0.55f, 0.36f};  // laptop below the chin when working
        case Character::Luffy: return {-0.28f, 0.55f, 0.48f};      // hat crown and brim; laptop
        case Character::Dazai: return {-0.24f, 0.55f, 0.46f};      // mop of hair; coat collar
        default: return {-0.50f, 0.50f, 0.50f};                  // skin packs fill their square
    }
}

void HorizontalReach(float* left, float* right) {
    const Style st = GetStyle();
    *left = *right = ExtentFor(st.character).halfWidth;
    if (st.character != Character::Custom) return;
    const std::wstring dir = PacksFolder() + L"\\" + st.customPack + L"\\";
    if (!FileExists(dir + L"idle_1.png")) return;  // single-picture packs fit their square
    struct Entry { unsigned gen; float l, r; };
    static std::mutex mu;
    static auto* cache = new std::map<std::wstring, Entry>;  // never destroyed, like the other caches
    const unsigned gen = g_packGeneration.load();
    std::lock_guard<std::mutex> lock(mu);
    if (auto it = cache->find(st.customPack); it != cache->end() && it->second.gen == gen) {
        *left = it->second.l;
        *right = it->second.r;
        return;
    }
    // Every frame's drawn part, measured against idle's first frame, which sets
    // the pack's scale and centre in DrawPack.
    auto box = [](const std::wstring& path, int* x0, int* x1, int* y0, int* y1) {
        Image img;
        if (!DecodeImageFile(path, &img) || img.w > 4096 || img.h > 4096) return false;
        *x0 = img.w; *x1 = -1; *y0 = img.h; *y1 = -1;
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x)
                if (img.at(x, y)[3] > 16) { *x0 = std::min(*x0, x); *x1 = std::max(*x1, x + 1); *y0 = std::min(*y0, y); *y1 = std::max(*y1, y + 1); }
        return *x1 > *x0;
    };
    Entry e{gen, *left, *right};
    int rx0, rx1, ry0, ry1;
    if (box(dir + L"idle_1.png", &rx0, &rx1, &ry0, &ry1)) {
        const float cx = (rx0 + rx1) * 0.5f, scale = static_cast<float>(std::max(rx1 - rx0, ry1 - ry0));
        int lo = rx0, hi = rx1;
        std::error_code ec;
        for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
            const std::wstring name = f.path().filename().wstring();
            if (f.path().extension() != L".png" || name.find(L'_') == std::wstring::npos || name.rfind(L"look_", 0) == 0) continue;
            int x0, x1, y0, y1;
            if (box(f.path().wstring(), &x0, &x1, &y0, &y1)) { lo = std::min(lo, x0); hi = std::max(hi, x1); }
        }
        e.l = std::clamp((cx - lo) / scale, 0.5f, 1.0f);
        e.r = std::clamp((hi - cx) / scale, 0.5f, 1.0f);
    }
    (*cache)[st.customPack] = e;
    *left = e.l;
    *right = e.r;
}

void FitToBox(D2D1_RECT_F box, D2D1_POINT_2F* center, float* size) {
    const Extent e = ExtentFor(GetStyle().character);
    const float h = box.bottom - box.top;
    const float w = box.right - box.left;
    // A little headroom for the idle bob and hop.
    const float s = std::max(1.0f, std::min(h / ((e.bottom - e.top) * 1.04f), w / (e.halfWidth * 2.0f)));
    *size = s;
    *center = D2D1::Point2F((box.left + box.right) * 0.5f, (box.top + box.bottom) * 0.5f - (e.top + e.bottom) * 0.5f * s);
}

void Draw(ID2D1RenderTarget* rt, D2D1_POINT_2F center, float size, Mood mood, double t, double moodSince,
          D2D1_POINT_2F look) {
    DrawStyled(rt, GetStyle(), center, size, mood, t, moodSince, look);
}

void DrawStyled(ID2D1RenderTarget* rt, const Style& st, D2D1_POINT_2F center, float size, Mood mood, double t,
                double moodSince, D2D1_POINT_2F look) {
    const float s = size;
    const float since = static_cast<float>(t - moodSince);
    if (st.character == Character::Custom) {
        DrawPack(rt, st, center, s, mood, t, since, look);
        return;
    }
    t_streaming = mood == Mood::Streaming;
    if (t_streaming) mood = Mood::Snack;
    t_drinking = mood == Mood::Drinking;
    if (t_drinking) mood = Mood::Snack;
    t_dnd = mood == Mood::Dnd;
    if (t_dnd) mood = Mood::Sleepy;
    t_live = mood == Mood::Live;
    if (t_live) mood = Mood::Working;
    if (!st.eyesFollow) look = D2D1::Point2F(0, 0);
    t_spidey = SpideyPaletteFor(st);
    const Look L = LookFor(st);
    BrushCache& cache = Brushes();
    auto brush = [&](D2D1_COLOR_F c) { return cache.Get(rt, c); };

    D2D1_MATRIX_3X2_F saved;
    rt->GetTransform(&saved);
    const Motion mo = BodyMotion(st, mood, t, since, s);

    const D2D1_POINT_2F headC = D2D1::Point2F(center.x, center.y + s * 0.16f + mo.bob);
    const float headRx = s * 0.36f;
    const float headRy = s * 0.30f * mo.squash;
    const D2D1_MATRIX_3X2_F world = D2D1::Matrix3x2F::Rotation(mo.tilt, headC) * saved;

    // ── Ears / antenna (behind the head) ─────────────────────────────────────
    const float k = st.motion;
    float spread = 13.0f, wiggle = std::sin(static_cast<float>(t) * 1.7f) * 2.0f * k, lenK = 1.0f;
    switch (mood) {
        case Mood::Working: wiggle = std::sin(static_cast<float>(t) * 4.0f) * 6.0f * k; break;
        case Mood::Alert: spread = 4.0f; wiggle = std::sin(static_cast<float>(t) * 18.0f) * 3.5f * k; lenK = 1.12f; break;
        case Mood::Happy: spread = 18.0f; wiggle = std::sin(since * 10.0f) * 5.0f * k; break;
        case Mood::Sad: spread = 62.0f; lenK = 0.92f; break;
        case Mood::Sleepy: if (!t_dnd) { spread = 48.0f; lenK = 0.94f; } break;  // DND: ears as usual
        case Mood::Music: spread = 16.0f; wiggle = std::sin(static_cast<float>(t) * kPi * 2.0f) * 7.0f * k; break;
        case Mood::Listening: spread = 6.0f; lenK = 1.08f; wiggle = std::sin(static_cast<float>(t) * 2.5f) * 1.5f * k; break;  // perked up
        case Mood::Talking: wiggle = std::sin(static_cast<float>(t) * 4.5f) * 3.0f * k; break;
        case Mood::Confused: spread = 24.0f; wiggle = std::sin(static_cast<float>(t) * 1.2f) * 6.0f * k; break;
        default: break;
    }
    const float earBaseY = headC.y - headRy * 0.72f;
    for (int side = -1; side <= 1; side += 2) {
        const D2D1_POINT_2F base = D2D1::Point2F(headC.x + side * headRx * 0.42f, earBaseY);
        const float angle = side * spread + wiggle * (side > 0 ? 0.8f : 1.0f);
        switch (L.ears) {
            case Ears::Long: {
                const float len = s * 0.50f * lenK, w = s * 0.17f;
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(angle, base) * world);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(base.x, base.y - len * 0.5f), w * 0.5f, len * 0.5f), brush(L.earColor));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(base.x, base.y - len * 0.47f), w * 0.25f, len * 0.36f), brush(L.inner));
                break;
            }
            case Ears::Pointy: {
                // Cat / fox: triangles that flatten when sad or sleepy.
                const float flat = (mood == Mood::Sad || mood == Mood::Sleepy) ? 0.6f : 1.0f;
                const float h = s * 0.26f * lenK * flat, w = s * 0.24f;
                const D2D1_POINT_2F b = D2D1::Point2F(headC.x + side * headRx * 0.55f, headC.y - headRy * 0.55f);
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(side * 8.0f + wiggle * 0.6f, b) * world);
                Triangle(rt, brush(L.earColor), D2D1::Point2F(b.x - w * 0.5f, b.y + h * 0.15f),
                         D2D1::Point2F(b.x + side * w * 0.12f, b.y - h), D2D1::Point2F(b.x + w * 0.5f, b.y + h * 0.15f));
                Triangle(rt, brush(L.inner), D2D1::Point2F(b.x - w * 0.28f, b.y + h * 0.05f),
                         D2D1::Point2F(b.x + side * w * 0.10f, b.y - h * 0.68f), D2D1::Point2F(b.x + w * 0.28f, b.y + h * 0.05f));
                break;
            }
            case Ears::Floppy: {
                // Dog: long ears hanging beside the head; they swing with the motion.
                const float lift = (mood == Mood::Alert ? -18.0f : mood == Mood::Listening ? -14.0f : mood == Mood::Happy ? -10.0f : 0.0f);
                const D2D1_POINT_2F b = D2D1::Point2F(headC.x + side * headRx * 0.80f, headC.y - headRy * 0.55f);
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(side * (18.0f + lift) + wiggle, b) * world);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(b.x + side * s * 0.04f, b.y + s * 0.17f), s * 0.10f, s * 0.21f),
                                brush(L.earColor));
                break;
            }
            case Ears::Round: {
                const D2D1_POINT_2F b = D2D1::Point2F(headC.x + side * headRx * 0.70f, headC.y - headRy * 0.78f);
                rt->SetTransform(world);
                rt->FillEllipse(D2D1::Ellipse(b, s * 0.11f, s * 0.11f), brush(L.earColor));
                break;
            }
            case Ears::Antenna: {
                if (side > 0) break;  // one antenna, in the middle
                const D2D1_POINT_2F b = D2D1::Point2F(headC.x, headC.y - headRy * 0.95f);
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(wiggle * 2.0f, b) * world);
                const D2D1_POINT_2F tip = D2D1::Point2F(b.x, b.y - s * 0.18f * lenK);
                rt->DrawLine(b, tip, brush(Shade(L.fur, 0.7f)), std::max(1.0f, s * 0.03f));
                const float glow = mood == Mood::Alert ? 0.6f + 0.4f * std::sin(static_cast<float>(t) * 12.0f)
                                   : mood == Mood::Talking ? 0.45f + 0.55f * TalkOpen(t) : 1.0f;
                const D2D1_COLOR_F tipColor = mood == Mood::Snack ? Hex(0x4CF08A) : mood == Mood::Sad ? Hex(0xFF5A5A)
                                              : mood == Mood::Listening ? Hex(0x7FD4FF) : mood == Mood::Confused ? Hex(0xFFB627) : L.inner;
                rt->FillEllipse(D2D1::Ellipse(tip, s * 0.05f, s * 0.05f), brush(D2D1::ColorF(tipColor.r, tipColor.g, tipColor.b, glow)));
                break;
            }
            case Ears::None: break;
        }
    }
    rt->SetTransform(world);
    if (mood == Mood::Confused) DizzyRing(rt, cache, headC, headRx, headRy, s, t, world, false);  // behind the head
    if (mood == Mood::Happy)  // the celebration's confetti, behind the head
        Celebrate(rt, cache, headC, headRx, headRy, s, since, world, s * 0.012f, true);

    // ── Head ────────────────────────────────────────────────────────────────
    if (L.robot) {
        const D2D1_RECT_F head = D2D1::RectF(headC.x - headRx, headC.y - headRy, headC.x + headRx, headC.y + headRy);
        rt->FillRoundedRectangle(D2D1::RoundedRect(head, s * 0.12f, s * 0.12f), brush(Shade(L.fur, 0.85f)));
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(head.left, head.top, head.right, head.bottom - s * 0.015f),
                                                   s * 0.12f, s * 0.12f), brush(L.fur));
        // Visor
        const D2D1_RECT_F visor = D2D1::RectF(head.left + s * 0.06f, headC.y - headRy * 0.55f, head.right - s * 0.06f,
                                              headC.y + headRy * 0.45f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(visor, s * 0.07f, s * 0.07f), brush(Hex(0x1B2330)));
    } else if (L.luffy) {
        LuffyHead(rt, cache, headC, headRx, headRy, s, mood, t, world);
    } else if (L.dazai) {
        DazaiHead(rt, cache, headC, headRx, headRy, s, t, world);
    } else if (L.spiderman) {
        SpideyHead(rt, cache, headC, headRx * 0.94f, headRy * 1.14f, s, mood, t, world);
    } else {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(headC.x, headC.y + headRy * 0.06f), headRx, headRy), brush(Shade(L.fur, 0.9f)));
        rt->FillEllipse(D2D1::Ellipse(headC, headRx, headRy * 0.97f), brush(L.fur));
        if (L.muzzle) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(headC.x, headC.y + headRy * 0.42f), headRx * 0.62f, headRy * 0.50f),
                            brush(L.muzzleColor));
        }
    }

    const float eyeDx = headRx * 0.40f;
    const float eyeY = headC.y + headRy * (L.robot ? -0.05f : (L.spiderman ? 0.04f : (L.dazai ? 0.10f : 0.02f)));
    if (mood == Mood::Working) look = ReadingLook(t);  // eyes on the screen (or the page)
    const float lookX = std::clamp(look.x, -1.0f, 1.0f) * s * 0.035f;
    float lookY = std::clamp(look.y, -1.0f, 1.0f) * s * 0.025f;
    if (mood == Mood::Thinking) lookY = -s * 0.03f;
    const float eyeR = s * (mood == Mood::Alert ? 0.068f : mood == Mood::Listening ? 0.066f : 0.06f);
    const float stroke = std::max(1.0f, s * 0.035f);

    // ── Spidey-Sense Tingling Arcs (Mood::Alert) ───────────────────────────
    if (L.spiderman && mood == Mood::Alert) {
        // Iconic spidey-sense radiating squiggly lightning/waves above the head
        const float tPhase = static_cast<float>(t) * 9.0f;
        const float sensePulse = 0.70f + 0.30f * std::sin(tPhase);
        const D2D1_COLOR_F senseGlow = Hex(0xFFE600, sensePulse * 0.95f);  // Electric yellow
        const D2D1_COLOR_F senseOutline = Hex(0xFF3B30, sensePulse * 0.85f); // Red danger aura
        const float senseW = std::max(1.4f, s * 0.028f);

        // Multiple radiating squiggles at angles around the crown: -60°, -30°, 0°, 30°, 60°
        for (float deg : {-65.0f, -38.0f, -12.0f, 12.0f, 38.0f, 65.0f}) {
            const float rad = deg * (kPi / 180.0f);
            const float cosA = std::sin(rad); // 0 at top, positive to right
            const float sinA = -std::cos(rad); // negative at top

            // Zig-zag wave points: start near head edge, zig, zag, end outward
            const float r0 = headRx * 1.08f;
            const float r1 = headRx * 1.25f + std::sin(tPhase + deg * 0.1f) * s * 0.02f;
            const float r2 = headRx * 1.45f + std::cos(tPhase + deg * 0.1f) * s * 0.025f;

            // Perpendicular vector for the lightning zig-zag
            const float perpX = -sinA * s * 0.032f;
            const float perpY = cosA * s * 0.032f;

            const D2D1_POINT_2F p0 = D2D1::Point2F(headC.x + cosA * r0, headC.y + sinA * r0);
            const D2D1_POINT_2F p1 = D2D1::Point2F(headC.x + cosA * (r0 + (r1 - r0) * 0.5f) + perpX,
                                                  headC.y + sinA * (r0 + (r1 - r0) * 0.5f) + perpY);
            const D2D1_POINT_2F p2 = D2D1::Point2F(headC.x + cosA * r1 - perpX,
                                                  headC.y + sinA * r1 - perpY);
            const D2D1_POINT_2F p3 = D2D1::Point2F(headC.x + cosA * r2, headC.y + sinA * r2);

            // Draw red aura + bright electric yellow lightning core
            rt->DrawLine(p0, p1, brush(senseOutline), senseW * 2.0f);
            rt->DrawLine(p1, p2, brush(senseOutline), senseW * 2.0f);
            rt->DrawLine(p2, p3, brush(senseOutline), senseW * 2.0f);

            rt->DrawLine(p0, p1, brush(senseGlow), senseW);
            rt->DrawLine(p1, p2, brush(senseGlow), senseW);
            rt->DrawLine(p2, p3, brush(senseGlow), senseW);
        }
    }

    if (L.patches) {
        for (int side = -1; side <= 1; side += 2) {
            const D2D1_POINT_2F pc = D2D1::Point2F(headC.x + side * eyeDx, eyeY + s * 0.01f);
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(side * -25.0f, pc) * world);
            rt->FillEllipse(D2D1::Ellipse(pc, s * 0.085f, s * 0.11f), brush(L.earColor));
        }
        rt->SetTransform(world);
    }

    // Cheeks
    if (!L.robot && !L.spiderman && !L.luffy && !L.dazai) {
        const float cheekY = headC.y + headRy * 0.30f;
        for (int side = -1; side <= 1; side += 2)
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(headC.x + side * headRx * 0.58f, cheekY), s * 0.065f, s * 0.04f),
                            brush(L.blush));
    }

    if (L.spiderman) SpideyLenses(rt, cache, headC, headRx * 0.94f, headRy * 1.14f, s, mood, t, since, st, look, world);

    // Bolt works in his own head: the visor becomes a terminal.
    const bool terminalEyes = L.robot && mood == Mood::Working;
    if (terminalEyes) {
        TerminalVisor(rt, cache, D2D1::RectF(headC.x - headRx + s * 0.06f, headC.y - headRy * 0.55f,
                                             headC.x + headRx - s * 0.06f, headC.y + headRy * 0.45f), s, t);
    }

    // ── Eyes ────────────────────────────────────────────────────────────────
    const D2D1_COLOR_F eyeColor = L.patches ? Hex(0x0B0B0D) : L.eye;
    for (int side = -1; side <= 1; side += 2) {
        const D2D1_POINT_2F ec = D2D1::Point2F(headC.x + side * eyeDx + lookX, eyeY + lookY);
        if (L.spiderman || terminalEyes) continue;  // drawn by SpideyLenses / TerminalVisor
        const float eyeSide = mood == Mood::Confused ? eyeR * (side < 0 ? 1.15f : 0.70f) : eyeR;
        D2D1_COLOR_F drawEyeColor = eyeColor;
        if (L.patches && (mood == Mood::Happy || mood == Mood::Music || mood == Mood::Sleepy || mood == Mood::Sad)) {
            // White curve over black panda eye patches for clean visibility
            drawEyeColor = Hex(0xFFFFFF);
        }
        if (L.luffy && mood == Mood::Working) {  // Gear 5: laughing
            EyeLine(rt, brush(drawEyeColor), ec, eyeR, eyeR * 0.9f, stroke * 1.2f);
            continue;
        }
        switch (t_streaming ? Mood::Idle : mood) {  // streaming: eyes open, on the screen
            case Mood::Happy:
            case Mood::Snack:
            case Mood::Music: EyeLine(rt, brush(drawEyeColor), ec, eyeR, eyeR * 0.9f, stroke * (L.patches ? 1.25f : 1.0f)); break;
            case Mood::Sleepy: EyeLine(rt, brush(drawEyeColor), ec, eyeR, -eyeR * 0.25f, stroke * (L.patches ? 1.25f : 1.0f)); break;
            case Mood::Confused:  // dizzy: spinning spirals (Bolt's on his visor, in LED light)
                SpiralEye(rt, cache, ec, eyeR * 1.2f, t + side * 0.15, L.robot ? L.eye : Hex(0x1A1622), !L.robot, stroke * 0.55f, world);
                break;
            case Mood::Sad: {
                if (L.robot || L.luffy || L.dazai) {
                    // X eyes: Bolt's are red with a faint glitch flicker, the others' are ink.
                    const float flick = std::fmod(static_cast<float>(t) * 7.0f + side, 5.0f) < 0.4f ? 0.45f : 1.0f;
                    const float r = eyeR * 0.75f;
                    ID2D1Brush* red = L.robot ? brush(Hex(0xFF5A5A, flick)) : brush(eyeColor);
                    rt->DrawLine(D2D1::Point2F(ec.x - r, ec.y - r), D2D1::Point2F(ec.x + r, ec.y + r), red, stroke * 1.2f);
                    rt->DrawLine(D2D1::Point2F(ec.x - r, ec.y + r), D2D1::Point2F(ec.x + r, ec.y - r), red, stroke * 1.2f);
                    break;
                }
                // Open eyes with a heavy lid sloping down to the outside: sad, not asleep.
                const D2D1_POINT_2F lowered = D2D1::Point2F(ec.x, ec.y + eyeR * 0.15f);
                rt->FillEllipse(D2D1::Ellipse(lowered, eyeR * 0.95f, eyeR * 1.05f), brush(eyeColor));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lowered.x + eyeR * 0.30f, lowered.y + eyeR * 0.15f), eyeR * 0.30f, eyeR * 0.30f),
                                brush(Hex(0xFFFFFF, 0.9f)));
                const D2D1_COLOR_F lid = L.patches ? L.earColor : L.fur;
                Triangle(rt, brush(lid), D2D1::Point2F(ec.x - side * eyeR * 1.4f, ec.y - eyeR * 1.4f),
                         D2D1::Point2F(ec.x + side * eyeR * 1.4f, ec.y - eyeR * 1.4f),
                         D2D1::Point2F(ec.x + side * eyeR * 1.4f, ec.y + eyeR * 0.15f));
                // The lid's edge, so the slant reads as an eyelid on light fur too.
                rt->DrawLine(D2D1::Point2F(ec.x - side * eyeR * 1.05f, ec.y - eyeR * 1.05f),
                             D2D1::Point2F(ec.x + side * eyeR * 1.05f, ec.y + eyeR * 0.05f), brush(drawEyeColor), stroke * 0.8f);
                if (side > 0) Tear(rt, cache, lowered, eyeR, s, t);
                break;
            }
            default: {
                const float eyeR = eyeSide;  // Confused: one eye wide, the other squinting
                const float blink = BlinkAmount(t + (side > 0 ? 0.03 : 0.0), st.blinkSeconds);
                const float ry = eyeR * 1.15f * (1.0f - 0.9f * Smooth(blink));
                if (L.robot) {
                    // LED eyes: rounded bars that squash to a line on blink.
                    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ec.x - eyeR * 0.8f, ec.y - ry, ec.x + eyeR * 0.8f, ec.y + ry),
                                                               eyeR * 0.4f, eyeR * 0.4f), brush(eyeColor));
                } else if (L.dazai) {
                    // Half-lidded: the top of the eye hidden under a heavy, slightly
                    // slanted lid, so he looks bored even while following the cursor.
                    const float lidY = ec.y - ry * (mood == Mood::Alert ? 0.45f : 0.20f);
                    rt->PushAxisAlignedClip(D2D1::RectF(ec.x - eyeR * 2.0f, lidY, ec.x + eyeR * 2.0f, ec.y + ry * 2.0f),
                                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    rt->FillEllipse(D2D1::Ellipse(ec, eyeR * 1.05f, ry), brush(eyeColor));
                    if (blink < 0.5f)
                        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ec.x + eyeR * 0.35f, lidY + eyeR * 0.30f), eyeR * 0.28f, eyeR * 0.28f),
                                        brush(Hex(0xFFFFFF)));
                    rt->PopAxisAlignedClip();
                    rt->DrawLine(D2D1::Point2F(ec.x - side * eyeR * 1.25f, lidY - eyeR * 0.10f), D2D1::Point2F(ec.x + side * eyeR * 1.25f, lidY + eyeR * 0.10f),
                                 brush(eyeColor), stroke * 0.9f);
                } else {
                    rt->FillEllipse(D2D1::Ellipse(ec, eyeR, ry), brush(eyeColor));
                    if (blink < 0.5f) {
                        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ec.x + eyeR * 0.35f, ec.y - ry * 0.40f), eyeR * 0.36f,
                                                      eyeR * 0.36f * (ry / (eyeR * 1.15f))), brush(Hex(0xFFFFFF)));
                    }
                }
                break;
            }
        }
    }

    // ── Nose, mouth, whiskers ───────────────────────────────────────────────
    const D2D1_POINT_2F noseC = D2D1::Point2F(headC.x + lookX * 0.5f, headC.y + headRy * (L.robot ? 0.28f : 0.30f));
    const float lw = stroke * 0.6f;
    if (L.robot && mood == Mood::Talking) {
        // Bolt talks through an equaliser: five LED bars bouncing with the words.
        for (int i = -2; i <= 2; ++i) {
            const float hgt = s * (0.012f + 0.045f * TalkOpen(t + i * 0.13) * (1.0f - std::fabs(i) * 0.18f));
            const float x = noseC.x + i * s * 0.03f;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - s * 0.01f, noseC.y - hgt, x + s * 0.01f, noseC.y + hgt),
                                                       s * 0.01f, s * 0.01f), brush(L.eye));
        }
    } else if (L.robot && mood == Mood::Confused) {
        WavyMouth(rt, cache, noseC, s * 0.07f, s * 0.012f, lw * 1.3f, L.eye);
    } else if (L.robot) {
        const float mw = s * (mood == Mood::Happy ? 0.09f : 0.06f);
        rt->DrawLine(D2D1::Point2F(noseC.x - mw, noseC.y), D2D1::Point2F(noseC.x + mw, noseC.y), brush(L.eye), lw * 1.3f);
    } else if (L.spiderman && mood == Mood::Talking) {
        // The mask's fabric moves over his mouth while he talks.
        const float open = TalkOpen(t);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(headC.x, headC.y + headRy * 0.66f), headRx * (0.16f - 0.03f * open),
                                      headRy * (0.03f + 0.09f * open)), brush(t_spidey.mouth));
    } else if (L.luffy) {
        LuffyFace(rt, cache, headC, headRx, headRy, s, mood, t, world);
    } else if (L.dazai) {
        DazaiFace(rt, cache, headC, headRx, headRy, s, mood, t, world);
    } else if (!L.spiderman) {
        rt->FillEllipse(D2D1::Ellipse(noseC, s * 0.035f, s * 0.024f), brush(L.nose));
        const D2D1_COLOR_F ink = Hex(0x2A2230);
        if (mood == Mood::Alert) {
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(noseC.x, noseC.y + s * 0.07f), s * 0.025f, s * 0.03f), brush(ink), lw * 1.1f);
        } else if (mood == Mood::Talking) {
            // An open mouth that opens and closes with the words, a hint of tongue inside.
            const float open = TalkOpen(t);
            const D2D1_ELLIPSE m = D2D1::Ellipse(D2D1::Point2F(noseC.x, noseC.y + s * (0.06f + 0.012f * open)), s * (0.034f - 0.006f * open),
                                                 s * (0.008f + 0.03f * open));
            rt->FillEllipse(m, brush(Hex(0x4A1E2C)));
            if (open > 0.4f)
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(m.point.x, m.point.y + m.radiusY * 0.45f), m.radiusX * 0.6f, m.radiusY * 0.4f),
                                brush(Hex(0xFF7A93)));
            rt->DrawEllipse(m, brush(ink), lw);
        } else if (mood == Mood::Confused) {
            WavyMouth(rt, cache, D2D1::Point2F(noseC.x + s * 0.01f, noseC.y + s * 0.065f), s * 0.045f, s * 0.008f, lw * 1.2f, ink);
        } else if (mood == Mood::Sad) {
            // Wobbly frown: an arch whose corners tremble a little.
            const float mw = s * 0.045f, my = noseC.y + s * 0.07f;
            const float wob = std::sin(static_cast<float>(t) * 11.0f) * s * 0.004f;
            ComPtr<ID2D1GeometrySink> sink;
            ComPtr<ID2D1PathGeometry> frown = NewPath(rt, &sink);
            if (frown) {
                sink->BeginFigure(D2D1::Point2F(noseC.x - mw, my + wob), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(noseC.x, my - s * 0.045f),
                                                                      D2D1::Point2F(noseC.x + mw, my - wob)));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                rt->DrawGeometry(frown.Get(), brush(ink), lw * 1.3f);
            }
        } else {
            const float mw = s * 0.035f, my = noseC.y + s * 0.045f;
            const float smile = s * 0.02f;
            rt->DrawLine(noseC, D2D1::Point2F(noseC.x, my), brush(ink), lw);
            rt->DrawLine(D2D1::Point2F(noseC.x, my), D2D1::Point2F(noseC.x - mw, my + smile), brush(ink), lw);
            rt->DrawLine(D2D1::Point2F(noseC.x, my), D2D1::Point2F(noseC.x + mw, my + smile), brush(ink), lw);
        }
        if (L.whiskers) {
            const D2D1_COLOR_F wc = Hex(0x6B6470, 0.7f);
            for (int side = -1; side <= 1; side += 2)
                for (int i = -1; i <= 1; i += 2)
                    rt->DrawLine(D2D1::Point2F(noseC.x + side * s * 0.10f, noseC.y + i * s * 0.012f),
                                 D2D1::Point2F(noseC.x + side * s * 0.27f, noseC.y + i * s * 0.035f), brush(wc), lw * 0.8f);
        }
    }

    if (mood == Mood::Snack && t_drinking) {
        // Spidey drinks under his rolled-up mask, like his pizza.
        const D2D1_POINT_2F mouth = L.spiderman ? D2D1::Point2F(headC.x - headRx * 0.06f, headC.y + headRy * 0.78f)
                                                : D2D1::Point2F(headC.x, headC.y + headRy * 0.42f);
        WaterGlass(rt, cache, mouth, s, since, world);
    } else if (mood == Mood::Snack && t_streaming) {
        Popcorn(rt, cache, headC, headRx, headRy, s, t, world);
    } else if (mood == Mood::Snack) {
        // Each character has its own favourite.
        const D2D1_POINT_2F mouth = D2D1::Point2F(headC.x, headC.y + headRy * 0.42f);
        switch (st.character) {
            case Character::Robot: Battery(rt, cache, mouth, s, t, world); break;
            case Character::Dazai: break;  // his bun is in his hands (DazaiFace)
            case Character::Spiderman:  // up to the mouth under his rolled-up mask
                Pizza(rt, cache, D2D1::Point2F(headC.x - headRx * 0.06f, headC.y + headRy * 0.78f), s, t, world);
                break;
            case Character::Bunny: Carrot(rt, cache, mouth, s, t, world); break;
            case Character::Cat: Fish(rt, cache, mouth, s, t, world); break;
            case Character::Dog: Bone(rt, cache, mouth, s, t, world); break;
            case Character::Panda: Bamboo(rt, cache, mouth, s, t, world); break;
            default: Drumstick(rt, cache, mouth, s, t, world); break;
        }
    }
    if (mood == Mood::Sad && L.spiderman) {
        SnappedWeb(rt, cache, headC, headRx, headC.y - headRy * 1.14f, s, t, saved, world);
        // A bigger tear than the others': it has to read against the red mask.
        Tear(rt, cache, D2D1::Point2F(headC.x + headRx * 0.50f, headC.y + headRy * 0.22f), s * 0.06f, s * 1.6f, t);
    } else if (mood == Mood::Sad && !L.luffy && !L.dazai) {
        RainCloud(rt, cache, headC, headRx, headRy, s, t);
    }
    if (mood == Mood::Working) {
        // A laptop for (nearly) everyone: Dazai reads a book and Bolt's terminal is
        // on his visor. The originals get a smaller one just under the mouth, so it
        // stays inside their slot.
        if (L.luffy) Laptop(rt, cache, D2D1::Point2F(headC.x, headC.y + headRy * 0.80f), s, t, st.character, world);
        else if (L.dazai && t_live) Laptop(rt, cache, D2D1::Point2F(headC.x, headC.y + headRy * 0.80f), s, t, st.character, world);
        else if (L.dazai || L.robot) {}  // book (DazaiFace) / terminal visor (above)
        else if (L.spiderman) Laptop(rt, cache, D2D1::Point2F(headC.x, headC.y + headRy * 0.50f), s, t, st.character, world);
        else Laptop(rt, cache, D2D1::Point2F(headC.x, headC.y + headRy * 0.60f), s, t, st.character, world, 0.85f);
    }
    if (mood == Mood::Alert && !L.spiderman && !L.dazai) AlertBadge(rt, cache, headC, headRx, headRy, s, t, world);
    if (mood == Mood::Listening) ListenWaves(rt, cache, headC, headRx, headRy, s, t, world);
    if (mood == Mood::Talking) SpeechBubble(rt, cache, headC, headRx, headRy, s, t, world);
    if (mood == Mood::Confused) DizzyRing(rt, cache, headC, headRx, headRy, s, t, world, true);  // in front of the head
    if (mood == Mood::Thinking)
        ThinkQuestion(rt, cache, headC, headRx, headRy, s, t, world, Hex(0xFFF8EE), Hex(0x1A1622, 0.55f), L.spiderman);

    // ── Headphones + a floating note while music plays ──────────────────────
    // (Do Not Disturb wears them too, without the note.)
    if (mood == Mood::Music || t_dnd) {
        const D2D1_COLOR_F band = Hex(0x2D2B36);
        const D2D1_COLOR_F cushion = t_dnd ? Hex(0xA78BFA) : Hex(0xFF8FC2);
        const float cupY = headC.y + headRy * 0.02f;
        const float cupDx = headRx * (L.robot ? 1.02f : 0.97f);
        ComPtr<ID2D1Factory> f;
        rt->GetFactory(&f);
        ComPtr<ID2D1PathGeometry> arc;
        if (f && SUCCEEDED(f->CreatePathGeometry(&arc))) {
            ComPtr<ID2D1GeometrySink> sink;
            arc->Open(&sink);
            sink->BeginFigure(D2D1::Point2F(headC.x - cupDx, cupY - headRy * 0.15f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(headC.x + cupDx, cupY - headRy * 0.15f),
                                          D2D1::SizeF(cupDx, headRy * 1.12f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            rt->DrawGeometry(arc.Get(), brush(band), std::max(1.5f, s * 0.055f));
        }
        for (int side = -1; side <= 1; side += 2) {
            const D2D1_POINT_2F c = D2D1::Point2F(headC.x + side * cupDx, cupY);
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - s * 0.07f, c.y - s * 0.11f, c.x + s * 0.07f, c.y + s * 0.11f),
                                                       s * 0.06f, s * 0.06f), brush(band));
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - side * s * 0.01f - s * 0.03f, c.y - s * 0.075f,
                                                                   c.x - side * s * 0.01f + s * 0.03f, c.y + s * 0.075f),
                                                       s * 0.03f, s * 0.03f), brush(cushion));
        }
        // A note drifts up and fades, once every 1.6 s.
        const float phase = std::fmod(static_cast<float>(t) / 1.6f, 1.0f);
        const float ns = s * 0.07f;
        const D2D1_POINT_2F n = D2D1::Point2F(headC.x + headRx * 1.25f + phase * s * 0.06f, headC.y - phase * s * 0.35f);
        ID2D1Brush* nb = brush(D2D1::ColorF(0xFF8FC2, std::sin(phase * kPi)));
        if (!t_dnd) {
            rt->FillEllipse(D2D1::Ellipse(n, ns * 0.6f, ns * 0.45f), nb);
            rt->DrawLine(D2D1::Point2F(n.x + ns * 0.5f, n.y), D2D1::Point2F(n.x + ns * 0.5f, n.y - ns * 1.8f), nb, lw * 1.1f);
            rt->DrawLine(D2D1::Point2F(n.x + ns * 0.5f, n.y - ns * 1.8f), D2D1::Point2F(n.x + ns * 1.2f, n.y - ns * 1.3f), nb, lw * 1.1f);
        }
    }

    // ── Do Not Disturb: a moon badge; Live: a REC badge ─────────────────────
    if (t_dnd) DndMoonBadge(rt, cache, D2D1::Point2F(headC.x + headRx * 1.08f, headC.y - headRy * 0.95f), s, t, lw);
    if (t_live) RecBadge(rt, cache, D2D1::Point2F(headC.x + headRx * 1.02f, headC.y - headRy * 0.78f), s, t, lw);

    // ── Celebrating: sparks and confetti ────────────────────────────────────
    if (mood == Mood::Happy) Celebrate(rt, cache, headC, headRx, headRy, s, since, world, lw, false);

    // ── Charging: holding a plug, with a little spark now and then ──────────
    if (g_charging.load(std::memory_order_relaxed) && (mood == Mood::Idle || mood == Mood::Happy || mood == Mood::Sleepy)) {
        ChargingPlug(rt, cache, D2D1::Point2F(headC.x + headRx * 0.92f, headC.y + headRy * 0.78f), s, t, world);
    }

    // ── Sleepy "z" ──────────────────────────────────────────────────────────
    if (mood == Mood::Sleepy && !t_dnd) {
        const float phase = std::fmod(static_cast<float>(t) * 0.5f, 1.0f);
        const float zs = s * 0.11f;
        // Do Not Disturb's muted bell sits up there, so its "z" drifts up beside the cheek.
        const float zy = t_dnd ? headC.y + headRy * 0.30f : headC.y - headRy * 0.9f;
        const D2D1_POINT_2F z = D2D1::Point2F(headC.x + headRx * (t_dnd ? 1.05f : 0.9f) + phase * s * 0.1f, zy - phase * s * (t_dnd ? 0.18f : 0.25f));
        const float alpha = std::sin(phase * kPi);
        ID2D1Brush* glow = brush(D2D1::ColorF(0xB69CFF, alpha * 0.85f));
        ID2D1Brush* zb = brush(D2D1::ColorF(0xFFFFFF, alpha * 0.95f));
        // Outer soft glow
        rt->DrawLine(z, D2D1::Point2F(z.x + zs, z.y), glow, lw * 2.2f);
        rt->DrawLine(D2D1::Point2F(z.x + zs, z.y), D2D1::Point2F(z.x, z.y + zs), glow, lw * 2.2f);
        rt->DrawLine(D2D1::Point2F(z.x, z.y + zs), D2D1::Point2F(z.x + zs, z.y + zs), glow, lw * 2.2f);
        // Bright core
        rt->DrawLine(z, D2D1::Point2F(z.x + zs, z.y), zb, lw * 1.1f);
        rt->DrawLine(D2D1::Point2F(z.x + zs, z.y), D2D1::Point2F(z.x, z.y + zs), zb, lw * 1.1f);
        rt->DrawLine(D2D1::Point2F(z.x, z.y + zs), D2D1::Point2F(z.x + zs, z.y + zs), zb, lw * 1.1f);
    }

    rt->SetTransform(saved);
}

}  // namespace bb::mascot
