// Bunbun — one mascot frame as PNG bytes (see mascot_png.h).
#include "mascot_png.h"

#include <cairo.h>

namespace bb::plat {

std::string RenderMascotPng(const mascot::Style& style, mascot::Mood mood, int px, double t, double since,
                            D2D1_POINT_2F look) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    cairo_t* cr = cairo_create(surface);
    ID2D1RenderTarget* rt = bbcompat::CreateCairoTarget();
    bbcompat::SetCairo(rt, cr);
    const float s = static_cast<float>(px);
    mascot::DrawStyled(rt, style, D2D1::Point2F(s * 0.5f, s * 0.42f), s * 0.92f, mood, t, since, look);
    mascot::ReleaseThreadResources();  // rt is short-lived
    rt->Release();
    cairo_destroy(cr);
    std::string png;
    cairo_surface_write_to_png_stream(
        surface,
        [](void* closure, const unsigned char* data, unsigned int len) {
            static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), len);
            return CAIRO_STATUS_SUCCESS;
        },
        &png);
    cairo_surface_destroy(surface);
    return png;
}

std::string Base64(const std::string& in) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const auto b = [&](size_t k) { return k < in.size() ? static_cast<unsigned char>(in[k]) : 0u; };
        const unsigned v = (b(i) << 16) | (b(i + 1) << 8) | b(i + 2);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? kB64[v & 63] : '=';
    }
    return out;
}

}  // namespace bb::plat
