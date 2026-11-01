// Bunbun — mascot characters (see mascot.h). Original artwork, drawn in code.
#include "mascot.h"

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
    bool doom = false;
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
        case Character::Doom:
            l.ears = Ears::None;
            l.fur = Hex(0x98A1A8);         // steel mask
            l.eye = Hex(0x0C0E10);
            l.inner = Hex(0x1F5C2B);       // hood green
            l.doom = true;
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

// 0 = open, 1 = closed: a quick blink every `period` seconds.
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
        default: return L"idle.png";
    }
}

// Decoded once per (render target, file); D2D bitmaps belong to their target.
struct PackCache {
    ID2D1RenderTarget* owner = nullptr;
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> bitmaps;
};
PackCache& Packs() {
    thread_local PackCache* cache = new PackCache;  // see Brushes()
    return *cache;
}

ID2D1Bitmap* PackBitmap(ID2D1RenderTarget* rt, const std::wstring& pack, Mood mood) {
    PackCache& t_pack = Packs();
    if (t_pack.owner != rt) { t_pack.bitmaps.clear(); t_pack.owner = rt; }
    const std::wstring dir = PacksFolder() + L"\\" + pack + L"\\";
    for (const wchar_t* file : {MoodFile(mood), L"idle.png"}) {
        const std::wstring path = dir + file;
        auto it = t_pack.bitmaps.find(path);
        if (it != t_pack.bitmaps.end()) {
            if (it->second) return it->second.Get();
            continue;  // known missing
        }
        ComPtr<ID2D1Bitmap>& slot = t_pack.bitmaps[path];
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICBitmapDecoder> dec;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
            SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                       WICBitmapPaletteTypeMedianCut))) {
            rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &slot);
        }
        if (slot) return slot.Get();
    }
    return nullptr;
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
            if (st.character == Character::Spiderman || st.character == Character::Doom) {
                // Spidey-Sense tingling shudder (Doom shares it, with his green aura)
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

void DrawPack(ID2D1RenderTarget* rt, const Style& st, D2D1_POINT_2F c, float s, Mood mood, double t, float since) {
    ID2D1Bitmap* bmp = PackBitmap(rt, st.customPack, mood);
    if (!bmp) return;
    const Motion m = BodyMotion(st, mood, t, since, s);
    const D2D1_SIZE_F bs = bmp->GetSize();
    const float fit = s / std::max(bs.width, bs.height);
    const float w = bs.width * fit, h = bs.height * fit * m.squash;
    const D2D1_POINT_2F base = D2D1::Point2F(c.x, c.y + s * 0.5f);  // squash from the feet
    D2D1_MATRIX_3X2_F saved;
    rt->GetTransform(&saved);
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(m.tilt, base) * saved);
    rt->DrawBitmap(bmp, D2D1::RectF(c.x - w * 0.5f, base.y - h + m.bob, c.x + w * 0.5f, base.y + m.bob), 1.0f,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    rt->SetTransform(saved);
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

void SpideyHead(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s,
                const D2D1_MATRIX_3X2_F& world) {
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

    // Shaded mask: deep red rim below, the suit red, a soft highlight top-left.
    if (egg) {
        rt->SetTransform(D2D1::Matrix3x2F::Translation(0, ry * 0.05f) * world);
        rt->FillGeometry(egg.Get(), brush(Hex(0x8E1219)));
        rt->SetTransform(D2D1::Matrix3x2F::Scale(0.985f, 0.97f, D2D1::Point2F(c.x, c.y - ry * 0.02f)) * world);
        rt->FillGeometry(egg.Get(), brush(Hex(0xD3222C)));
        rt->SetTransform(world);
    }
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - rx * 0.30f, c.y - ry * 0.52f), rx * 0.42f, ry * 0.24f),
                    brush(Hex(0xFF5A63, 0.30f)));

    // The web, clipped to the head.
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(&f);
    ComPtr<ID2D1Layer> layer;
    if (egg && SUCCEEDED(rt->CreateLayer(&layer))) {
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), egg.Get()), layer.Get());
        const D2D1_COLOR_F webCol = Hex(0x4A0A10, 0.62f);
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
    if (egg) rt->DrawGeometry(egg.Get(), brush(Hex(0x5E0B12, 0.9f)), std::max(0.8f, s * 0.012f));
    (void)head;
}

void SpideyLenses(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, Mood mood,
                  double t, float since, const Style& st, D2D1_POINT_2F look, const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
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
            case Mood::Sad: rot = side * 16.0f; sy = 0.80f; break;     // outer corners droop
            case Mood::Alert: sx = sy = 1.14f; break;                  // wide open
            case Mood::Thinking: sy = 0.78f; lift = -ry * 0.07f; rot = side * -6.0f; break;
            case Mood::Working: sy = 0.86f; break;                     // focused
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
    }
    rt->SetTransform(world);
}

// ── Doom ────────────────────────────────────────────────────────────────────
// A chibi Doctor Doom: a steel mask (stern brow, eye slits, nose ridge, mouth
// grille, rivets) framed by his deep green hood. Needs-you: a pulsing green
// aura with crackling energy, and the same shudder as Spidey's sense.
// Thinking: a floating green question mark.

const D2D1_COLOR_F kDoomGreen = {0.36f, 1.0f, 0.52f, 1.0f};

void DoomAura(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    const float tp = static_cast<float>(t);
    const float pulse = 0.78f + 0.22f * std::sin(tp * 6.0f);  // always visible, breathing
    // Soft rings of light behind the hood.
    for (int i = 0; i < 4; ++i) {
        const float grow = i * s * 0.05f + pulse * s * 0.02f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + ry * 0.05f), rx * 1.45f + grow, ry * 1.55f + grow),
                        brush(D2D1::ColorF(0.22f, 1.0f, 0.42f, (0.30f - i * 0.065f) * pulse)));
    }
    // Crackling energy arcs.
    const float w = std::max(1.2f, s * 0.022f);
    for (float deg : {-70.0f, -40.0f, -12.0f, 16.0f, 44.0f, 72.0f}) {
        const float a = deg * (kPi / 180.0f) + std::sin(tp * 4.0f + deg) * 0.06f;
        const float dx = std::sin(a), dy = -std::cos(a);
        const float r0 = rx * 1.30f, r1 = rx * 1.52f + std::sin(tp * 9.0f + deg) * s * 0.02f, r2 = rx * 1.72f;
        const float px = -dy * s * 0.03f, py = dx * s * 0.03f;
        const D2D1_POINT_2F p0 = D2D1::Point2F(c.x + dx * r0, c.y + dy * r0 * 1.05f);
        const D2D1_POINT_2F p1 = D2D1::Point2F(c.x + dx * r1 + px, c.y + dy * r1 * 1.05f + py);
        const D2D1_POINT_2F p2 = D2D1::Point2F(c.x + dx * r2 - px, c.y + dy * r2 * 1.05f - py);
        const D2D1_COLOR_F glow = D2D1::ColorF(0.18f, 0.95f, 0.40f, 0.75f * pulse);
        rt->DrawLine(p0, p1, brush(glow), w * 2.2f);
        rt->DrawLine(p1, p2, brush(glow), w * 2.2f);
        const D2D1_COLOR_F core = D2D1::ColorF(0.75f, 1.0f, 0.80f, 0.95f * pulse);
        rt->DrawLine(p0, p1, brush(core), w);
        rt->DrawLine(p1, p2, brush(core), w);
    }
}

// The hood behind the head: a soft peak, falling to the shoulders.
ComPtr<ID2D1PathGeometry> DoomHoodShape(ID2D1RenderTarget* rt, D2D1_POINT_2F c, float rx, float ry) {
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (!g) return g;
    const D2D1_POINT_2F peak = D2D1::Point2F(c.x, c.y - ry * 1.32f);
    sink->BeginFigure(peak, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx * 0.70f, c.y - ry * 1.30f),
                                        D2D1::Point2F(c.x + rx * 1.28f, c.y - ry * 0.40f),
                                        D2D1::Point2F(c.x + rx * 1.32f, c.y + ry * 0.98f)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx * 0.80f, c.y + ry * 1.12f),
                                        D2D1::Point2F(c.x - rx * 0.80f, c.y + ry * 1.12f),
                                        D2D1::Point2F(c.x - rx * 1.32f, c.y + ry * 0.98f)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - rx * 1.28f, c.y - ry * 0.40f),
                                        D2D1::Point2F(c.x - rx * 0.70f, c.y - ry * 1.30f), peak));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return g;
}

// The steel mask: broad forehead, cheeks tapering to the chin.
ComPtr<ID2D1PathGeometry> DoomMaskShape(ID2D1RenderTarget* rt, D2D1_POINT_2F c, float rx, float ry) {
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1PathGeometry> g = NewPath(rt, &sink);
    if (!g) return g;
    const D2D1_POINT_2F top = D2D1::Point2F(c.x, c.y - ry * 0.78f);
    sink->BeginFigure(top, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx * 0.55f, c.y - ry * 0.78f),
                                        D2D1::Point2F(c.x + rx * 0.74f, c.y - ry * 0.55f),
                                        D2D1::Point2F(c.x + rx * 0.74f, c.y - ry * 0.10f)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x + rx * 0.74f, c.y + ry * 0.40f),
                                        D2D1::Point2F(c.x + rx * 0.42f, c.y + ry * 0.86f),
                                        D2D1::Point2F(c.x, c.y + ry * 0.90f)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - rx * 0.42f, c.y + ry * 0.86f),
                                        D2D1::Point2F(c.x - rx * 0.74f, c.y + ry * 0.40f),
                                        D2D1::Point2F(c.x - rx * 0.74f, c.y - ry * 0.10f)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - rx * 0.74f, c.y - ry * 0.55f),
                                        D2D1::Point2F(c.x - rx * 0.55f, c.y - ry * 0.78f), top));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return g;
}

void DoomHead(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s,
              const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    rt->SetTransform(world);

    // Hood: body, a lit left fold and a shaded right one, the dark opening.
    if (ComPtr<ID2D1PathGeometry> hood = DoomHoodShape(rt, c, rx, ry)) {
        rt->FillGeometry(hood.Get(), brush(Hex(0x1F5C2B)));
        ComPtr<ID2D1Layer> layer;
        if (SUCCEEDED(rt->CreateLayer(&layer))) {
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), hood.Get()), layer.Get());
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - rx * 0.85f, c.y - ry * 0.35f), rx * 0.45f, ry * 1.1f),
                            brush(Hex(0x2F8240, 0.55f)));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + rx * 0.95f, c.y + ry * 0.25f), rx * 0.40f, ry * 1.0f),
                            brush(Hex(0x0F3317, 0.55f)));
            rt->PopLayer();
        }
        rt->DrawGeometry(hood.Get(), brush(Hex(0x0C2912)), std::max(0.8f, s * 0.014f));
    }
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + ry * 0.02f), rx * 0.86f, ry * 0.98f), brush(Hex(0x0B1F0F)));

    // Steel mask, lit from the top left.
    ComPtr<ID2D1PathGeometry> mask = DoomMaskShape(rt, c, rx, ry);
    if (!mask) return;
    rt->FillGeometry(mask.Get(), brush(Hex(0x98A1A8)));
    ComPtr<ID2D1Layer> layer;
    if (SUCCEEDED(rt->CreateLayer(&layer))) {
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get()), layer.Get());
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - rx * 0.35f, c.y - ry * 0.50f), rx * 0.45f, ry * 0.32f),
                        brush(Hex(0xD5DBDF, 0.55f)));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + rx * 0.10f, c.y + ry * 0.85f), rx * 0.80f, ry * 0.35f),
                        brush(Hex(0x5D656C, 0.55f)));
        rt->PopLayer();
    }
    rt->DrawGeometry(mask.Get(), brush(Hex(0x363C42)), std::max(0.9f, s * 0.018f));

    const float lw = std::max(0.9f, s * 0.018f);
    const D2D1_COLOR_F ink = Hex(0x2A2F34);
    // Stern brow, dipping at the centre.
    rt->DrawLine(D2D1::Point2F(c.x - rx * 0.56f, c.y - ry * 0.30f), D2D1::Point2F(c.x, c.y - ry * 0.20f), brush(ink), lw * 1.6f);
    rt->DrawLine(D2D1::Point2F(c.x, c.y - ry * 0.20f), D2D1::Point2F(c.x + rx * 0.56f, c.y - ry * 0.30f), brush(ink), lw * 1.6f);
    // Nose ridge.
    rt->DrawLine(D2D1::Point2F(c.x, c.y - ry * 0.16f), D2D1::Point2F(c.x, c.y + ry * 0.26f), brush(Hex(0x5F676E)), lw);
    rt->DrawLine(D2D1::Point2F(c.x - rx * 0.08f, c.y + ry * 0.28f), D2D1::Point2F(c.x + rx * 0.08f, c.y + ry * 0.28f),
                 brush(Hex(0x5F676E)), lw);
    // Mouth grille.
    const D2D1_RECT_F grille = D2D1::RectF(c.x - rx * 0.27f, c.y + ry * 0.42f, c.x + rx * 0.27f, c.y + ry * 0.58f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(grille, s * 0.02f, s * 0.02f), brush(Hex(0x15181B)));
    for (int i = 1; i < 6; ++i) {
        const float x = grille.left + (grille.right - grille.left) * i / 6.0f;
        rt->DrawLine(D2D1::Point2F(x, grille.top + s * 0.006f), D2D1::Point2F(x, grille.bottom - s * 0.006f),
                     brush(Hex(0x7F888F)), std::max(0.7f, s * 0.010f));
    }
    // Rivets.
    for (int side = -1; side <= 1; side += 2) {
        for (const D2D1_POINT_2F& p : {D2D1::Point2F(c.x + side * rx * 0.60f, c.y + ry * 0.12f),
                                       D2D1::Point2F(c.x + side * rx * 0.46f, c.y + ry * 0.56f)}) {
            rt->FillEllipse(D2D1::Ellipse(p, s * 0.016f, s * 0.016f), brush(Hex(0xD3D9DD)));
            rt->DrawEllipse(D2D1::Ellipse(p, s * 0.016f, s * 0.016f), brush(Hex(0x40464C)), std::max(0.6f, s * 0.006f));
        }
    }
    // The hood's rim drapes over the mask's edges.
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + ry * 0.04f), rx * 0.86f, ry * 0.99f), brush(Hex(0x1F5C2B)),
                    std::max(1.5f, s * 0.07f));
}

void DoomEyes(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, Mood mood,
              double t, const Style& st, D2D1_POINT_2F look, const D2D1_MATRIX_3X2_F& world) {
    auto brush = [&](D2D1_COLOR_F col) { return cache.Get(rt, col); };
    const float lookX = std::clamp(look.x, -1.0f, 1.0f) * s * 0.012f;
    const bool glow = mood == Mood::Alert || mood == Mood::Thinking;
    for (int side = -1; side <= 1; side += 2) {
        const D2D1_POINT_2F e = D2D1::Point2F(c.x + side * rx * 0.30f + lookX, c.y - ry * 0.08f);
        float h = ry * 0.11f, rot = side * -8.0f;
        switch (mood) {
            case Mood::Happy:
            case Mood::Music: h *= 0.45f; break;           // smug squint
            case Mood::Sleepy: h *= 0.18f; break;
            case Mood::Sad: rot = side * 10.0f; break;
            case Mood::Alert: h *= 1.25f; break;
            default: break;
        }
        h *= 1.0f - 0.85f * Smooth(BlinkAmount(t + (side > 0 ? 0.02 : 0.0), st.blinkSeconds));
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(rot, e) * world);
        const D2D1_RECT_F slit = D2D1::RectF(e.x - rx * 0.17f, e.y - h, e.x + rx * 0.17f, e.y + h);
        rt->FillRoundedRectangle(D2D1::RoundedRect(slit, h, h), brush(Hex(0x0C0E10)));
        if (glow) {
            const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(t) * 7.0f);
            D2D1_COLOR_F g = kDoomGreen;
            g.a = 0.9f * pulse;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(slit.left + rx * 0.05f, slit.top + h * 0.35f,
                                                                   slit.right - rx * 0.05f, slit.bottom - h * 0.35f),
                                                       h * 0.4f, h * 0.4f), brush(g));
        }
    }
    rt->SetTransform(world);
}

// A green question mark drifting beside the head while thinking.
// A bobbing "?" beside the head while thinking. Doom's is green; everyone else's
// is a soft white with a dark drop shadow.
void ThinkQuestion(ID2D1RenderTarget* rt, BrushCache& cache, D2D1_POINT_2F c, float rx, float ry, float s, double t,
                   const D2D1_MATRIX_3X2_F& world, D2D1_COLOR_F ink, D2D1_COLOR_F shadow) {
    const float bob = std::sin(static_cast<float>(t) * 2.4f) * s * 0.03f;
    const float q = s * 0.17f;
    const D2D1_POINT_2F o = D2D1::Point2F(c.x + rx * 1.15f, c.y - ry * 1.05f + bob);
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
    rt->SetTransform(D2D1::Matrix3x2F::Translation(q * 0.05f, q * 0.05f) * world);
    rt->DrawGeometry(hook.Get(), cache.Get(rt, shadow), w);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y + q * 0.58f), w * 0.62f, w * 0.62f), cache.Get(rt, shadow));
    rt->SetTransform(world);
    rt->DrawGeometry(hook.Get(), cache.Get(rt, ink), w);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(o.x, o.y + q * 0.58f), w * 0.62f, w * 0.62f), cache.Get(rt, ink));
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

}  // namespace

// ── Style ───────────────────────────────────────────────────────────────────

void SetStyle(const Style& style) {
    std::lock_guard lock(g_styleMutex);
    g_style = style;
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
        case Character::Doom: return L"doom";
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
        for (Character c : {Character::Cat, Character::Dog, Character::Fox, Character::Panda, Character::Robot, Character::Spiderman, Character::Doom})
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
    // Spidey and Doom keep their own colours, and skin packs are images.
    if (s.character == Character::Spiderman || s.character == Character::Doom || s.character == Character::Custom) hex.clear();
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
        if (e.is_directory(ec) && std::filesystem::exists(e.path() / L"idle.png", ec))
            out.push_back(e.path().filename().wstring());
    }
    std::sort(out.begin(), out.end());
    return out;
}

void ReleaseThreadResources() {
    BrushCache& b = Brushes();
    b.brush.Reset();
    b.owner = nullptr;
    PackCache& p = Packs();
    p.bitmaps.clear();
    p.owner = nullptr;
}

void SetMusicEnergy(float energy) {
    g_musicEnergy.store(std::clamp(energy, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool NeedsFastFrames(Mood mood, double t, double moodSince) {
    switch (mood) {
        case Mood::Alert: return true;
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
        case Character::Spiderman: return {-0.19f, 0.50f, 0.36f};
        case Character::Doom: return {-0.26f, 0.50f, 0.42f};    // hood
        default: return {-0.50f, 0.50f, 0.50f};                  // skin packs fill their square
    }
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
        DrawPack(rt, st, center, s, mood, t, since);
        return;
    }
    if (!st.eyesFollow) look = D2D1::Point2F(0, 0);
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

    // Doom's aura glows behind everything else.
    if (L.doom && mood == Mood::Alert) {
        rt->SetTransform(world);
        DoomAura(rt, cache, headC, headRx, headRy * 1.05f, s, t);
    }

    // ── Ears / antenna (behind the head) ─────────────────────────────────────
    const float k = st.motion;
    float spread = 13.0f, wiggle = std::sin(static_cast<float>(t) * 1.7f) * 2.0f * k, lenK = 1.0f;
    switch (mood) {
        case Mood::Working: wiggle = std::sin(static_cast<float>(t) * 4.0f) * 6.0f * k; break;
        case Mood::Alert: spread = 4.0f; wiggle = std::sin(static_cast<float>(t) * 18.0f) * 3.5f * k; lenK = 1.12f; break;
        case Mood::Happy: spread = 18.0f; wiggle = std::sin(since * 10.0f) * 5.0f * k; break;
        case Mood::Sad: spread = 62.0f; lenK = 0.92f; break;
        case Mood::Sleepy: spread = 48.0f; lenK = 0.94f; break;
        case Mood::Music: spread = 16.0f; wiggle = std::sin(static_cast<float>(t) * kPi * 2.0f) * 7.0f * k; break;
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
                const float lift = (mood == Mood::Alert ? -18.0f : mood == Mood::Happy ? -10.0f : 0.0f);
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
                const float glow = mood == Mood::Alert ? 0.6f + 0.4f * std::sin(static_cast<float>(t) * 12.0f) : 1.0f;
                const D2D1_COLOR_F tipColor = mood == Mood::Snack ? Hex(0x4CF08A) : mood == Mood::Sad ? Hex(0xFF5A5A) : L.inner;
                rt->FillEllipse(D2D1::Ellipse(tip, s * 0.05f, s * 0.05f), brush(D2D1::ColorF(tipColor.r, tipColor.g, tipColor.b, glow)));
                break;
            }
            case Ears::None: break;
        }
    }
    rt->SetTransform(world);

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
    } else if (L.doom) {
        DoomHead(rt, cache, headC, headRx, headRy * 1.05f, s, world);
    } else if (L.spiderman) {
        SpideyHead(rt, cache, headC, headRx * 0.94f, headRy * 1.14f, s, world);
    } else {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(headC.x, headC.y + headRy * 0.06f), headRx, headRy), brush(Shade(L.fur, 0.9f)));
        rt->FillEllipse(D2D1::Ellipse(headC, headRx, headRy * 0.97f), brush(L.fur));
        if (L.muzzle) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(headC.x, headC.y + headRy * 0.42f), headRx * 0.62f, headRy * 0.50f),
                            brush(L.muzzleColor));
        }
    }

    const float eyeDx = headRx * 0.40f;
    const float eyeY = headC.y + headRy * (L.robot ? -0.05f : (L.spiderman ? 0.04f : 0.02f));
    const float lookX = std::clamp(look.x, -1.0f, 1.0f) * s * 0.035f;
    float lookY = std::clamp(look.y, -1.0f, 1.0f) * s * 0.025f;
    if (mood == Mood::Thinking) lookY = -s * 0.03f;
    const float eyeR = s * (mood == Mood::Alert ? 0.068f : 0.06f);
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
    if (!L.robot && !L.spiderman && !L.doom) {
        const float cheekY = headC.y + headRy * 0.30f;
        for (int side = -1; side <= 1; side += 2)
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(headC.x + side * headRx * 0.58f, cheekY), s * 0.065f, s * 0.04f),
                            brush(L.blush));
    }

    if (L.doom) DoomEyes(rt, cache, headC, headRx, headRy * 1.05f, s, mood, t, st, look, world);
    if (L.spiderman) SpideyLenses(rt, cache, headC, headRx * 0.94f, headRy * 1.14f, s, mood, t, since, st, look, world);

    const bool terminalEyes = L.robot && mood == Mood::Snack;
    if (terminalEyes) {
        TerminalVisor(rt, cache, D2D1::RectF(headC.x - headRx + s * 0.06f, headC.y - headRy * 0.55f,
                                             headC.x + headRx - s * 0.06f, headC.y + headRy * 0.45f), s, t);
    }

    // ── Eyes ────────────────────────────────────────────────────────────────
    const D2D1_COLOR_F eyeColor = L.patches ? Hex(0x0B0B0D) : L.eye;
    for (int side = -1; side <= 1; side += 2) {
        const D2D1_POINT_2F ec = D2D1::Point2F(headC.x + side * eyeDx + lookX, eyeY + lookY);
        if (L.spiderman || L.doom || terminalEyes) continue;  // drawn by SpideyLenses / DoomEyes / TerminalVisor
        D2D1_COLOR_F drawEyeColor = eyeColor;
        if (L.patches && (mood == Mood::Happy || mood == Mood::Music || mood == Mood::Sleepy || mood == Mood::Sad)) {
            // White curve over black panda eye patches for clean visibility
            drawEyeColor = Hex(0xFFFFFF);
        }
        switch (mood) {
            case Mood::Happy:
            case Mood::Snack:
            case Mood::Music: EyeLine(rt, brush(drawEyeColor), ec, eyeR, eyeR * 0.9f, stroke * (L.patches ? 1.25f : 1.0f)); break;
            case Mood::Sleepy: EyeLine(rt, brush(drawEyeColor), ec, eyeR, -eyeR * 0.25f, stroke * (L.patches ? 1.25f : 1.0f)); break;
            case Mood::Sad: {
                if (L.robot) {
                    // Bolt: red X eyes with a faint glitch flicker.
                    const float flick = std::fmod(static_cast<float>(t) * 7.0f + side, 5.0f) < 0.4f ? 0.45f : 1.0f;
                    const float r = eyeR * 0.75f;
                    ID2D1Brush* red = brush(Hex(0xFF5A5A, flick));
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
                const float blink = BlinkAmount(t + (side > 0 ? 0.03 : 0.0), st.blinkSeconds);
                const float ry = eyeR * 1.15f * (1.0f - 0.9f * Smooth(blink));
                if (L.robot) {
                    // LED eyes: rounded bars that squash to a line on blink.
                    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ec.x - eyeR * 0.8f, ec.y - ry, ec.x + eyeR * 0.8f, ec.y + ry),
                                                               eyeR * 0.4f, eyeR * 0.4f), brush(eyeColor));
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
    if (L.robot) {
        const float mw = s * (mood == Mood::Happy ? 0.09f : 0.06f);
        rt->DrawLine(D2D1::Point2F(noseC.x - mw, noseC.y), D2D1::Point2F(noseC.x + mw, noseC.y), brush(L.eye), lw * 1.3f);
    } else if (!L.spiderman && !L.doom) {
        rt->FillEllipse(D2D1::Ellipse(noseC, s * 0.035f, s * 0.024f), brush(L.nose));
        const D2D1_COLOR_F ink = Hex(0x2A2230);
        if (mood == Mood::Alert) {
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(noseC.x, noseC.y + s * 0.07f), s * 0.025f, s * 0.03f), brush(ink), lw * 1.1f);
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

    if (mood == Mood::Snack && !L.robot) {
        // Each character has its own favourite; Bolt's is drawn on the visor instead.
        const D2D1_POINT_2F mouth = D2D1::Point2F(headC.x, headC.y + headRy * 0.42f);
        switch (st.character) {
            case Character::Bunny: Carrot(rt, cache, mouth, s, t, world); break;
            case Character::Cat: Fish(rt, cache, mouth, s, t, world); break;
            case Character::Dog: Bone(rt, cache, mouth, s, t, world); break;
            case Character::Panda: Bamboo(rt, cache, mouth, s, t, world); break;
            default: Drumstick(rt, cache, mouth, s, t, world); break;
        }
    }
    if (mood == Mood::Sad && !L.spiderman && !L.doom) RainCloud(rt, cache, headC, headRx, headRy, s, t);
    if (mood == Mood::Alert && !L.spiderman && !L.doom) AlertBadge(rt, cache, headC, headRx, headRy, s, t, world);
    if (mood == Mood::Thinking && !L.spiderman) {
        if (L.doom) ThinkQuestion(rt, cache, headC, headRx, headRy * 1.05f, s, t, world, kDoomGreen, Hex(0x0B3D17, 0.8f));
        else ThinkQuestion(rt, cache, headC, headRx, headRy, s, t, world, Hex(0xFFF8EE), Hex(0x1A1622, 0.55f));
    }

    // ── Headphones + a floating note while music plays ──────────────────────
    if (mood == Mood::Music) {
        const D2D1_COLOR_F band = Hex(0x2D2B36);
        const D2D1_COLOR_F cushion = Hex(0xFF8FC2);
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
        rt->FillEllipse(D2D1::Ellipse(n, ns * 0.6f, ns * 0.45f), nb);
        rt->DrawLine(D2D1::Point2F(n.x + ns * 0.5f, n.y), D2D1::Point2F(n.x + ns * 0.5f, n.y - ns * 1.8f), nb, lw * 1.1f);
        rt->DrawLine(D2D1::Point2F(n.x + ns * 0.5f, n.y - ns * 1.8f), D2D1::Point2F(n.x + ns * 1.2f, n.y - ns * 1.3f), nb, lw * 1.1f);
    }

    // ── Sleepy "z" ──────────────────────────────────────────────────────────
    if (mood == Mood::Sleepy) {
        const float phase = std::fmod(static_cast<float>(t) * 0.5f, 1.0f);
        const float zs = s * 0.11f;
        const D2D1_POINT_2F z = D2D1::Point2F(headC.x + headRx * 0.9f + phase * s * 0.1f, headC.y - headRy * 0.9f - phase * s * 0.25f);
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
