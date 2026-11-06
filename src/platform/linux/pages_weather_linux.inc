// ── Weather page: now, the next hours, five days ─────────────────────────────
// #included inside studio_linux.cpp's anonymous namespace. The same three cards
// as Windows (studio_draw.inc's DrawStudioWeatherPage). Only real values show:
// a detail the service didn't send is left out, never made up.

// A small weather glyph for an Open-Meteo (WMO) code: sun, sun and cloud, cloud,
// fog, rain, snow or thunder.
void WeatherGlyph(cairo_t* cr, double cx, double cy, double size, int code, Col strong) {
    const Col soft = WithA(strong, strong.a * 0.45);
    const double s = size / 24;
    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, std::max(1.0, 1.6 * s));
    auto sun = [&](double x, double y, double r, Col c) {
        Dot(cr, x, y, r, c);
        Source(cr, c);
        for (int i = 0; i < 8; ++i) {
            const double a = i * kPi / 4;
            cairo_move_to(cr, x + std::cos(a) * r * 1.5, y + std::sin(a) * r * 1.5);
            cairo_line_to(cr, x + std::cos(a) * r * 2.0, y + std::sin(a) * r * 2.0);
        }
        cairo_stroke(cr);
    };
    auto cloud = [&](double x, double y, Col c) {
        Source(cr, c);
        cairo_new_sub_path(cr);
        cairo_arc(cr, x - 4 * s, y + 1 * s, 4 * s, kPi / 2, 3 * kPi / 2);
        cairo_arc(cr, x + 0.5 * s, y - 2 * s, 5 * s, kPi, 2 * kPi);
        cairo_arc(cr, x + 5 * s, y + 1 * s, 4 * s, 3 * kPi / 2, kPi / 2);
        cairo_close_path(cr);
        cairo_fill(cr);
    };
    const bool clear = code <= 1, partly = code == 2, overcast = code == 3, fog = code == 45 || code == 48;
    const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86, thunder = code >= 95;
    const bool rain = !snow && !thunder && code >= 51;
    if (clear) sun(cx, cy, 5 * s, strong);
    else if (partly) { sun(cx - 3 * s, cy - 3 * s, 3.5 * s, soft); cloud(cx + 1 * s, cy + 3 * s, strong); }
    else if (fog) {
        Source(cr, strong);
        for (int i = -1; i <= 1; ++i) { cairo_move_to(cr, cx - 8 * s, cy + i * 4 * s); cairo_line_to(cr, cx + 8 * s, cy + i * 4 * s); }
        cairo_stroke(cr);
    } else {
        cloud(cx, cy - (overcast ? 0 : 2) * s, strong);
        Source(cr, soft);
        if (rain) for (int i = -1; i <= 1; ++i) { cairo_move_to(cr, cx + i * 4 * s, cy + 5 * s); cairo_line_to(cr, cx + i * 4 * s - 1.5 * s, cy + 9 * s); }
        if (snow) for (int i = -1; i <= 1; ++i) { cairo_new_sub_path(cr); cairo_arc(cr, cx + i * 4 * s, cy + 7 * s, 1.1 * s, 0, 2 * kPi); }
        if (thunder) { cairo_move_to(cr, cx + 1 * s, cy + 4 * s); cairo_line_to(cr, cx - 1.5 * s, cy + 8 * s); cairo_line_to(cr, cx + 1.5 * s, cy + 8 * s); cairo_line_to(cr, cx - 1 * s, cy + 12 * s); }
        if (snow) cairo_fill(cr); else cairo_stroke(cr);
    }
    cairo_restore(cr);
}

std::string Deg(double t) { return std::to_string(static_cast<int>(std::lround(t))) + "\xC2\xB0"; }

void PaintWeatherCards(cairo_t* cr, const Rect& rect) {
    const lsrc::Weather& w = lsrc::CurrentWeather();
    if (!w.has) {  // still loading, or the service couldn't be reached
        WeatherGlyph(cr, rect.x + rect.w / 2, rect.y + 56, 34, 0, kSky);
        SText(cr, w.loading ? "Getting the weather\xE2\x80\xA6" : w.error, rect.x, rect.y + 86, rect.w, 22, FBody, W(0.8), 1);
        if (!w.loading) SText(cr, "Change City sets where.", rect.x, rect.y + 108, rect.w, 18, FSmall, C(0x636366), 1);
        return;
    }
    const double gap = 6, w1 = 138, w3 = 140, w2 = rect.w - w1 - w3 - gap * 2;
    const Rect c1{rect.x, rect.y, w1, rect.h}, c2{rect.x + w1 + gap, rect.y, w2, rect.h}, c3{c2.x + w2 + gap, rect.y, w3, rect.h};
    const Col cardBg = C(0x18181a), white = W(1), grey = kGrey, accent = Accent();

    // ── Card 1: now ──
    Fill(cr, c1.x, c1.y, c1.w, c1.h, 12, F(cardBg));
    WeatherGlyph(cr, c1.x + 36, c1.y + 34, 38, w.code, F(accent));
    SText(cr, Deg(w.temp), c1.x + 14, c1.y + 54, c1.w - 28, 34, FHero, F(white));
    SText(cr, lsrc::WeatherWords(w.code), c1.x + 14, c1.y + 90, c1.w - 28, 16, FBody, F(white));
    // Linux fonts run wider than Windows': the long forms where they fit, short ones otherwise.
    auto fit = [&](const std::string& longer, const std::string& shorter) {
        return Measure(cr, longer, FSmall) <= c1.w - 18 ? longer : shorter;
    };
    const std::string hiLo = "H " + Deg(w.high) + " L " + Deg(w.low);
    const std::string hl = w.feels > -900 ? fit(hiLo + kDot + "Feels like " + Deg(w.feels), hiLo + kDot + "Feels " + Deg(w.feels)) : hiLo;
    SText(cr, hl, c1.x + 10, c1.y + 108, c1.w - 18, 16, FSmall, F(grey));
    Fill(cr, c1.x + 14, c1.y + 132, c1.w - 28, 1, 0, F(C(0x28282a)));
    if (!w.sunrise.empty() && !w.sunset.empty())
        SText(cr, fit("Sunrise " + w.sunrise + kDot + "Sunset " + w.sunset, "Rise " + w.sunrise + kDot + "Set " + w.sunset), c1.x + 10,
              c1.y + 140, c1.w - 18, 14, FSmall, F(grey));
    const int mins = static_cast<int>(std::max(0.0, Now() - w.updatedAt) / 60);
    SText(cr, mins <= 1 ? "Updated just now" : "Updated " + std::to_string(mins) + "m ago", c1.x + 14, c1.y + 156, c1.w - 28, 14, FSmall,
          F(C(0x636366)));

    // ── Card 2: the next hours, as a curve, then humidity, wind and UV ──
    Fill(cr, c2.x, c2.y, c2.w, c2.h, 12, F(cardBg));
    SText(cr, "Hourly", c2.x + 14, c2.y + 8, 66, 14, FSmall, F(grey));
    struct Slot { std::string label; double temp; int code; };
    std::vector<Slot> slots = {{"Now", w.temp, w.code}};
    for (const auto& h : w.hours) {
        if (slots.size() >= 6) break;
        const int h12 = h.hour % 12 == 0 ? 12 : h.hour % 12;
        slots.push_back({std::to_string(h12) + (h.hour < 12 ? " AM" : " PM"), h.temp, h.code});
    }
    if (slots.size() >= 2) {
        double lo = 1e9, hi = -1e9;
        for (const auto& s : slots) { lo = std::min(lo, s.temp); hi = std::max(hi, s.temp); }
        if (hi <= lo) hi = lo + 2;
        const double step = (c2.w - 24) / (slots.size() - 1);
        std::vector<std::pair<double, double>> pts;
        for (size_t i = 0; i < slots.size(); ++i) {
            const double x = c2.x + 12 + i * step;
            SText(cr, slots[i].label, x - 18, c2.y + 22, 36, 12, FSmall, F(grey), 1);
            WeatherGlyph(cr, x, c2.y + 43, 12, slots[i].code, F(white));
            SText(cr, Deg(slots[i].temp), x - 16, c2.y + 52, 32, 12, FSmall, F(white), 1);
            pts.push_back({x, c2.y + 104 - (slots[i].temp - lo) / (hi - lo) * 26});
        }
        auto curve = [&]() {
            cairo_move_to(cr, pts[0].first, pts[0].second);
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                const auto& p0 = i ? pts[i - 1] : pts[i];
                const auto& p1 = pts[i];
                const auto& p2 = pts[i + 1];
                const auto& p3 = i + 2 < pts.size() ? pts[i + 2] : p2;
                cairo_curve_to(cr, p1.first + (p2.first - p0.first) / 6, p1.second + (p2.second - p0.second) / 6,
                               p2.first - (p3.first - p1.first) / 6, p2.second - (p3.second - p1.second) / 6, p2.first, p2.second);
            }
        };
        cairo_new_path(cr);
        curve();
        cairo_line_to(cr, pts.back().first, c2.y + 116);
        cairo_line_to(cr, pts.front().first, c2.y + 116);
        cairo_close_path(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(0, c2.y + 76, 0, c2.y + 116);
        cairo_pattern_add_color_stop_rgba(g, 0, accent.r, accent.g, accent.b, 0.25 * g_alpha);
        cairo_pattern_add_color_stop_rgba(g, 1, accent.r, accent.g, accent.b, 0);
        cairo_set_source(cr, g);
        cairo_fill(cr);
        cairo_pattern_destroy(g);
        cairo_new_path(cr);
        curve();
        Source(cr, F(accent));
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);
        for (const auto& p : pts) Dot(cr, p.first, p.second, 2.5, F(white));
    }
    const double sColW = (c2.w - 16) / 3, sy = c2.y + 136;
    auto stat = [&](int i, const std::string& label, const std::string& value) {
        const double x = c2.x + 8 + sColW * i;
        SText(cr, label, x, sy, sColW, 14, FSmall, F(grey));
        SText(cr, value.empty() ? "\xE2\x80\x94" : value, x, sy + 15, sColW, 17, value.size() > 6 ? FSmall : FLabel, F(white));
    };
    char wind[24] = "";
    if (w.wind >= 0) snprintf(wind, sizeof wind, "%.0f %s", w.wind, w.fahrenheit ? "mph" : "km/h");
    stat(0, "Humidity", w.humidity >= 0 ? std::to_string(w.humidity) + "%" : "");
    stat(1, "Wind", wind);
    stat(2, "UV index", lsrc::UvWords(w.uv));

    // ── Card 3: five days ──
    Fill(cr, c3.x, c3.y, c3.w, c3.h, 12, F(cardBg));
    SText(cr, w.days.size() >= 5 ? "5-Day" : "Next days", c3.x + 14, c3.y + 8, 66, 14, FSmall, F(grey));
    double lo = 1e9, hi = -1e9;
    for (const auto& d : w.days) { lo = std::min(lo, d.lo); hi = std::max(hi, d.hi); }
    if (hi <= lo) hi = lo + 4;
    // Columns sized from the text itself: day names, low, the range bar, high.
    double nameW = 0, hiW = 0, loW = 0;
    for (const auto& d : w.days) {
        nameW = std::max(nameW, Measure(cr, d.name, FLabel));
        hiW = std::max(hiW, Measure(cr, Deg(d.hi), FLabel));
        loW = std::max(loW, Measure(cr, Deg(d.lo), FSmall));
    }
    nameW = std::min(nameW, 46.0);
    for (size_t i = 0; i < w.days.size() && i < 5; ++i) {
        const lsrc::Day& d = w.days[i];
        const double y = c3.y + 28 + i * 28;
        const double gx = c3.x + 12 + nameW + 10, lx = gx + 9;
        SText(cr, d.name, c3.x + 12, y, nameW + 2, 24, FLabel, F(white));
        WeatherGlyph(cr, gx, y + 12, 12, d.code, F(white));
        SText(cr, Deg(d.lo), lx, y, loW + 2, 24, FSmall, F(grey), 2);
        const double barL = lx + loW + 6, barR = c3.x + c3.w - 14 - hiW - 4, barW = std::max(8.0, barR - barL);
        Fill(cr, barL, y + 10, barW, 4, 2, F(C(0x28282a)));
        const double f0 = std::clamp((d.lo - lo) / (hi - lo), 0.0, 1.0), f1 = std::clamp((d.hi - lo) / (hi - lo), 0.0, 1.0);
        Fill(cr, barL + barW * f0, y + 10, std::max(4.0, barW * (f1 - f0)), 4, 2, F(white));
        if (i == 0) Dot(cr, barL + barW * std::clamp((w.temp - lo) / (hi - lo), 0.0, 1.0), y + 12, 3, F(accent));
        SText(cr, Deg(d.hi), c3.x + c3.w - 12 - hiW - 2, y, hiW + 2, 24, FLabel, F(white), 2);
    }
}
