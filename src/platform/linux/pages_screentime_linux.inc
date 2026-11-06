// ── Screen Time page ────────────────────────────────────────────────────────
// #included inside studio_linux.cpp's anonymous namespace. The same cards as
// Windows (studio_draw.inc's DrawStudioScreenTimePage): today, the last seven
// days, and the apps with their own icons. Data: screentime_linux.cpp.

void BarsGlyph(cairo_t* cr, double cx, double cy, Col c) {
    Source(cr, c);
    cairo_set_line_width(cr, 1.2);
    cairo_move_to(cr, cx - 3, cy + 3); cairo_line_to(cr, cx - 3, cy - 1);
    cairo_move_to(cr, cx, cy + 4); cairo_line_to(cr, cx, cy - 3.5);
    cairo_move_to(cr, cx + 3, cy + 3); cairo_line_to(cr, cx + 3, cy);
    cairo_stroke(cr);
}

void PaintScreenTime(cairo_t* cr, const Rect& r) {
    namespace bst = bb::screentime;
    const Col white = W(1), grey = kGrey, cardBg = C(0x18181a), accent = Accent();
    const bb::lscreen::View view = bb::lscreen::Get();
    const bst::Summary& sum = view.summary;
    const double gap = 6, leftW = std::min(304.0, std::floor(r.w * 0.6));

    // ── Today ──
    const Rect r1{r.x, r.y, leftW, 76};
    Fill(cr, r1.x, r1.y, r1.w, r1.h, 12, F(cardBg));
    BarsGlyph(cr, r1.x + 16, r1.y + 14, F(grey));
    SText(cr, "Today", r1.x + 24, r1.y + 8, 56, 14, FSmall, F(grey));
    const double cw = (leftW - 24) / 3;
    const bool tight = cw < 84;  // a narrower page: shorter labels
    // A figure too wide for its column ("12h 45m" in a wide font) steps down a size.
    auto fig = [&](const std::string& s, double w) { return Measure(cr, s, FBig) <= w ? FBig : FSong; };
    const std::string todayText = U(bst::Duration(sum.todaySeconds));
    SText(cr, todayText, r1.x + 16, r1.y + 24, cw, 30, fig(todayText, cw), F(white));
    SText(cr, tight ? "Today" : "Screen time", r1.x + 16, r1.y + 54, cw, 14, FSmall, F(grey));
    // The middle: what's left of your daily limit, with a ring; else the daily average.
    const double mx = r1.x + 16 + cw, c3 = r1.x + 16 + cw * 2;
    const long long limit = static_cast<long long>(bb::lscreen::LimitHours()) * 3600;
    std::string big, small;
    double tx = mx;
    if (limit > 0) {
        const bool over = sum.todaySeconds >= limit;
        cairo_new_sub_path(cr);
        cairo_arc(cr, mx + 6, r1.y + 40, 11, 0, 2 * kPi);
        Source(cr, F(W(0.18)));
        cairo_set_line_width(cr, 2.2);
        cairo_stroke(cr);
        const double frac = std::clamp(static_cast<double>(sum.todaySeconds) / limit, 0.0, 1.0);
        if (frac > 0) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, mx + 6, r1.y + 40, 11, -kPi / 2, -kPi / 2 + 2 * kPi * frac);
            Source(cr, F(over ? kRed : white));
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_stroke(cr);
        }
        big = U(bst::Duration(over ? sum.todaySeconds - limit : limit - sum.todaySeconds));
        small = tight ? std::string(over ? "over " : "left of ") + std::to_string(bb::lscreen::LimitHours()) + "h"
                      : std::string(over ? "over your " : "left of your ") + std::to_string(bb::lscreen::LimitHours()) + "h limit";
        tx = mx + 24;
    } else {
        big = U(bst::Duration(sum.averageSeconds));
        small = tight ? "Average" : "Daily average";
    }
    SText(cr, big, tx, r1.y + 24, c3 - tx, 30, fig(big, c3 - tx), F(white));
    SText(cr, small, tx, r1.y + 54, c3 - tx, 14, FSmall, F(grey));
    g_hits.push_back({{mx - 4, r1.y + 22, c3 - mx + 4, 48}, Action::ScreenTimeLimit});
    SText(cr, std::to_string(sum.appsUsed), c3, r1.y + 24, r1.x + r1.w - 8 - c3, 30, FBig, F(white));
    SText(cr, tight ? "Apps" : "Apps used", c3, r1.y + 54, r1.x + r1.w - 8 - c3, 14, FSmall, F(grey));

    // ── The last seven days ──
    const Rect r2{r.x, r.y + 82, leftW, r.h - 82};
    Fill(cr, r2.x, r2.y, r2.w, r2.h, 12, F(cardBg));
    RoundRect(cr, r2.x + 11.5, r2.y + 9.5, 9, 9, 1.5);
    Source(cr, F(grey));
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    SText(cr, "Last 7 days", r2.x + 25, r2.y + 8, 115, 14, FSmall, F(grey));
    if (sum.averageSeconds > 0) SText(cr, "avg " + U(bst::Duration(sum.averageSeconds)), r2.x + r2.w - 110, r2.y + 8, 96, 14, FSmall, F(grey), 2);
    static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    long long most = 1;
    for (const bst::DayTotal& d : sum.week) most = std::max(most, d.seconds);
    const double dw = (leftW - 20) / 7, baseline = r2.y + r2.h - 22, tallest = baseline - (r2.y + 46);
    for (size_t i = 0; i < sum.week.size() && i < 7; ++i) {
        const bst::DayTotal& d = sum.week[i];
        const double dx = r2.x + 10 + i * dw, cx = dx + dw / 2;
        SText(cr, kDays[d.weekday % 7], dx, r2.y + r2.h - 18, dw, 14, FSmall, F(d.today ? white : grey), 1);
        if (d.seconds >= 60) {
            const double h = std::max(6.0, tallest * d.seconds / most);
            Fill(cr, cx - 9, baseline - h, 18, h, 4.5, F(d.today ? white : WithA(grey, 0.55)));
            const long long m = d.seconds / 60;
            SText(cr, m >= 60 ? std::to_string(m / 60) + "h" : std::to_string(m) + "m", dx - 2, baseline - h - 16, dw + 4, 14, FSmall,
                  F(d.today ? white : grey), 1);
        } else {
            Fill(cr, cx - 8, baseline - 1.5, 16, 3, 1, F(C(0x38383a)));
        }
    }

    // ── By app ──
    const Rect r3{r.x + leftW + gap, r.y, r.w - leftW - gap, r.h};
    Fill(cr, r3.x, r3.y, r3.w, r3.h, 12, F(cardBg));
    BarsGlyph(cr, r3.x + 16, r3.y + 14, F(grey));
    SText(cr, "By app", r3.x + 24, r3.y + 8, 66, 14, FSmall, F(grey));
    if (sum.apps.empty()) {
        const bool can = bb::lscreen::Supported();
        SText(cr, !bb::lscreen::On() ? "Screen Time is off" : can ? "Counting from now" : "Not on this desktop", r3.x + 14, r3.y + 60,
              r3.w - 28, 20, FBody, F(white), 1);
        SText(cr, can ? "Apps show here as you use them" : "Wayland doesn't show which app is in front", r3.x + 10, r3.y + 82, r3.w - 20,
              16, FSmall, F(grey), 1);
        return;
    }
    const double rowH = 28, listY = r3.y + 28;
    for (size_t i = 0; i < sum.apps.size() && i < 5; ++i) {
        const bst::AppTime& app = sum.apps[i];
        const double y = listY + i * (rowH + 2);
        if (y + rowH > r3.y + r3.h - 2) break;
        const Rect ic{r3.x + 12, y + 2, 20, 20};
        if (GdkPixbuf* pb = i < view.icons.size() ? view.icons[i] : nullptr) {  // the app's own icon
            cairo_save(cr);
            const double k = ic.w / std::max(1, gdk_pixbuf_get_width(pb));
            cairo_translate(cr, ic.x, ic.y);
            cairo_scale(cr, k, k);
            gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
            cairo_paint_with_alpha(cr, g_alpha);
            cairo_restore(cr);
        } else {  // its initial on a tile, until (or unless) an icon turns up
            Fill(cr, ic.x, ic.y, ic.w, ic.h, 5, F(C(0x3a3a3c)));
            SText(cr, app.name.substr(0, 1), ic.x, ic.y, ic.w, ic.h, FLabel, F(white), 1);
        }
        SText(cr, app.name, r3.x + 38, y, r3.w - 92, 14, FLabel, F(white));
        SText(cr, U(bst::Duration(app.seconds)), r3.x + r3.w - 54, y, 44, 14, FLabel, F(white), 2);
        const double barL = r3.x + 38, barR = r3.x + r3.w - 12, barY = y + 16.5;
        Fill(cr, barL, barY, barR - barL, 2.5, 1.2, F(C(0x28282a)));
        const double fw = (barR - barL) * std::clamp(app.percent / 100.0, 0.0, 1.0);
        if (fw > 1) Fill(cr, barL, barY, fw, 2.5, 1.2, F(i == 0 ? accent : white));
    }
}
