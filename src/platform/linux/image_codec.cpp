// Bunbun — image files for skin packs on Linux (pack_sheet.h's codec half).
// Decoding goes through GdkPixbuf (PNG, JPEG, WebP… whatever loaders the
// system has); encoding writes PNG with GdkPixbuf too.
#include "../../mascot/pack_sheet.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "platform.h"

namespace bb::mascot {
namespace {

bool FromPixbuf(GdkPixbuf* pb, Image* out) {
    if (!pb) return false;
    GdkPixbuf* rgba = gdk_pixbuf_get_has_alpha(pb) ? GDK_PIXBUF(g_object_ref(pb)) : gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
    const int w = gdk_pixbuf_get_width(rgba), h = gdk_pixbuf_get_height(rgba);
    bool ok = false;
    if (w > 0 && h > 0 && w <= 8192 && h <= 8192 && gdk_pixbuf_get_n_channels(rgba) == 4 &&
        gdk_pixbuf_get_bits_per_sample(rgba) == 8) {
        out->w = w;
        out->h = h;
        out->px.resize(static_cast<size_t>(w) * h * 4);
        const guchar* src = gdk_pixbuf_read_pixels(rgba);
        const int stride = gdk_pixbuf_get_rowstride(rgba);
        for (int y = 0; y < h; ++y)
            std::copy(src + static_cast<size_t>(y) * stride, src + static_cast<size_t>(y) * stride + w * 4,
                      out->px.begin() + static_cast<size_t>(y) * w * 4);
        ok = true;
    }
    g_object_unref(rgba);
    return ok;
}

GdkPixbuf* ToPixbuf(const Image& img) {
    auto* copy = new std::vector<uint8_t>(img.px);
    return gdk_pixbuf_new_from_data(copy->data(), GDK_COLORSPACE_RGB, TRUE, 8, img.w, img.h, img.w * 4,
                                    [](guchar*, gpointer p) { delete static_cast<std::vector<uint8_t>*>(p); }, copy);
}

}  // namespace

bool DecodeImage(const void* data, size_t size, Image* out) {
    if (size == 0 || size > 64u * 1024 * 1024) return false;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    bool ok = gdk_pixbuf_loader_write(loader, static_cast<const guchar*>(data), size, nullptr);
    ok = gdk_pixbuf_loader_close(loader, nullptr) && ok;
    ok = ok && FromPixbuf(gdk_pixbuf_loader_get_pixbuf(loader), out);
    g_object_unref(loader);
    return ok;
}

bool DecodeImageFile(const std::wstring& path, Image* out) {
    GdkPixbuf* pb = gdk_pixbuf_new_from_file(plat::Native(path).c_str(), nullptr);
    const bool ok = FromPixbuf(pb, out);
    if (pb) g_object_unref(pb);
    return ok;
}

bool EncodePng(const Image& img, const std::wstring& path) {
    GdkPixbuf* pb = ToPixbuf(img);
    const bool ok = gdk_pixbuf_save(pb, plat::Native(path).c_str(), "png", nullptr, nullptr);
    g_object_unref(pb);
    return ok;
}

bool EncodePngBytes(const Image& img, std::string* bytes) {
    GdkPixbuf* pb = ToPixbuf(img);
    gchar* buf = nullptr;
    gsize len = 0;
    const bool ok = gdk_pixbuf_save_to_buffer(pb, &buf, &len, "png", nullptr, nullptr);
    if (ok) bytes->assign(buf, len);
    g_free(buf);
    g_object_unref(pb);
    return ok;
}

}  // namespace bb::mascot
