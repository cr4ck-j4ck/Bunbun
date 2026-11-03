// Bunbun — the Direct2D stand-ins of d2d1.h, drawn with Cairo.
//
// Conventions that matter for matching Direct2D:
//   - matrices: D2D's row-vector 3x2 (_11 _12 / _21 _22 / _31 _32) maps to
//     cairo's {xx=_11, yx=_12, xy=_21, yy=_22, x0=_31, y0=_32};
//   - path geometries fill with the even-odd rule (D2D's default, ALTERNATE)
//     and skip hollow figures;
//   - lines have flat caps unless a stroke style says otherwise;
//   - cairo_save/restore (clips, layers) would also restore the matrix, so
//     every draw call sets the target's own transform first.
#include "d2d1.h"

#include <cairo.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

cairo_t* Cr(const ID2D1RenderTarget* rt) { return static_cast<cairo_t*>(rt->cr); }

cairo_matrix_t ToCairo(const D2D1_MATRIX_3X2_F& m) {
    cairo_matrix_t c;
    cairo_matrix_init(&c, m._11, m._12, m._21, m._22, m._31, m._32);
    return c;
}

void ApplyWorld(const ID2D1RenderTarget* rt, const D2D1_MATRIX_3X2_F& world) {
    const cairo_matrix_t c = ToCairo(world * rt->base);
    cairo_set_matrix(Cr(rt), &c);
}

void ApplyTransform(const ID2D1RenderTarget* rt) { ApplyWorld(rt, rt->transform); }

// Sets `brush` as the source. Brush coordinates are in the current user space.
void SetSource(cairo_t* cr, ID2D1Brush* brush) {
    if (auto* s = dynamic_cast<ID2D1SolidColorBrush*>(brush)) {
        cairo_set_source_rgba(cr, s->color.r, s->color.g, s->color.b, s->color.a * s->opacity);
        return;
    }
    if (auto* r = dynamic_cast<ID2D1RadialGradientBrush*>(brush)) {
        const auto& p = r->props;
        const float rx = std::max(0.001f, p.radiusX), ry = std::max(0.001f, p.radiusY);
        // A unit circle at the origin, stretched to the ellipse by the pattern matrix.
        cairo_pattern_t* pat = cairo_pattern_create_radial(p.gradientOriginOffset.x / rx, p.gradientOriginOffset.y / ry, 0,
                                                           0, 0, 1);
        for (UINT32 i = 0; i < r->count; ++i) {
            const auto& st = r->stops[i];
            cairo_pattern_add_color_stop_rgba(pat, st.position, st.color.r, st.color.g, st.color.b, st.color.a * r->opacity);
        }
        cairo_matrix_t m;
        cairo_matrix_init(&m, rx, 0, 0, ry, p.center.x, p.center.y);
        cairo_matrix_invert(&m);  // pattern matrices map user space to pattern space
        cairo_pattern_set_matrix(pat, &m);
        cairo_set_source(cr, pat);
        cairo_pattern_destroy(pat);
        return;
    }
    if (auto* b = dynamic_cast<ID2D1BitmapBrush*>(brush); b && b->bitmap) {
        cairo_pattern_t* pat = cairo_pattern_create_for_surface(static_cast<cairo_surface_t*>(b->bitmap->surface));
        cairo_matrix_t m = ToCairo(b->transform);
        cairo_matrix_invert(&m);
        cairo_pattern_set_matrix(pat, &m);
        cairo_pattern_set_filter(pat, b->interpolation == D2D1_BITMAP_INTERPOLATION_MODE_LINEAR ? CAIRO_FILTER_GOOD
                                                                                                 : CAIRO_FILTER_NEAREST);
        cairo_set_source(cr, pat);
        cairo_pattern_destroy(pat);
        return;
    }
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
}

void SetStroke(cairo_t* cr, float width, ID2D1StrokeStyle* style) {
    cairo_set_line_width(cr, width);
    cairo_line_cap_t cap = CAIRO_LINE_CAP_BUTT;
    cairo_line_join_t join = CAIRO_LINE_JOIN_MITER;
    if (style) {
        if (style->props.startCap == D2D1_CAP_STYLE_ROUND) cap = CAIRO_LINE_CAP_ROUND;
        else if (style->props.startCap == D2D1_CAP_STYLE_SQUARE) cap = CAIRO_LINE_CAP_SQUARE;
        if (style->props.lineJoin == D2D1_LINE_JOIN_ROUND) join = CAIRO_LINE_JOIN_ROUND;
        else if (style->props.lineJoin == D2D1_LINE_JOIN_BEVEL) join = CAIRO_LINE_JOIN_BEVEL;
        cairo_set_miter_limit(cr, style->props.miterLimit > 0 ? style->props.miterLimit : 10.0);
    } else {
        cairo_set_miter_limit(cr, 10.0);
    }
    cairo_set_line_cap(cr, cap);
    cairo_set_line_join(cr, join);
}

void RoundedRectPath(cairo_t* cr, const D2D1_ROUNDED_RECT& r) {
    const double x = r.rect.left, y = r.rect.top, w = r.rect.right - r.rect.left, h = r.rect.bottom - r.rect.top;
    const double rx = std::clamp<double>(r.radiusX, 0, w / 2), ry = std::clamp<double>(r.radiusY, 0, h / 2);
    if (rx <= 0 || ry <= 0) { cairo_rectangle(cr, x, y, w, h); return; }
    cairo_new_sub_path(cr);
    cairo_save(cr);
    cairo_matrix_t m;
    cairo_get_matrix(cr, &m);
    // Elliptical corners: draw unit-circle arcs under a per-corner scale.
    const auto corner = [&](double cx, double cy, double a0, double a1) {
        cairo_matrix_t c = m;
        cairo_matrix_translate(&c, cx, cy);
        cairo_matrix_scale(&c, rx, ry);
        cairo_set_matrix(cr, &c);
        cairo_arc(cr, 0, 0, 1, a0, a1);
        cairo_set_matrix(cr, &m);
    };
    corner(x + w - rx, y + ry, -M_PI / 2, 0);
    corner(x + w - rx, y + h - ry, 0, M_PI / 2);
    corner(x + rx, y + h - ry, M_PI / 2, M_PI);
    corner(x + rx, y + ry, M_PI, 3 * M_PI / 2);
    cairo_restore(cr);
    cairo_close_path(cr);
}

void EllipsePath(cairo_t* cr, const D2D1_ELLIPSE& e) {
    if (e.radiusX <= 0 || e.radiusY <= 0) return;
    cairo_matrix_t m, c;
    cairo_get_matrix(cr, &m);
    c = m;
    cairo_matrix_translate(&c, e.point.x, e.point.y);
    cairo_matrix_scale(&c, e.radiusX, e.radiusY);
    cairo_new_sub_path(cr);
    cairo_set_matrix(cr, &c);
    cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
    cairo_set_matrix(cr, &m);
    cairo_close_path(cr);
}

// An SVG / D2D endpoint arc from `from` as cubic Béziers (the standard
// endpoint-to-centre conversion, then quarter-turn pieces).
void ArcToBeziers(D2D1_POINT_2F from, const D2D1_ARC_SEGMENT& a, std::vector<D2D1_POINT_2F>* out) {
    double rx = std::fabs(a.size.width), ry = std::fabs(a.size.height);
    const double x1 = from.x, y1 = from.y, x2 = a.point.x, y2 = a.point.y;
    if (rx < 1e-6 || ry < 1e-6 || (x1 == x2 && y1 == y2)) {
        out->push_back(from);
        out->push_back(a.point);
        out->push_back(a.point);
        return;
    }
    const double phi = a.rotationAngle * M_PI / 180.0, cp = std::cos(phi), sp = std::sin(phi);
    const double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    const double x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
    const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1) { rx *= std::sqrt(lambda); ry *= std::sqrt(lambda); }
    // Direct2D's CLOCKWISE is SVG's sweep-flag 1 (y points down in both).
    const bool large = a.arcSize == D2D1_ARC_SIZE_LARGE, sweep = a.sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE;
    double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    const double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    double k = den > 0 ? std::sqrt(std::max(0.0, num / den)) : 0;
    if (large == sweep) k = -k;
    const double cxp = k * rx * y1p / ry, cyp = -k * ry * x1p / rx;
    const double cx = cp * cxp - sp * cyp + (x1 + x2) / 2, cy = sp * cxp + cp * cyp + (y1 + y2) / 2;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        const double d = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        return d;
    };
    const double t1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    double dt = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dt > 0) dt -= 2 * M_PI;
    else if (sweep && dt < 0) dt += 2 * M_PI;
    const int segs = std::max(1, static_cast<int>(std::ceil(std::fabs(dt) / (M_PI / 2) - 1e-9)));
    const double step = dt / segs, kappa = 4.0 / 3.0 * std::tan(step / 4);
    const auto pt = [&](double t, double ex, double ey) {
        const double x = rx * ex, y = ry * ey;
        return D2D1_POINT_2F{static_cast<float>(cp * x - sp * y + cx), static_cast<float>(sp * x + cp * y + cy)};
    };
    double t = t1;
    for (int i = 0; i < segs; ++i) {
        const double ta = t, tb = t + step;
        const double ca = std::cos(ta), sa = std::sin(ta), cb = std::cos(tb), sb = std::sin(tb);
        out->push_back(pt(0, ca - kappa * sa, sa + kappa * ca));
        out->push_back(pt(0, cb + kappa * sb, sb - kappa * cb));
        out->push_back(i == segs - 1 ? a.point : pt(0, cb, sb));
        t = tb;
    }
}

ID2D1Factory* TheFactory() {
    static ID2D1Factory* f = new ID2D1Factory;  // lives for the process
    return f;
}

}  // namespace

// ── Resources ───────────────────────────────────────────────────────────────

ID2D1Bitmap::~ID2D1Bitmap() {
    if (surface) cairo_surface_destroy(static_cast<cairo_surface_t*>(surface));
}
D2D1_SIZE_F ID2D1Bitmap::GetSize() const { return {static_cast<float>(width), static_cast<float>(height)}; }

ID2D1BitmapBrush::~ID2D1BitmapBrush() {
    if (bitmap) bitmap->Release();
}

ID2D1PathGeometry::~ID2D1PathGeometry() { free(ops); }

void ID2D1PathGeometry::Push(const Op& op) {
    if (count == capacity) {
        capacity = capacity ? capacity * 2 : 16;
        ops = static_cast<Op*>(realloc(ops, capacity * sizeof(Op)));
    }
    ops[count++] = op;
}

HRESULT ID2D1PathGeometry::Open(ID2D1GeometrySink** sink) {
    auto* s = new ID2D1GeometrySink;
    s->target = this;
    AddRef();  // the sink keeps its geometry alive
    *sink = s;
    return S_OK;
}

void ID2D1PathGeometry::Trace(void* crv, bool filledOnly) const {
    auto* cr = static_cast<cairo_t*>(crv);
    bool skip = false;
    for (size_t i = 0; i < count; ++i) {
        const Op& o = ops[i];
        if (o.kind == Op::Move) {
            skip = filledOnly && !o.filled;
            if (!skip) cairo_move_to(cr, o.p[0].x, o.p[0].y);
            continue;
        }
        if (skip) continue;
        switch (o.kind) {
            case Op::Line: cairo_line_to(cr, o.p[0].x, o.p[0].y); break;
            case Op::Curve: cairo_curve_to(cr, o.p[0].x, o.p[0].y, o.p[1].x, o.p[1].y, o.p[2].x, o.p[2].y); break;
            case Op::Close: cairo_close_path(cr); break;
            default: break;
        }
    }
}

void ID2D1PathGeometry::Clip(void* crv) const {
    if (!clipped) return;
    auto* cr = static_cast<cairo_t*>(crv);
    cairo_rectangle(cr, clipRect.left, clipRect.top, clipRect.right - clipRect.left, clipRect.bottom - clipRect.top);
    cairo_clip(cr);
}

void ID2D1RectangleGeometry::Trace(void* crv, bool) const {
    cairo_rectangle(static_cast<cairo_t*>(crv), rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

// Only the combine the mascot uses: a path intersected with a rectangle. The
// result keeps the path and remembers the rectangle as a clip.
HRESULT ID2D1Geometry::CombineWithGeometry(ID2D1Geometry* other, D2D1_COMBINE_MODE mode, const D2D1_MATRIX_3X2_F*,
                                           ID2D1SimplifiedGeometrySink* sinkBase) const {
    auto* sink = static_cast<ID2D1GeometrySink*>(sinkBase);
    auto* self = dynamic_cast<const ID2D1PathGeometry*>(this);
    auto* rect = dynamic_cast<ID2D1RectangleGeometry*>(other);
    if (!sink || !sink->target || !self || !rect || mode != D2D1_COMBINE_MODE_INTERSECT) return E_INVALIDARG;
    for (size_t i = 0; i < self->count; ++i) sink->target->Push(self->ops[i]);
    sink->target->fillMode = fillMode;
    sink->target->clipRect = rect->rect;
    sink->target->clipped = true;
    return S_OK;
}

void ID2D1GeometrySink::BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin) {
    target->figureFilled = begin == D2D1_FIGURE_BEGIN_FILLED;
    target->Push({ID2D1PathGeometry::Op::Move, {start}, target->figureFilled});
    target->cursor = start;
}
void ID2D1GeometrySink::AddLine(D2D1_POINT_2F p) {
    target->Push({ID2D1PathGeometry::Op::Line, {p}, target->figureFilled});
    target->cursor = p;
}
void ID2D1GeometrySink::AddBezier(const D2D1_BEZIER_SEGMENT& b) {
    target->Push({ID2D1PathGeometry::Op::Curve, {b.point1, b.point2, b.point3}, target->figureFilled});
    target->cursor = b.point3;
}
void ID2D1GeometrySink::AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT& q) {
    const D2D1_POINT_2F p0 = target->cursor;
    const D2D1_POINT_2F c1{p0.x + 2.0f / 3.0f * (q.point1.x - p0.x), p0.y + 2.0f / 3.0f * (q.point1.y - p0.y)};
    const D2D1_POINT_2F c2{q.point2.x + 2.0f / 3.0f * (q.point1.x - q.point2.x),
                           q.point2.y + 2.0f / 3.0f * (q.point1.y - q.point2.y)};
    AddBezier({c1, c2, q.point2});
}
void ID2D1GeometrySink::AddArc(const D2D1_ARC_SEGMENT& a) {
    std::vector<D2D1_POINT_2F> pts;
    ArcToBeziers(target->cursor, a, &pts);
    for (size_t i = 0; i + 2 < pts.size(); i += 3) AddBezier({pts[i], pts[i + 1], pts[i + 2]});
}
void ID2D1GeometrySink::EndFigure(D2D1_FIGURE_END end) {
    if (end == D2D1_FIGURE_END_CLOSED) target->Push({ID2D1PathGeometry::Op::Close, {}, target->figureFilled});
}
void ID2D1GeometrySink::SetFillMode(D2D1_FILL_MODE mode) { target->fillMode = mode; }

// ── Factory ─────────────────────────────────────────────────────────────────

HRESULT ID2D1Factory::CreatePathGeometry(ID2D1PathGeometry** out) {
    *out = new ID2D1PathGeometry;
    return S_OK;
}
HRESULT ID2D1Factory::CreateRectangleGeometry(const D2D1_RECT_F& r, ID2D1RectangleGeometry** out) {
    auto* g = new ID2D1RectangleGeometry;
    g->rect = r;
    *out = g;
    return S_OK;
}
HRESULT ID2D1Factory::CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& p, const float*, UINT32,
                                        ID2D1StrokeStyle** out) {
    auto* s = new ID2D1StrokeStyle;
    s->props = p;
    *out = s;
    return S_OK;
}

// ── Render target ───────────────────────────────────────────────────────────

ID2D1RenderTarget::~ID2D1RenderTarget() = default;

void ID2D1RenderTarget::GetFactory(ID2D1Factory** f) const {
    *f = TheFactory();
    (*f)->AddRef();
}

HRESULT ID2D1RenderTarget::CreateSolidColorBrush(const D2D1_COLOR_F& c, ID2D1SolidColorBrush** out) {
    auto* b = new ID2D1SolidColorBrush;
    b->color = c;
    *out = b;
    return S_OK;
}

HRESULT ID2D1RenderTarget::CreateGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 n,
                                                        ID2D1GradientStopCollection** out) {
    auto* c = new ID2D1GradientStopCollection;
    c->count = std::min<UINT32>(n, 8);
    std::copy(stops, stops + c->count, c->stops);
    *out = c;
    return S_OK;
}

HRESULT ID2D1RenderTarget::CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& p,
                                                     ID2D1GradientStopCollection* stops, ID2D1RadialGradientBrush** out) {
    auto* b = new ID2D1RadialGradientBrush;
    b->props = p;
    if (stops) {
        b->count = stops->count;
        std::copy(stops->stops, stops->stops + stops->count, b->stops);
    }
    *out = b;
    return S_OK;
}

HRESULT ID2D1RenderTarget::CreateBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES&,
                                        ID2D1Bitmap** out) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size.width, size.height);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        *out = nullptr;
        return E_OUTOFMEMORY;
    }
    // DXGI_FORMAT_B8G8R8A8_UNORM premultiplied is cairo's ARGB32 on little-endian machines.
    cairo_surface_flush(s);
    unsigned char* dst = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    if (data)
        for (UINT32 y = 0; y < size.height; ++y)
            memcpy(dst + static_cast<size_t>(y) * stride, static_cast<const unsigned char*>(data) + static_cast<size_t>(y) * pitch,
                   size.width * 4);
    cairo_surface_mark_dirty(s);
    auto* b = new ID2D1Bitmap;
    b->surface = s;
    b->width = size.width;
    b->height = size.height;
    *out = b;
    return S_OK;
}

HRESULT ID2D1RenderTarget::CreateBitmapBrush(ID2D1Bitmap* bitmap, ID2D1BitmapBrush** out) {
    if (!bitmap) { *out = nullptr; return E_INVALIDARG; }
    auto* b = new ID2D1BitmapBrush;
    b->bitmap = bitmap;
    bitmap->AddRef();
    *out = b;
    return S_OK;
}

HRESULT ID2D1RenderTarget::CreateLayer(ID2D1Layer** out) {
    *out = new ID2D1Layer;
    return S_OK;
}

void ID2D1RenderTarget::SetTransform(const D2D1_MATRIX_3X2_F& m) { transform = m; }

void ID2D1RenderTarget::Clear(const D2D1_COLOR_F& c) {
    cairo_t* cr = Cr(this);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
    cairo_paint(cr);
    cairo_restore(cr);
}

void ID2D1RenderTarget::DrawLine(D2D1_POINT_2F a, D2D1_POINT_2F b, ID2D1Brush* brush, float width, ID2D1StrokeStyle* style) {
    cairo_t* cr = Cr(this);
    ApplyTransform(this);
    cairo_new_path(cr);
    cairo_move_to(cr, a.x, a.y);
    cairo_line_to(cr, b.x, b.y);
    SetStroke(cr, width, style);
    SetSource(cr, brush);
    cairo_stroke(cr);
}

void ID2D1RenderTarget::DrawRectangle(const D2D1_RECT_F& r, ID2D1Brush* brush, float width, ID2D1StrokeStyle* style) {
    cairo_t* cr = Cr(this);
    ApplyTransform(this);
    cairo_new_path(cr);
    cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
    SetStroke(cr, width, style);
    SetSource(cr, brush);
    cairo_stroke(cr);
}

void ID2D1RenderTarget::FillRectangle(const D2D1_RECT_F& r, ID2D1Brush* brush) {
    cairo_t* cr = Cr(this);
    ApplyTransform(this);
    cairo_new_path(cr);
    cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
    SetSource(cr, brush);
    cairo_fill(cr);
}

void ID2D1RenderTarget::DrawRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* brush, float width,
                                             ID2D1StrokeStyle* style) {
    cairo_t* cr = Cr(this);
    ApplyTransform(this);
    cairo_new_path(cr);
    RoundedRectPath(cr, r);
    SetStroke(cr, width, style);
    SetSource(cr, brush);
    cairo_stroke(cr);
}

void ID2D1RenderTarget::FillRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* brush) {
    cairo_t* cr = Cr(this);
    ApplyTransform(this);
    cairo_new_path(cr);
    RoundedRectPath(cr, r);
    SetSource(cr, brush);
    cairo_fill(cr);
}

void ID2D1RenderTarget::DrawEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* brush, float width, ID2D1StrokeStyle* style) {
    cairo_t* cr = Cr(this);
    ApplyTransform(this);
    cairo_new_path(cr);
    EllipsePath(cr, e);
    SetStroke(cr, width, style);
    SetSource(cr, brush);
    cairo_stroke(cr);
}

void ID2D1RenderTarget::FillEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* brush) {
    cairo_t* cr = Cr(this);
    ApplyTransform(this);
    cairo_new_path(cr);
    EllipsePath(cr, e);
    SetSource(cr, brush);
    cairo_fill(cr);
}

void ID2D1RenderTarget::DrawGeometry(ID2D1Geometry* g, ID2D1Brush* brush, float width, ID2D1StrokeStyle* style) {
    if (!g) return;
    cairo_t* cr = Cr(this);
    cairo_save(cr);
    ApplyTransform(this);
    g->Clip(cr);
    cairo_new_path(cr);
    g->Trace(cr, false);
    SetStroke(cr, width, style);
    SetSource(cr, brush);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void ID2D1RenderTarget::FillGeometry(ID2D1Geometry* g, ID2D1Brush* brush, ID2D1Brush*) {
    if (!g) return;
    cairo_t* cr = Cr(this);
    cairo_save(cr);
    ApplyTransform(this);
    g->Clip(cr);
    cairo_new_path(cr);
    g->Trace(cr, true);
    cairo_set_fill_rule(cr, g->fillMode == D2D1_FILL_MODE_WINDING ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
    SetSource(cr, brush);
    cairo_fill(cr);
    cairo_restore(cr);
}

void ID2D1RenderTarget::DrawBitmap(ID2D1Bitmap* bmp, const D2D1_RECT_F& dst, float opacity,
                                   D2D1_BITMAP_INTERPOLATION_MODE mode, const D2D1_RECT_F& srcIn) {
    if (!bmp || !bmp->surface) return;
    const D2D1_RECT_F src = srcIn.right < srcIn.left ? D2D1_RECT_F{0, 0, float(bmp->width), float(bmp->height)} : srcIn;
    const float sw = src.right - src.left, sh = src.bottom - src.top;
    if (sw <= 0 || sh <= 0 || dst.right <= dst.left || dst.bottom <= dst.top) return;
    cairo_t* cr = Cr(this);
    cairo_save(cr);
    ApplyTransform(this);
    cairo_new_path(cr);
    cairo_rectangle(cr, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top);
    cairo_clip(cr);
    cairo_translate(cr, dst.left, dst.top);
    cairo_scale(cr, (dst.right - dst.left) / sw, (dst.bottom - dst.top) / sh);
    cairo_translate(cr, -src.left, -src.top);
    cairo_set_source_surface(cr, static_cast<cairo_surface_t*>(bmp->surface), 0, 0);
    cairo_pattern_t* pat = cairo_get_source(cr);
    // GOOD filters with box sampling when shrinking, close to D2D's linear + mips.
    cairo_pattern_set_filter(pat, mode == D2D1_BITMAP_INTERPOLATION_MODE_LINEAR ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_PAD);
    cairo_paint_with_alpha(cr, opacity);
    cairo_restore(cr);
}

void ID2D1RenderTarget::DrawBitmap(ID2D1Bitmap* bmp, const D2D1_RECT_F* dst, float opacity,
                                   D2D1_BITMAP_INTERPOLATION_MODE mode, const D2D1_RECT_F* src) {
    if (!bmp) return;
    const D2D1_RECT_F full{0, 0, float(bmp->width), float(bmp->height)};
    DrawBitmap(bmp, dst ? *dst : full, opacity, mode, src ? *src : full);
}

void ID2D1RenderTarget::PushLayer(const D2D1_LAYER_PARAMETERS& p, ID2D1Layer*) {
    cairo_t* cr = Cr(this);
    LayerState st{p.opacity, p.opacityBrush, transform, false};
    cairo_save(cr);
    if (p.geometricMask) {
        ApplyWorld(this, p.maskTransform * transform);
        p.geometricMask->Clip(cr);
        cairo_new_path(cr);
        p.geometricMask->Trace(cr, true);
        cairo_set_fill_rule(cr, p.geometricMask->fillMode == D2D1_FILL_MODE_WINDING ? CAIRO_FILL_RULE_WINDING
                                                                                    : CAIRO_FILL_RULE_EVEN_ODD);
        cairo_clip(cr);
    }
    if (p.opacity < 1.0f || p.opacityBrush) {
        cairo_push_group(cr);
        st.group = true;
        if (p.opacityBrush) p.opacityBrush->AddRef();
    }
    if (layerDepth < 16) layers[layerDepth] = st;
    ++layerDepth;
}

void ID2D1RenderTarget::PopLayer() {
    if (layerDepth <= 0) return;
    --layerDepth;
    if (layerDepth >= 16) return;
    cairo_t* cr = Cr(this);
    const LayerState st = layers[layerDepth];
    if (st.group) {
        auto* mask = dynamic_cast<ID2D1BitmapBrush*>(st.mask);
        if (mask && mask->bitmap) {
            // Work in device space: the group surface already is, and the mask
            // (a bitmap brush in the world space of PushLayer's time) is mapped
            // device -> world -> brush.
            cairo_pattern_t* group = cairo_pop_group(cr);
            cairo_matrix_t id, toWorld = ToCairo(st.world * base), toBrush = ToCairo(mask->transform), m;
            cairo_matrix_init_identity(&id);
            cairo_identity_matrix(cr);
            cairo_pattern_set_matrix(group, &id);
            cairo_matrix_invert(&toWorld);
            cairo_matrix_invert(&toBrush);
            cairo_matrix_multiply(&m, &toWorld, &toBrush);
            cairo_pattern_t* maskPat = cairo_pattern_create_for_surface(static_cast<cairo_surface_t*>(mask->bitmap->surface));
            cairo_pattern_set_matrix(maskPat, &m);
            cairo_pattern_set_filter(maskPat, CAIRO_FILTER_GOOD);
            cairo_set_source(cr, group);
            cairo_mask(cr, maskPat);  // the layer's opacity is 1 wherever a mask is used
            cairo_pattern_destroy(maskPat);
            cairo_pattern_destroy(group);
        } else {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, st.opacity);
        }
        if (st.mask) st.mask->Release();
    }
    cairo_restore(cr);
}

void ID2D1RenderTarget::PushAxisAlignedClip(const D2D1_RECT_F& r, D2D1_ANTIALIAS_MODE) {
    cairo_t* cr = Cr(this);
    cairo_save(cr);
    ApplyTransform(this);
    cairo_new_path(cr);
    cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
    cairo_clip(cr);
}

void ID2D1RenderTarget::PopAxisAlignedClip() { cairo_restore(Cr(this)); }

namespace bbcompat {

ID2D1RenderTarget* CreateCairoTarget() { return new ID2D1RenderTarget; }

void SetCairo(ID2D1RenderTarget* rt, void* crv) {
    rt->cr = crv;
    rt->transform = D2D1::Matrix3x2F::Identity();
    rt->layerDepth = 0;
    if (auto* cr = static_cast<cairo_t*>(crv)) {
        cairo_matrix_t m;
        cairo_get_matrix(cr, &m);
        rt->base = {static_cast<float>(m.xx), static_cast<float>(m.yx), static_cast<float>(m.xy),
                    static_cast<float>(m.yy), static_cast<float>(m.x0), static_cast<float>(m.y0)};
    }
}

}  // namespace bbcompat
