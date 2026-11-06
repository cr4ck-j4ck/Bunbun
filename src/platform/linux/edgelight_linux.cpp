// The edge light on Linux. Same looks, triggers and settings as Windows
// (src/island/modules/edgelight_*.inc); cairo has no "point at length", so the
// outline is flattened once per size and walked by hand.
#include "edgelight_linux.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../../common/edgelight.h"
#include "look_linux.h"
#include "settings.h"

namespace bb::ledge {
namespace {

int g_style = 2;          // 0 off, 1 comet, 2 twin comets, 3 breathe
unsigned g_on = 0;        // bb::edgelight::kOn* bits
int g_colour = 0;         // 0 default (white), 1 dynamic, 2 custom
double g_custom[3] = {0.353, 0.784, 0.98};

constexpr double kPlaySeconds = 4.0, kFadeIn = 0.25, kFadeOut = 0.9;
double g_start = -1, g_until = -1;

// The flattened outline: points and the length up to each.
struct Pt { double x, y; };
std::vector<Pt> g_pts;
std::vector<double> g_len;
bool g_closed = true;
double g_key[5] = {-1, -1, -1, -1, -1};

double Level(double now) {
    if (g_style == 0) return 0;
    if (g_on & bb::edgelight::kOnAlways) return 1;
    if (g_start < 0 || now >= g_until) return 0;
    return std::min(std::clamp((now - g_start) / kFadeIn, 0.0, 1.0), std::clamp((g_until - now) / kFadeOut, 0.0, 1.0));
}

void Flatten(cairo_t* cr, double x, double y, double w, double h, double r) {
    const double key[5] = {x, y, w, h, r};
    if (std::equal(key, key + 5, g_key)) return;
    std::copy(key, key + 5, g_key);
    cairo_save(cr);
    cairo_identity_matrix(cr);  // the path in our own units, whatever the scale
    g_closed = bb::llook::OutlinePath(cr, x, y, w, h, r);
    cairo_path_t* path = cairo_copy_path_flat(cr);
    cairo_new_path(cr);
    cairo_restore(cr);
    g_pts.clear();
    g_len.clear();
    for (int i = 0; i < path->num_data; i += path->data[i].header.length) {
        const cairo_path_data_t& d = path->data[i];
        if (d.header.type == CAIRO_PATH_MOVE_TO || d.header.type == CAIRO_PATH_LINE_TO)
            g_pts.push_back({path->data[i + 1].point.x, path->data[i + 1].point.y});
    }
    cairo_path_destroy(path);
    if (g_closed && !g_pts.empty()) g_pts.push_back(g_pts.front());
    double total = 0;
    for (size_t i = 0; i < g_pts.size(); ++i) {
        if (i) total += std::hypot(g_pts[i].x - g_pts[i - 1].x, g_pts[i].y - g_pts[i - 1].y);
        g_len.push_back(total);
    }
}

double Length() { return g_len.empty() ? 0 : g_len.back(); }

Pt At(double d) {
    const double L = Length();
    if (g_closed) d = std::fmod(std::fmod(d, L) + L, L);
    else d = std::clamp(d, 0.0, L);
    const size_t i = static_cast<size_t>(std::lower_bound(g_len.begin(), g_len.end(), d) - g_len.begin());
    if (i == 0) return g_pts.front();
    if (i >= g_pts.size()) return g_pts.back();
    const double seg = g_len[i] - g_len[i - 1], t = seg > 0 ? (d - g_len[i - 1]) / seg : 0;
    return {g_pts[i - 1].x + (g_pts[i].x - g_pts[i - 1].x) * t, g_pts[i - 1].y + (g_pts[i].y - g_pts[i - 1].y) * t};
}

void Comet(cairo_t* cr, double s, double dir, const double c[3], double level) {
    const double L = Length();
    const double tail = std::clamp(L * 0.2, 36.0, 200.0);
    const double ends = g_closed ? 1.0 : std::clamp(std::min(s, L - s) / 28.0, 0.0, 1.0);
    const double a0 = level * ends;
    if (a0 <= 0.01) return;
    constexpr int kSteps = 24;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    Pt prev = At(s);
    const Pt head = prev;
    for (int i = 1; i <= kSteps; ++i) {
        const double d = s - dir * tail * i / kSteps;
        if (!g_closed && (d < 0 || d > L)) break;
        const Pt p = At(d);
        const double f = 1.0 - static_cast<double>(i - 1) / kSteps, a = a0 * f * f;
        cairo_move_to(cr, prev.x, prev.y);
        cairo_line_to(cr, p.x, p.y);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], a * 0.22);
        cairo_set_line_width(cr, 5);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], a * 0.95);
        cairo_set_line_width(cr, 1.6);
        cairo_stroke(cr);
        prev = p;
    }
    cairo_arc(cr, head.x, head.y, 4.5, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, c[0], c[1], c[2], a0 * 0.10);
    cairo_fill(cr);
    cairo_arc(cr, head.x, head.y, 1.5, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, c[0], c[1], c[2], a0);
    cairo_fill(cr);
}

double Hex(const std::wstring& s, int at) { return std::wcstol(s.substr(static_cast<size_t>(at), 2).c_str(), nullptr, 16) / 255.0; }

}  // namespace

void LoadSettings() {
    namespace s = bb::settings;
    std::wstring style = s::Get(L"Bunbun.EdgeLight");
    for (auto& c : style) c = towlower(c);
    g_style = style == L"off" ? 0 : style == L"comet" ? 1 : style == L"breathe" ? 3 : 2;
    g_on = bb::edgelight::ParseOn(s::Get(L"Bunbun.EdgeLightOn"));
    std::wstring col = s::Get(L"Bunbun.EdgeLightColor");
    for (auto& c : col) c = towlower(c);
    g_colour = col == L"dynamic" || col == L"accent" ? 1 : col == L"custom" ? 2 : 0;
    std::wstring hex = s::Get(L"Bunbun.EdgeLightCustomColor");
    if (!hex.empty() && hex[0] == L'#') hex.erase(0, 1);
    if (hex.size() >= 6) {
        g_custom[0] = Hex(hex, 0);
        g_custom[1] = Hex(hex, 2);
        g_custom[2] = Hex(hex, 4);
    }
}

void Note(unsigned events, double now) {
    if (!(events & g_on)) return;
    if (now >= g_until) g_start = now;  // a play already running just lasts longer
    g_until = now + kPlaySeconds;
}

bool Lit(double now) { return Level(now) > 0; }

void Draw(cairo_t* cr, double x, double y, double w, double h, double r, const double accent[3], double now) {
    const double level = Level(now);
    if (level <= 0.01) return;
    // Half a pixel in, so the line sits on the surface's edge.
    Flatten(cr, x + 0.75, y + 0.75, w - 1.5, h - 1.5, std::max(0.0, r - 0.75));
    const double L = Length();
    if (L < 24 || g_pts.size() < 2) return;
    double c[3] = {1, 1, 1};
    if (g_colour == 1) std::copy(accent, accent + 3, c);
    else if (g_colour == 2) std::copy(g_custom, g_custom + 3, c);
    const double t = (g_on & bb::edgelight::kOnAlways) || g_start < 0 ? now : now - g_start;
    cairo_save(cr);
    if (g_style == 3) {  // Breathe: the whole outline glows and fades
        const double b = 0.5 - 0.5 * std::cos(t * 2 * M_PI / 2.8);
        for (int pass = 0; pass < 2; ++pass) {
            cairo_new_path(cr);
            for (size_t i = 0; i < g_pts.size(); ++i)
                i ? cairo_line_to(cr, g_pts[i].x, g_pts[i].y) : cairo_move_to(cr, g_pts[i].x, g_pts[i].y);
            cairo_set_line_width(cr, pass ? 1.3 : 5);
            cairo_set_source_rgba(cr, c[0], c[1], c[2], level * (pass ? 0.18 + 0.55 * b : 0.06 + 0.16 * b));
            cairo_stroke(cr);
        }
        cairo_restore(cr);
        return;
    }
    const double period = 3.6, u = std::fmod(t, period) / period;
    const double ping = u < 0.5 ? u * 2 : 2 - u * 2;
    const double eased = ping * ping * (3 - 2 * ping), dir = u < 0.5 ? 1 : -1;
    if (g_style == 2) {
        const double s = eased * L * 0.5;
        Comet(cr, s, dir, c, level);
        Comet(cr, L - s, -dir, c, level);
    } else if (g_closed) {
        Comet(cr, u * L, 1, c, level);
    } else {
        Comet(cr, eased * L, dir, c, level);
    }
    cairo_restore(cr);
}

}  // namespace bb::ledge
