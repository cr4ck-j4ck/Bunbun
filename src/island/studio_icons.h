// Bunbun — the Studio layout's line icons, shared by the Windows island and the
// Linux island (which draws them through the Direct2D-on-Cairo stand-ins, like
// the mascot). Every icon sits in a 24-unit grid centred on `c`.
#pragma once

#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>

#include "studio_pages.h"

namespace bb::studio {

// What the icons draw with. `brush` hands out a solid brush in a colour (the
// Windows renderer's per-frame pool; a single recoloured brush on Linux).
struct Pen {
    ID2D1RenderTarget* rt = nullptr;
    ID2D1Factory* factory = nullptr;
    ID2D1StrokeStyle* round = nullptr;  // round caps and joins
    std::function<ID2D1SolidColorBrush*(const D2D1_COLOR_F&)> brush;
};

inline D2D1_COLOR_F Rgb(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, a);
}

inline void Line(const Pen& pen, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float w) {
    if (ID2D1SolidColorBrush* br = pen.brush(c)) pen.rt->DrawLine(a, b, br, w, pen.round);
}

// Open or closed polyline in the 24-unit grid, placed at `c` with `scale`.
inline void Poly(const Pen& pen, std::initializer_list<D2D1_POINT_2F> pts, D2D1_POINT_2F c, float scale, D2D1_COLOR_F col,
                 float width, bool closed = false, bool fill = false) {
    ID2D1PathGeometry* path = nullptr;
    if (FAILED(pen.factory->CreatePathGeometry(&path)) || !path) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(path->Open(&sink)) && sink) {
        bool first = true;
        for (const auto& p : pts) {
            const D2D1_POINT_2F q = D2D1::Point2F(c.x + (p.x - 12.0f) * scale, c.y + (p.y - 12.0f) * scale);
            if (first) { sink->BeginFigure(q, fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW); first = false; }
            else sink->AddLine(q);
        }
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        if (ID2D1SolidColorBrush* b = pen.brush(col)) {
            if (fill) pen.rt->FillGeometry(path, b);
            else pen.rt->DrawGeometry(path, b, width, pen.round);
        }
    }
    path->Release();
}

// An arc around `c` from angle a0 to a1 (radians, 0 = 3 o'clock, clockwise).
inline void Arc(const Pen& pen, D2D1_POINT_2F c, float r, float a0, float a1, D2D1_COLOR_F col, float width) {
    if (r <= 0.5f || std::fabs(a1 - a0) < 0.001f) return;
    if (std::fabs(a1 - a0) >= 6.2831f) {
        if (ID2D1SolidColorBrush* b = pen.brush(col)) pen.rt->DrawEllipse(D2D1::Ellipse(c, r, r), b, width);
        return;
    }
    ID2D1PathGeometry* path = nullptr;
    if (FAILED(pen.factory->CreatePathGeometry(&path)) || !path) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(path->Open(&sink)) && sink) {
        sink->BeginFigure(D2D1::Point2F(c.x + r * std::cos(a0), c.y + r * std::sin(a0)), D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_ARC_SEGMENT arc{};
        arc.point = D2D1::Point2F(c.x + r * std::cos(a1), c.y + r * std::sin(a1));
        arc.size = D2D1::SizeF(r, r);
        arc.rotationAngle = 0.0f;
        arc.sweepDirection = a1 > a0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
        arc.arcSize = std::fabs(a1 - a0) > 3.14159f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        sink->AddArc(arc);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        if (ID2D1SolidColorBrush* b = pen.brush(col)) pen.rt->DrawGeometry(path, b, width, pen.round);
    }
    path->Release();
}

// Progress ring from 12 o'clock: a faint track plus a coloured arc. `alpha`
// fades both (the frame fading in).
inline void Ring(const Pen& pen, D2D1_POINT_2F c, float r, float width, float fraction, D2D1_COLOR_F col, float alpha = 1.0f) {
    constexpr float kTop = -1.5707963f;
    Arc(pen, c, r, 0.0f, 6.2832f, D2D1::ColorF(1, 1, 1, 0.16f * alpha), width);
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    col.a *= alpha;
    if (fraction > 0.004f) Arc(pen, c, r, kTop, kTop + 6.2831853f * std::min(fraction, 0.9999f), col, width);
}

// One icon per page (the tab bar, Home cards, headers).
inline void Icon(const Pen& pen, Page page, D2D1_POINT_2F c, float size, D2D1_COLOR_F col) {
    const float s = size / 24.0f;
    const float w = 1.8f * s * (24.0f / 16.0f);
    auto P = [](float x, float y) { return D2D1::Point2F(x, y); };
    auto at = [&](float x, float y) { return D2D1::Point2F(c.x + (x - 12) * s, c.y + (y - 12) * s); };
    auto box = [&](float x0, float y0, float x1, float y1) { return D2D1::RectF(at(x0, y0).x, at(x0, y0).y, at(x1, y1).x, at(x1, y1).y); };
    ID2D1SolidColorBrush* b = pen.brush(col);
    if (!b) return;
    ID2D1RenderTarget* rt = pen.rt;
    switch (page) {
        case Page::Home:
            Poly(pen, {P(4, 11), P(12, 4), P(20, 11)}, c, s, col, w);
            Poly(pen, {P(6, 9.5f), P(6, 20), P(18, 20), P(18, 9.5f)}, c, s, col, w);
            break;
        case Page::Music:
            rt->DrawEllipse(D2D1::Ellipse(at(8, 17), 3 * s, 3 * s), b, w);
            Poly(pen, {P(11, 17), P(11, 4), P(18, 6)}, c, s, col, w);
            break;
        case Page::Calendar:
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box(4, 6, 20, 20), 3 * s, 3 * s), b, w);
            Poly(pen, {P(4, 10.5f), P(20, 10.5f)}, c, s, col, w);
            Poly(pen, {P(8, 3.5f), P(8, 7)}, c, s, col, w);
            Poly(pen, {P(16, 3.5f), P(16, 7)}, c, s, col, w);
            break;
        case Page::Weather:
            rt->DrawEllipse(D2D1::Ellipse(at(12, 12), 4 * s, 4 * s), b, w);
            for (int i = 0; i < 8; ++i) {
                const float a = i * 0.785398f;
                Line(pen, at(12 + 6.8f * std::cos(a), 12 + 6.8f * std::sin(a)), at(12 + 8.8f * std::cos(a), 12 + 8.8f * std::sin(a)),
                     col, w);
            }
            break;
        case Page::Hardware:
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box(6, 6, 18, 18), 2 * s, 2 * s), b, w);
            rt->DrawRectangle(box(9.5f, 9.5f, 14.5f, 14.5f), b, w * 0.8f);
            for (float k : {9.5f, 14.5f}) {
                Line(pen, at(k, 3), at(k, 6), col, w); Line(pen, at(k, 18), at(k, 21), col, w);
                Line(pen, at(3, k), at(6, k), col, w); Line(pen, at(18, k), at(21, k), col, w);
            }
            break;
        case Page::Connectivity:
            for (int i = 1; i <= 3; ++i) Arc(pen, at(12, 19), (i * 4.6f) * s, -2.356f, -0.785f, col, w);
            rt->FillEllipse(D2D1::Ellipse(at(12, 19), 1.4f * s, 1.4f * s), pen.brush(col));
            break;
        case Page::Tray:
            Poly(pen, {P(3.5f, 13), P(3.5f, 19), P(20.5f, 19), P(20.5f, 13)}, c, s, col, w);
            Poly(pen, {P(3.5f, 13), P(8.5f, 13), P(10, 15.5f), P(14, 15.5f), P(15.5f, 13), P(20.5f, 13)}, c, s, col, w);
            Poly(pen, {P(12, 4), P(12, 11)}, c, s, col, w);
            Poly(pen, {P(9, 8), P(12, 11), P(15, 8)}, c, s, col, w);
            break;
        case Page::Todo:
            Poly(pen, {P(3.5f, 7), P(5.5f, 9), P(9, 5)}, c, s, col, w);
            Poly(pen, {P(3.5f, 16), P(5.5f, 18), P(9, 14)}, c, s, col, w);
            Poly(pen, {P(12, 7), P(20.5f, 7)}, c, s, col, w);
            Poly(pen, {P(12, 16), P(20.5f, 16)}, c, s, col, w);
            break;
        case Page::Lyrics:
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box(9, 3, 15, 14), 3 * s, 3 * s), b, w);
            Arc(pen, at(12, 10), 6.5f * s, 0.15f, 2.99f, col, w);
            Poly(pen, {P(12, 16.5f), P(12, 21)}, c, s, col, w);
            break;
        case Page::Agent:
            for (int i = 0; i < 4; ++i) {
                const float a = i * 0.785398f;
                Line(pen, at(12 - 7.5f * std::cos(a), 12 - 7.5f * std::sin(a)), at(12 + 7.5f * std::cos(a), 12 + 7.5f * std::sin(a)),
                     col, w * 1.15f);
            }
            break;
        case Page::Clipboard:
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box(5, 5, 19, 21), 2.5f * s, 2.5f * s), b, w);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box(9, 3, 15, 7), 1.5f * s, 1.5f * s), b, w);
            break;
        case Page::Focus:
            rt->DrawEllipse(D2D1::Ellipse(at(12, 13.5f), 7.5f * s, 7.5f * s), b, w);
            Poly(pen, {P(12, 13.5f), P(12, 9.5f)}, c, s, col, w);
            Poly(pen, {P(9.5f, 3.5f), P(14.5f, 3.5f)}, c, s, col, w);
            break;
        case Page::Mirror:
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box(3, 7, 17, 18), 2.5f * s, 2.5f * s), b, w);
            Poly(pen, {P(17, 11), P(21, 8.5f), P(21, 16.5f), P(17, 14)}, c, s, col, w, true);
            break;
        case Page::Apps:
            for (float gx : {6.5f, 13.5f})
                for (float gy : {6.5f, 13.5f})
                    rt->DrawRoundedRectangle(D2D1::RoundedRect(box(gx - 3, gy - 3, gx + 3, gy + 3), 1.6f * s, 1.6f * s), b, w);
            break;
        case Page::Water:  // a drop: two sides meeting at the top, a round bottom
            Poly(pen, {P(6.2f, 14.5f), P(12, 3.5f), P(17.8f, 14.5f)}, c, s, col, w);
            Arc(pen, at(12, 14.5f), 5.8f * s, 0.0f, 3.14159265f, col, w);
            break;
        case Page::Note:
            Poly(pen, {P(6, 3.5f), P(14, 3.5f), P(19, 8.5f), P(19, 20.5f), P(6, 20.5f)}, c, s, col, w, true);
            Poly(pen, {P(9, 12), P(16, 12)}, c, s, col, w);
            Poly(pen, {P(9, 16), P(14, 16)}, c, s, col, w);
            break;
    }
}

inline void Gear(const Pen& pen, D2D1_POINT_2F c, float size, D2D1_COLOR_F col) {
    const float s = size / 24.0f;
    if (ID2D1SolidColorBrush* b = pen.brush(col)) {
        pen.rt->DrawEllipse(D2D1::Ellipse(c, 5.5f * s, 5.5f * s), b, 1.8f * s * 1.5f);
        pen.rt->DrawEllipse(D2D1::Ellipse(c, 2.0f * s, 2.0f * s), b, 1.5f * s * 1.5f);
    }
    for (int i = 0; i < 8; ++i) {
        const float a = i * 0.785398f + 0.39f;
        Line(pen, D2D1::Point2F(c.x + 6.5f * s * std::cos(a), c.y + 6.5f * s * std::sin(a)),
             D2D1::Point2F(c.x + 9.0f * s * std::cos(a), c.y + 9.0f * s * std::sin(a)), col, 2.6f * s * 1.5f);
    }
}

// A pipette: a slanted barrel with a bulb at the top and a drop at the tip.
inline void Eyedropper(const Pen& pen, D2D1_POINT_2F c, float size, D2D1_COLOR_F col) {
    const float s = size / 24.0f;
    const float w = 1.8f * s * 1.5f;
    auto P = [](float x, float y) { return D2D1::Point2F(x, y); };
    Poly(pen, {P(5, 19), P(14, 10)}, c, s, col, w * 1.6f);        // barrel
    Poly(pen, {P(12, 8), P(16, 12)}, c, s, col, w);               // collar
    Poly(pen, {P(14, 10), P(17.5f, 6.5f)}, c, s, col, w * 2.2f);  // bulb
    Poly(pen, {P(4, 20), P(5, 19)}, c, s, col, w);                // tip
}

inline void Speaker(const Pen& pen, D2D1_POINT_2F c, float size, int level, bool muted, D2D1_COLOR_F col) {
    const float s = size / 24.0f;
    auto P = [](float x, float y) { return D2D1::Point2F(x, y); };
    Poly(pen, {P(4, 9), P(8, 9), P(13, 4.5f), P(13, 19.5f), P(8, 15), P(4, 15)}, c, s, col, 0, true, true);
    if (muted) {
        Poly(pen, {P(16, 9), P(21, 15)}, c, s, col, 1.8f * s * 1.5f);
        Poly(pen, {P(21, 9), P(16, 15)}, c, s, col, 1.8f * s * 1.5f);
        return;
    }
    const D2D1_POINT_2F mid = D2D1::Point2F(c.x + (13 - 12) * s, c.y);
    if (level > 0) Arc(pen, mid, 3.5f * s, -0.9f, 0.9f, col, 1.8f * s * 1.5f);
    if (level > 1) Arc(pen, mid, 7.0f * s, -0.9f, 0.9f, col, 1.8f * s * 1.5f);
}

// A tick for checked options, in the 24-unit grid.
inline void Tick(const Pen& pen, D2D1_POINT_2F c, float scale, D2D1_COLOR_F col, float width = 1.8f) {
    auto P = [](float x, float y) { return D2D1::Point2F(x, y); };
    Poly(pen, {P(8.5f, 12), P(11, 14.5f), P(15.5f, 9.5f)}, c, scale, col, width);
}

}  // namespace bb::studio
