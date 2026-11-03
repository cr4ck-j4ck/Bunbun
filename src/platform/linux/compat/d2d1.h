// Bunbun — the Direct2D subset the mascot draws with, implemented on Cairo.
//
// The mascot (src/mascot/mascot.cpp) is about 3,000 lines of Direct2D calls.
// Rather than fork it, the Linux build compiles it unchanged against these
// stand-ins: the same names and signatures, backed by a cairo_t
// (d2d_cairo.cpp). Only what the mascot uses exists here; a call outside that
// subset is a compile error on Linux, not a silent difference.
//
// Linux code makes a render target for a cairo context with
// bbcompat::CreateCairoTarget(), and points it at each frame's context with
// bbcompat::SetCairo().
#pragma once

#include "windows.h"

#include <cmath>

using FLOAT = float;

struct D2D1_POINT_2F { float x, y; };
struct D2D1_POINT_2U { UINT32 x, y; };
struct D2D1_SIZE_F { float width, height; };
struct D2D1_SIZE_U { UINT32 width, height; };
struct D2D1_RECT_F { float left, top, right, bottom; };
struct D2D1_RECT_U { UINT32 left, top, right, bottom; };
struct D2D1_COLOR_F { float r, g, b, a; };
struct D2D1_ELLIPSE { D2D1_POINT_2F point; float radiusX, radiusY; };
struct D2D1_ROUNDED_RECT { D2D1_RECT_F rect; float radiusX, radiusY; };
struct D2D1_MATRIX_3X2_F { float _11, _12, _21, _22, _31, _32; };
struct D2D1_MATRIX_4X4_F { float m[4][4]; };
struct D2D1_BEZIER_SEGMENT { D2D1_POINT_2F point1, point2, point3; };
struct D2D1_QUADRATIC_BEZIER_SEGMENT { D2D1_POINT_2F point1, point2; };
struct D2D1_GRADIENT_STOP { float position; D2D1_COLOR_F color; };

enum D2D1_FIGURE_BEGIN { D2D1_FIGURE_BEGIN_FILLED = 0, D2D1_FIGURE_BEGIN_HOLLOW = 1 };
enum D2D1_FIGURE_END { D2D1_FIGURE_END_OPEN = 0, D2D1_FIGURE_END_CLOSED = 1 };
enum D2D1_FILL_MODE { D2D1_FILL_MODE_ALTERNATE = 0, D2D1_FILL_MODE_WINDING = 1 };
enum D2D1_SWEEP_DIRECTION { D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE = 0, D2D1_SWEEP_DIRECTION_CLOCKWISE = 1 };
enum D2D1_ARC_SIZE { D2D1_ARC_SIZE_SMALL = 0, D2D1_ARC_SIZE_LARGE = 1 };
enum D2D1_CAP_STYLE { D2D1_CAP_STYLE_FLAT = 0, D2D1_CAP_STYLE_SQUARE = 1, D2D1_CAP_STYLE_ROUND = 2, D2D1_CAP_STYLE_TRIANGLE = 3 };
enum D2D1_LINE_JOIN { D2D1_LINE_JOIN_MITER = 0, D2D1_LINE_JOIN_BEVEL = 1, D2D1_LINE_JOIN_ROUND = 2 };
enum D2D1_DASH_STYLE { D2D1_DASH_STYLE_SOLID = 0 };
enum D2D1_COMBINE_MODE { D2D1_COMBINE_MODE_UNION = 0, D2D1_COMBINE_MODE_INTERSECT = 1 };
enum D2D1_ANTIALIAS_MODE { D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0, D2D1_ANTIALIAS_MODE_ALIASED = 1 };
enum D2D1_BITMAP_INTERPOLATION_MODE { D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR = 1 };
enum D2D1_INTERPOLATION_MODE { D2D1_INTERPOLATION_MODE_LINEAR = 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC = 5 };
enum D2D1_ALPHA_MODE { D2D1_ALPHA_MODE_UNKNOWN = 0, D2D1_ALPHA_MODE_PREMULTIPLIED = 1, D2D1_ALPHA_MODE_STRAIGHT = 2 };
enum D2D1_LAYER_OPTIONS { D2D1_LAYER_OPTIONS_NONE = 0 };
enum D2D1_EXTEND_MODE { D2D1_EXTEND_MODE_CLAMP = 0 };
enum D2D1_GAMMA { D2D1_GAMMA_2_2 = 0 };
enum DXGI_FORMAT { DXGI_FORMAT_UNKNOWN = 0, DXGI_FORMAT_B8G8R8A8_UNORM = 87 };

struct D2D1_PIXEL_FORMAT { DXGI_FORMAT format; D2D1_ALPHA_MODE alphaMode; };
struct D2D1_BITMAP_PROPERTIES { D2D1_PIXEL_FORMAT pixelFormat; float dpiX, dpiY; };
struct D2D1_ARC_SEGMENT {
    D2D1_POINT_2F point;
    D2D1_SIZE_F size;
    float rotationAngle;
    D2D1_SWEEP_DIRECTION sweepDirection;
    D2D1_ARC_SIZE arcSize;
};
struct D2D1_STROKE_STYLE_PROPERTIES {
    D2D1_CAP_STYLE startCap, endCap, dashCap;
    D2D1_LINE_JOIN lineJoin;
    float miterLimit;
    D2D1_DASH_STYLE dashStyle;
    float dashOffset;
};
struct D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES {
    D2D1_POINT_2F center, gradientOriginOffset;
    float radiusX, radiusY;
};

// ── Objects ─────────────────────────────────────────────────────────────────
// Reference counted like COM objects, so ComPtr works as on Windows.

struct IID { int id; };
template <class T> struct BbIidOf;

class IUnknown {
public:
    virtual ~IUnknown() = default;
    unsigned long AddRef() { return ++refs_; }
    unsigned long Release() {
        const unsigned long n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    // Only ID2D1DeviceContext is ever asked for, and there is none here: the
    // mascot then takes its plain DrawBitmap path.
    HRESULT QueryInterface(const IID&, void** out) { *out = nullptr; return E_NOINTERFACE; }

private:
    unsigned long refs_ = 1;
};
template <class P>
IID BbIidFor(const P&) { return IID{0}; }
template <class T>
void** BbPpv(T** pp) { return reinterpret_cast<void**>(pp); }
template <class R>
void** BbPpv(R ref) { return static_cast<void**>(ref); }  // a ComPtrRef
#define IID_PPV_ARGS(pp) BbIidFor(pp), BbPpv(pp)

class ID2D1Resource : public IUnknown {};
class ID2D1Factory;
class ID2D1StrokeStyle : public ID2D1Resource {
public:
    D2D1_STROKE_STYLE_PROPERTIES props{};
};
class ID2D1Layer : public ID2D1Resource {};
class ID2D1GradientStopCollection : public ID2D1Resource {
public:
    D2D1_GRADIENT_STOP stops[8]{};
    UINT32 count = 0;
};

class ID2D1Bitmap : public ID2D1Resource {
public:
    ~ID2D1Bitmap() override;
    D2D1_SIZE_F GetSize() const;
    void* surface = nullptr;  // cairo_surface_t*, premultiplied ARGB32 (BGRA bytes)
    UINT32 width = 0, height = 0;
};

class ID2D1Brush : public ID2D1Resource {
public:
    void SetOpacity(float o) { opacity = o; }
    void SetTransform(const D2D1_MATRIX_3X2_F& m) { transform = m; }
    float opacity = 1.0f;
    D2D1_MATRIX_3X2_F transform{1, 0, 0, 1, 0, 0};
};
class ID2D1SolidColorBrush : public ID2D1Brush {
public:
    void SetColor(const D2D1_COLOR_F& c) { color = c; }
    D2D1_COLOR_F GetColor() const { return color; }
    D2D1_COLOR_F color{};
};
class ID2D1RadialGradientBrush : public ID2D1Brush {
public:
    void SetCenter(D2D1_POINT_2F c) { props.center = c; }
    void SetRadiusX(float r) { props.radiusX = r; }
    void SetRadiusY(float r) { props.radiusY = r; }
    void SetGradientOriginOffset(D2D1_POINT_2F o) { props.gradientOriginOffset = o; }
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{};
    D2D1_GRADIENT_STOP stops[8]{};
    UINT32 count = 0;
};
class ID2D1BitmapBrush : public ID2D1Brush {
public:
    ~ID2D1BitmapBrush() override;
    void SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE m) { interpolation = m; }
    ID2D1Bitmap* bitmap = nullptr;  // holds a reference
    D2D1_BITMAP_INTERPOLATION_MODE interpolation = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
};

class ID2D1Geometry;
class ID2D1SimplifiedGeometrySink : public IUnknown {};
class ID2D1GeometrySink : public ID2D1SimplifiedGeometrySink {
public:
    void BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin);
    void AddLine(D2D1_POINT_2F p);
    void AddLines(const D2D1_POINT_2F* p, UINT32 n) { for (UINT32 i = 0; i < n; ++i) AddLine(p[i]); }
    void AddBezier(const D2D1_BEZIER_SEGMENT& b);
    void AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT& q);
    void AddArc(const D2D1_ARC_SEGMENT& a);
    void EndFigure(D2D1_FIGURE_END end);
    void SetFillMode(D2D1_FILL_MODE mode);
    HRESULT Close() { return S_OK; }
    class ID2D1PathGeometry* target = nullptr;  // not owned: the geometry outlives its sink
};

class ID2D1Geometry : public ID2D1Resource {
public:
    // Replays the outline into a cairo context (cairo_t*), in geometry units.
    // filledOnly skips figures begun with D2D1_FIGURE_BEGIN_HOLLOW.
    virtual void Trace(void* cr, bool filledOnly) const = 0;
    virtual void Clip(void* cr) const {}
    HRESULT CombineWithGeometry(ID2D1Geometry* other, D2D1_COMBINE_MODE mode, const D2D1_MATRIX_3X2_F* transform,
                                ID2D1SimplifiedGeometrySink* sink) const;
    D2D1_FILL_MODE fillMode = D2D1_FILL_MODE_ALTERNATE;
};
class ID2D1RectangleGeometry : public ID2D1Geometry {
public:
    void Trace(void* cr, bool filledOnly) const override;
    D2D1_RECT_F rect{};
};
class ID2D1PathGeometry : public ID2D1Geometry {
public:
    HRESULT Open(ID2D1GeometrySink** sink);
    void Trace(void* cr, bool filledOnly) const override;
    void Clip(void* cr) const override;
    struct Op {
        enum Kind { Move, Line, Curve, Close } kind;
        D2D1_POINT_2F p[3];
        bool filled;
    };
    Op* ops = nullptr;
    size_t count = 0, capacity = 0;
    D2D1_RECT_F clipRect{};  // the rectangle an INTERSECT combine kept
    bool clipped = false;
    bool figureFilled = true;
    D2D1_POINT_2F cursor{};
    void Push(const Op& op);
    ~ID2D1PathGeometry() override;
};

struct D2D1_LAYER_PARAMETERS {
    D2D1_RECT_F contentBounds;
    ID2D1Geometry* geometricMask;
    D2D1_ANTIALIAS_MODE maskAntialiasMode;
    D2D1_MATRIX_3X2_F maskTransform;
    float opacity;
    ID2D1Brush* opacityBrush;
    D2D1_LAYER_OPTIONS layerOptions;
};

class ID2D1RenderTarget : public ID2D1Resource {
public:
    ~ID2D1RenderTarget() override;
    void GetFactory(ID2D1Factory** f) const;
    HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& c, ID2D1SolidColorBrush** out);
    HRESULT CreateGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 n, ID2D1GradientStopCollection** out);
    HRESULT CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& p, ID2D1GradientStopCollection* stops,
                                      ID2D1RadialGradientBrush** out);
    HRESULT CreateBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES& props,
                         ID2D1Bitmap** out);
    HRESULT CreateBitmapBrush(ID2D1Bitmap* bitmap, ID2D1BitmapBrush** out);
    HRESULT CreateLayer(ID2D1Layer** out);
    HRESULT CreateLayer(const D2D1_SIZE_F*, ID2D1Layer** out) { return CreateLayer(out); }

    void SetTransform(const D2D1_MATRIX_3X2_F& m);
    void GetTransform(D2D1_MATRIX_3X2_F* m) const { *m = transform; }
    void GetDpi(float* x, float* y) const { *x = 96.0f; *y = 96.0f; }

    void Clear(const D2D1_COLOR_F& c);
    void DrawLine(D2D1_POINT_2F a, D2D1_POINT_2F b, ID2D1Brush* brush, float width = 1.0f,
                  ID2D1StrokeStyle* style = nullptr);
    void DrawRectangle(const D2D1_RECT_F& r, ID2D1Brush* brush, float width = 1.0f, ID2D1StrokeStyle* style = nullptr);
    void FillRectangle(const D2D1_RECT_F& r, ID2D1Brush* brush);
    void DrawRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* brush, float width = 1.0f,
                              ID2D1StrokeStyle* style = nullptr);
    void FillRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* brush);
    void DrawEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* brush, float width = 1.0f, ID2D1StrokeStyle* style = nullptr);
    void FillEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* brush);
    void DrawGeometry(ID2D1Geometry* g, ID2D1Brush* brush, float width = 1.0f, ID2D1StrokeStyle* style = nullptr);
    void FillGeometry(ID2D1Geometry* g, ID2D1Brush* brush, ID2D1Brush* opacityBrush = nullptr);
    void DrawBitmap(ID2D1Bitmap* bmp, const D2D1_RECT_F& dst, float opacity = 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE mode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    const D2D1_RECT_F& src = D2D1_RECT_F{0, 0, -1, -1});
    void DrawBitmap(ID2D1Bitmap* bmp, const D2D1_RECT_F* dst, float opacity, D2D1_BITMAP_INTERPOLATION_MODE mode,
                    const D2D1_RECT_F* src);
    void PushLayer(const D2D1_LAYER_PARAMETERS& p, ID2D1Layer* layer);
    void PopLayer();
    void PushAxisAlignedClip(const D2D1_RECT_F& r, D2D1_ANTIALIAS_MODE mode);
    void PopAxisAlignedClip();

    void* cr = nullptr;  // cairo_t*, set per frame
    D2D1_MATRIX_3X2_F transform{1, 0, 0, 1, 0, 0};
    D2D1_MATRIX_3X2_F base{1, 0, 0, 1, 0, 0};  // the context's own transform when it was attached
    struct LayerState { float opacity; ID2D1Brush* mask; D2D1_MATRIX_3X2_F world; bool group; };
    LayerState layers[16]{};
    int layerDepth = 0;
};

// Never handed out (see IUnknown::QueryInterface); exists so the mascot compiles.
class ID2D1DeviceContext : public ID2D1RenderTarget {
public:
    void DrawBitmap(ID2D1Bitmap* bmp, const D2D1_RECT_F* dst, float opacity, D2D1_INTERPOLATION_MODE mode,
                    const D2D1_RECT_F* src, const D2D1_MATRIX_4X4_F* perspective) {
        ID2D1RenderTarget::DrawBitmap(bmp, dst, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
    }
};

class ID2D1Factory : public IUnknown {
public:
    HRESULT CreatePathGeometry(ID2D1PathGeometry** out);
    HRESULT CreateRectangleGeometry(const D2D1_RECT_F& r, ID2D1RectangleGeometry** out);
    HRESULT CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& p, const float* dashes, UINT32 n,
                              ID2D1StrokeStyle** out);
};

// ── Helpers (d2d1helper.h) ──────────────────────────────────────────────────

namespace D2D1 {

inline D2D1_POINT_2F Point2F(float x = 0, float y = 0) { return {x, y}; }
inline D2D1_SIZE_F SizeF(float w = 0, float h = 0) { return {w, h}; }
inline D2D1_SIZE_U SizeU(UINT32 w = 0, UINT32 h = 0) { return {w, h}; }
inline D2D1_RECT_F RectF(float l = 0, float t = 0, float r = 0, float b = 0) { return {l, t, r, b}; }
inline D2D1_RECT_F InfiniteRect() { return {-1e30f, -1e30f, 1e30f, 1e30f}; }
inline D2D1_ELLIPSE Ellipse(D2D1_POINT_2F c, float rx, float ry) { return {c, rx, ry}; }
inline D2D1_ROUNDED_RECT RoundedRect(const D2D1_RECT_F& r, float rx, float ry) { return {r, rx, ry}; }
inline D2D1_BEZIER_SEGMENT BezierSegment(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c) { return {a, b, c}; }
inline D2D1_QUADRATIC_BEZIER_SEGMENT QuadraticBezierSegment(D2D1_POINT_2F a, D2D1_POINT_2F b) { return {a, b}; }
inline D2D1_ARC_SEGMENT ArcSegment(D2D1_POINT_2F p, D2D1_SIZE_F s, float rot, D2D1_SWEEP_DIRECTION dir,
                                   D2D1_ARC_SIZE size) {
    return {p, s, rot, dir, size};
}
inline D2D1_PIXEL_FORMAT PixelFormat(DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE a = D2D1_ALPHA_MODE_UNKNOWN) {
    return {f, a};
}
inline D2D1_BITMAP_PROPERTIES BitmapProperties(const D2D1_PIXEL_FORMAT& f = PixelFormat(), float dx = 96, float dy = 96) {
    return {f, dx, dy};
}
inline D2D1_STROKE_STYLE_PROPERTIES StrokeStyleProperties(D2D1_CAP_STYLE start = D2D1_CAP_STYLE_FLAT,
                                                          D2D1_CAP_STYLE end = D2D1_CAP_STYLE_FLAT,
                                                          D2D1_CAP_STYLE dash = D2D1_CAP_STYLE_FLAT,
                                                          D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER,
                                                          float miter = 10.0f,
                                                          D2D1_DASH_STYLE style = D2D1_DASH_STYLE_SOLID,
                                                          float offset = 0.0f) {
    return {start, end, dash, join, miter, style, offset};
}
inline D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES RadialGradientBrushProperties(D2D1_POINT_2F c, D2D1_POINT_2F off, float rx,
                                                                           float ry) {
    return {c, off, rx, ry};
}

class ColorF : public D2D1_COLOR_F {
public:
    ColorF(UINT32 rgb, float a = 1.0f)
        : D2D1_COLOR_F{((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a} {}
    ColorF(float r, float g, float b, float a = 1.0f) : D2D1_COLOR_F{r, g, b, a} {}
};

class Matrix3x2F : public D2D1_MATRIX_3X2_F {
public:
    Matrix3x2F() : D2D1_MATRIX_3X2_F{1, 0, 0, 1, 0, 0} {}
    Matrix3x2F(float a, float b, float c, float d, float e, float f) : D2D1_MATRIX_3X2_F{a, b, c, d, e, f} {}
    Matrix3x2F(const D2D1_MATRIX_3X2_F& m) : D2D1_MATRIX_3X2_F(m) {}
    static Matrix3x2F Identity() { return {}; }
    static Matrix3x2F Translation(float x, float y) { return {1, 0, 0, 1, x, y}; }
    static Matrix3x2F Translation(D2D1_SIZE_F s) { return Translation(s.width, s.height); }
    static Matrix3x2F Scale(float x, float y, D2D1_POINT_2F c = {0, 0}) {
        return {x, 0, 0, y, c.x - x * c.x, c.y - y * c.y};
    }
    static Matrix3x2F Scale(D2D1_SIZE_F s, D2D1_POINT_2F c = {0, 0}) { return Scale(s.width, s.height, c); }
    // Degrees, clockwise on screen (y points down), as in Direct2D.
    static Matrix3x2F Rotation(float degrees, D2D1_POINT_2F c = {0, 0}) {
        const float r = degrees * 3.14159265358979f / 180.0f, cs = std::cos(r), sn = std::sin(r);
        return {cs, sn, -sn, cs, c.x - cs * c.x + sn * c.y, c.y - sn * c.x - cs * c.y};
    }
    static Matrix3x2F Skew(float ax, float ay, D2D1_POINT_2F c = {0, 0}) {
        const float tx = std::tan(ax * 3.14159265358979f / 180.0f), ty = std::tan(ay * 3.14159265358979f / 180.0f);
        return {1, ty, tx, 1, -c.y * tx, -c.x * ty};
    }
    D2D1_POINT_2F TransformPoint(D2D1_POINT_2F p) const { return {p.x * _11 + p.y * _21 + _31, p.x * _12 + p.y * _22 + _32}; }
    bool IsIdentity() const { return _11 == 1 && _12 == 0 && _21 == 0 && _22 == 1 && _31 == 0 && _32 == 0; }
};
inline D2D1_MATRIX_3X2_F IdentityMatrix() { return Matrix3x2F::Identity(); }

}  // namespace D2D1

// a * b: a first, then b (row vectors, as in Direct2D).
inline D2D1::Matrix3x2F operator*(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {
    return {a._11 * b._11 + a._12 * b._21,         a._11 * b._12 + a._12 * b._22,
            a._21 * b._11 + a._22 * b._21,         a._21 * b._12 + a._22 * b._22,
            a._31 * b._11 + a._32 * b._21 + b._31, a._31 * b._12 + a._32 * b._22 + b._32};
}

namespace D2D1 {

inline D2D1_LAYER_PARAMETERS LayerParameters(const D2D1_RECT_F& bounds = InfiniteRect(), ID2D1Geometry* mask = nullptr,
                                             D2D1_ANTIALIAS_MODE aa = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                             D2D1_MATRIX_3X2_F maskTransform = IdentityMatrix(), float opacity = 1.0f,
                                             ID2D1Brush* opacityBrush = nullptr,
                                             D2D1_LAYER_OPTIONS options = D2D1_LAYER_OPTIONS_NONE) {
    return {bounds, mask, aa, maskTransform, opacity, opacityBrush, options};
}

}  // namespace D2D1

namespace bbcompat {
// A render target that draws into cairo contexts. The caller owns the result
// (Release it, or keep it in a ComPtr).
ID2D1RenderTarget* CreateCairoTarget();
// Points the target at this frame's context (a cairo_t*). The context's current
// transform becomes the target's identity, like a D2D target's own pixel space.
void SetCairo(ID2D1RenderTarget* rt, void* cr);
}  // namespace bbcompat
