// Bunbun — the screen colour picker on Linux (see picker_linux.h).
#include "picker_linux.h"

#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace bb::lpick {
namespace {

constexpr int kCells = 15;  // odd, so there is a centre pixel
constexpr int kZoom = 9;

std::function<void(uint8_t, uint8_t, uint8_t)> g_picked;
bool g_open = false;

// ── X11: the frozen screen and the loupe ────────────────────────────────────

GtkWidget* g_win = nullptr;
GdkPixbuf* g_shot = nullptr;
int g_x0 = 0, g_y0 = 0;           // the screenshot's origin on the screen
double g_px = 0, g_py = 0;         // the pointer, in screenshot pixels
GdkSeat* g_seat = nullptr;

void PixelAt(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    x = std::clamp(x, 0, gdk_pixbuf_get_width(g_shot) - 1);
    y = std::clamp(y, 0, gdk_pixbuf_get_height(g_shot) - 1);
    const guchar* p = gdk_pixbuf_read_pixels(g_shot) + y * gdk_pixbuf_get_rowstride(g_shot) + x * gdk_pixbuf_get_n_channels(g_shot);
    *r = p[0];
    *g = p[1];
    *b = p[2];
}

void Close(bool pick) {
    if (!g_win) return;
    uint8_t r = 0, g = 0, b = 0;
    if (pick) PixelAt(static_cast<int>(g_px), static_cast<int>(g_py), &r, &g, &b);
    if (g_seat) gdk_seat_ungrab(g_seat);
    gtk_widget_destroy(g_win);
    g_win = nullptr;
    if (g_shot) g_object_unref(g_shot);
    g_shot = nullptr;
    g_open = false;
    if (pick && g_picked) g_picked(r, g, b);
}

gboolean OnDraw(GtkWidget*, cairo_t* cr, gpointer) {
    // The frozen screen, then the loupe beside the pointer.
    gdk_cairo_set_source_pixbuf(cr, g_shot, 0, 0);
    cairo_paint(cr);
    const int cx = static_cast<int>(g_px), cy = static_cast<int>(g_py);
    const double size = kCells * kZoom;
    double lx = g_px + 24, ly = g_py + 24;
    const int sw = gdk_pixbuf_get_width(g_shot), sh = gdk_pixbuf_get_height(g_shot);
    if (lx + size + 8 > sw) lx = g_px - 24 - size;
    if (ly + size + 34 > sh) ly = g_py - 24 - size - 26;
    cairo_save(cr);
    cairo_arc(cr, lx + size / 2, ly + size / 2, size / 2 + 4, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.08, 0.08, 0.1, 0.95);
    cairo_fill(cr);
    cairo_arc(cr, lx + size / 2, ly + size / 2, size / 2, 0, 2 * M_PI);
    cairo_clip(cr);
    for (int dy = 0; dy < kCells; ++dy)
        for (int dx = 0; dx < kCells; ++dx) {
            uint8_t r, g, b;
            PixelAt(cx - kCells / 2 + dx, cy - kCells / 2 + dy, &r, &g, &b);
            cairo_rectangle(cr, lx + dx * kZoom, ly + dy * kZoom, kZoom, kZoom);
            cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
            cairo_fill(cr);
        }
    // The centre pixel outlined, dark and light so it shows on any colour.
    const double c0x = lx + (kCells / 2) * kZoom, c0y = ly + (kCells / 2) * kZoom;
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, c0x - 1, c0y - 1, kZoom + 2, kZoom + 2);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, c0x, c0y, kZoom, kZoom);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_stroke(cr);
    cairo_restore(cr);
    // Its hex code under the loupe.
    uint8_t r, g, b;
    PixelAt(cx, cy, &r, &g, &b);
    char hex[16];
    snprintf(hex, sizeof hex, "#%02X%02X%02X", r, g, b);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string("Monospace Bold 11");
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, hex, -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double bx = lx + size / 2 - tw / 2.0 - 10, by = ly + size + 10;
    cairo_rectangle(cr, bx, by, tw + 20, th + 8);
    cairo_set_source_rgba(cr, 0.08, 0.08, 0.1, 0.95);
    cairo_fill(cr);
    cairo_rectangle(cr, bx + 6, by + (th + 8) / 2.0 - 4, 8, 8);
    cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, bx + 18, by + 4);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
    return TRUE;
}

gboolean OnMotion(GtkWidget* w, GdkEventMotion* e, gpointer) {
    g_px = e->x;
    g_py = e->y;
    gtk_widget_queue_draw(w);
    return TRUE;
}

gboolean OnButton(GtkWidget*, GdkEventButton* e, gpointer) {
    if (e->type != GDK_BUTTON_PRESS) return TRUE;
    Close(e->button == 1);
    return TRUE;
}

gboolean OnKey(GtkWidget* w, GdkEventKey* e, gpointer) {
    switch (e->keyval) {
        case GDK_KEY_Escape: Close(false); return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_space: Close(true); return TRUE;
        case GDK_KEY_Left: g_px -= 1; break;
        case GDK_KEY_Right: g_px += 1; break;
        case GDK_KEY_Up: g_py -= 1; break;
        case GDK_KEY_Down: g_py += 1; break;
        default: return TRUE;
    }
    // Arrow keys move the real pointer too, so the loupe and the pointer agree.
    if (GdkDevice* ptr = g_seat ? gdk_seat_get_pointer(g_seat) : nullptr)
        gdk_device_warp(ptr, gtk_widget_get_screen(w), g_x0 + static_cast<int>(g_px), g_y0 + static_cast<int>(g_py));
    gtk_widget_queue_draw(w);
    return TRUE;
}

bool StartX11() {
    GdkWindow* root = gdk_get_default_root_window();
    const int w = gdk_window_get_width(root), h = gdk_window_get_height(root);
    g_shot = gdk_pixbuf_get_from_window(root, 0, 0, w, h);
    if (!g_shot) return false;
    g_x0 = g_y0 = 0;
    g_win = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_move(GTK_WINDOW(g_win), 0, 0);
    gtk_window_set_default_size(GTK_WINDOW(g_win), w, h);
    gtk_widget_set_size_request(g_win, w, h);
    gtk_widget_set_app_paintable(g_win, TRUE);
    gtk_widget_add_events(g_win, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(g_win, "draw", G_CALLBACK(OnDraw), nullptr);
    g_signal_connect(g_win, "motion-notify-event", G_CALLBACK(OnMotion), nullptr);
    g_signal_connect(g_win, "button-press-event", G_CALLBACK(OnButton), nullptr);
    g_signal_connect(g_win, "key-press-event", G_CALLBACK(OnKey), nullptr);
    gtk_widget_show_all(g_win);
    // The pointer where it is now, and every click and key to us until it's done.
    g_seat = gdk_display_get_default_seat(gdk_display_get_default());
    int x = w / 2, y = h / 2;
    gdk_device_get_position(gdk_seat_get_pointer(g_seat), nullptr, &x, &y);
    g_px = x;
    g_py = y;
    GdkCursor* cross = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_CROSSHAIR);
    gdk_seat_grab(g_seat, gtk_widget_get_window(g_win), GDK_SEAT_CAPABILITY_ALL, TRUE, cross, nullptr, nullptr, nullptr);
    g_object_unref(cross);
    return true;
}

// ── Wayland: the screenshot portal's PickColor ──────────────────────────────

struct PortalWait {
    GDBusConnection* bus = nullptr;
    guint sub = 0;
};

void OnPortalResponse(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params, gpointer data) {
    auto* wait = static_cast<PortalWait*>(data);
    g_dbus_connection_signal_unsubscribe(wait->bus, wait->sub);
    g_object_unref(wait->bus);
    delete wait;
    g_open = false;
    guint32 response = 2;
    GVariant* results = nullptr;
    g_variant_get(params, "(u@a{sv})", &response, &results);
    double r = 0, g = 0, b = 0;
    const bool ok = response == 0 && results && g_variant_lookup(results, "color", "(ddd)", &r, &g, &b);
    if (results) g_variant_unref(results);
    const auto byte = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    if (ok && g_picked) g_picked(byte(r), byte(g), byte(b));
}

bool StartPortal() {
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return false;
    // The answer comes on /org/freedesktop/portal/desktop/request/<sender>/<token>;
    // listen there before asking, so it can't be missed.
    std::string sender = g_dbus_connection_get_unique_name(bus) + 1;  // without the ':'
    for (char& c : sender) if (c == '.') c = '_';
    static int counter = 0;
    const std::string token = "bunbun" + std::to_string(++counter);
    const std::string handle = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    auto* wait = new PortalWait{bus, 0};
    wait->sub = g_dbus_connection_signal_subscribe(bus, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request", "Response",
                                                   handle.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, OnPortalResponse, wait, nullptr);
    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
    GError* err = nullptr;
    GVariant* r = g_dbus_connection_call_sync(bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                              "org.freedesktop.portal.Screenshot", "PickColor", g_variant_new("(sa{sv})", "", &opts),
                                              G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &err);
    if (!r) {  // no portal, or no Screenshot interface in it
        g_clear_error(&err);
        g_dbus_connection_signal_unsubscribe(bus, wait->sub);
        g_object_unref(bus);
        delete wait;
        return false;
    }
    g_variant_unref(r);
    g_open = true;
    return true;
}

}  // namespace

void Start(std::function<void(uint8_t, uint8_t, uint8_t)> picked) {
    if (g_open || g_win) return;
    g_picked = std::move(picked);
    // A Wayland session can't take screenshots of other apps (XWayland only sees X
    // windows): the desktop's own picker there. Elsewhere our loupe, the portal as
    // the fallback.
    const bool wayland = getenv("WAYLAND_DISPLAY") != nullptr;
    if (wayland && StartPortal()) return;
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default()) && StartX11()) {
        g_open = true;
        return;
    }
#endif
    if (!wayland) StartPortal();
}

bool Open() { return g_open; }

}  // namespace bb::lpick
