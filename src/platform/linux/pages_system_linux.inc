// ── System page: battery, network, performance, volume ──────────────────────
// #included inside studio_linux.cpp's anonymous namespace. The same four cards
// as Windows (studio_draw.inc's DrawStudioSystemPage).

// A rolling history for the small waves: one sample every 0.4 s, 28 kept.
struct WaveHistory {
    static constexpr size_t kMax = 28;
    double buf[kMax] = {};
    size_t count = 0;
    double lastPush = 0;
    void Push(double v, double now) {
        if (count && now - lastPush < 0.4) { buf[count - 1] = v; return; }
        lastPush = now;
        if (count < kMax) buf[count++] = v;
        else { std::copy(buf + 1, buf + kMax, buf); buf[kMax - 1] = v; }
    }
};
WaveHistory g_cpuWave, g_gpuWave, g_ramWave, g_diskWave, g_downWave, g_upWave;

// A smooth line through the samples (Catmull-Rom as Béziers), scaled to [lo, hi];
// filled below in a fade when `filled`. A single sample draws flat.
void SmoothWave(cairo_t* cr, const Rect& r, const WaveHistory& w, double lo, double hi, Col c, double width, bool filled = false) {
    if (r.w <= 0 || r.h <= 0) return;
    std::vector<double> v(w.buf, w.buf + std::max<size_t>(w.count, 1));
    if (v.size() < 2) v.push_back(v[0]);
    const size_t n = v.size();
    std::vector<std::pair<double, double>> p(n);
    for (size_t i = 0; i < n; ++i)
        p[i] = {r.x + r.w * i / (n - 1), r.y + r.h - std::clamp((v[i] - lo) / std::max(1e-3, hi - lo), 0.0, 1.0) * r.h};
    cairo_new_path(cr);
    cairo_move_to(cr, p[0].first, p[0].second);
    for (size_t i = 0; i + 1 < n; ++i) {
        const auto& p0 = i ? p[i - 1] : p[i];
        const auto& p1 = p[i];
        const auto& p2 = p[i + 1];
        const auto& p3 = i + 2 < n ? p[i + 2] : p2;
        cairo_curve_to(cr, p1.first + (p2.first - p0.first) / 6, p1.second + (p2.second - p0.second) / 6,
                       p2.first - (p3.first - p1.first) / 6, p2.second - (p3.second - p1.second) / 6, p2.first, p2.second);
    }
    if (filled) {
        cairo_path_t* line = cairo_copy_path(cr);
        cairo_line_to(cr, r.x + r.w, r.y + r.h);
        cairo_line_to(cr, r.x, r.y + r.h);
        cairo_close_path(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
        cairo_pattern_add_color_stop_rgba(g, 0, c.r, c.g, c.b, 0.25 * g_alpha);
        cairo_pattern_add_color_stop_rgba(g, 1, c.r, c.g, c.b, 0);
        cairo_set_source(cr, g);
        cairo_fill(cr);
        cairo_pattern_destroy(g);
        cairo_append_path(cr, line);
        cairo_path_destroy(line);
    }
    Source(cr, F(c));
    cairo_set_line_width(cr, width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);
}

std::string BitRate(double kbs) {
    char b[32];
    const double bits = kbs * 8;  // KB/s -> kbit/s
    if (bits >= 1000 * 1000) snprintf(b, sizeof b, "%.1f Gbps", bits / 1e6);
    else if (bits >= 1000) snprintf(b, sizeof b, "%.1f Mbps", bits / 1000);
    else snprintf(b, sizeof b, "%.0f Kbps", bits);
    return b;
}

void SpeakerGlyph(cairo_t* cr, double cx, double cy, double size, bool muted, Col c) {
    const double s = size / 13;
    Source(cr, F(c));
    cairo_move_to(cr, cx - 5 * s, cy - 2 * s);
    cairo_line_to(cr, cx - 2.5 * s, cy - 2 * s);
    cairo_line_to(cr, cx + 1 * s, cy - 5 * s);
    cairo_line_to(cr, cx + 1 * s, cy + 5 * s);
    cairo_line_to(cr, cx - 2.5 * s, cy + 2 * s);
    cairo_line_to(cr, cx - 5 * s, cy + 2 * s);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_set_line_width(cr, 1.2);
    if (muted) {
        cairo_move_to(cr, cx + 3 * s, cy - 2.5 * s); cairo_line_to(cr, cx + 7 * s, cy + 2.5 * s);
        cairo_move_to(cr, cx + 7 * s, cy - 2.5 * s); cairo_line_to(cr, cx + 3 * s, cy + 2.5 * s);
    } else {
        cairo_arc(cr, cx + 1 * s, cy, 4 * s, -0.7, 0.7);
    }
    cairo_stroke(cr);
}

void PaintSystemCards(cairo_t* cr, const Rect& rect) {
    const double now = Now();
    const double gap = 10, colW = (rect.w - gap) / 2, rowH = (rect.h - gap) / 2;
    // Quick toggles (Network) and Hardware monitor (Performance) switch their cards off;
    // the neighbour then takes the whole row (as on Windows).
    const bool netCard = bb::settings::GetBool(L"Modules.ConnectivityModule");
    const bool perfCard = bb::settings::GetBool(L"Modules.HardwareMonitorModule");
    const Rect r1{rect.x, rect.y, netCard ? colW : rect.w, rowH}, r2{rect.x + colW + gap, rect.y, colW, rowH};
    const Rect r3{rect.x, rect.y + rowH + gap, colW, rowH};
    const Rect r4{perfCard ? rect.x + colW + gap : rect.x, rect.y + rowH + gap, perfCard ? colW : rect.w, rowH};
    const Col cardBg = C(0x18181a), white = W(1), grey = kGrey, labelCol = C(0xa0a0a0), green = kGreen, accent = Accent();
    const lsrc::Load& load = lsrc::SystemLoad();
    const lsrc::Battery& bat = lsrc::BatteryState();
    const lsrc::Radios& radios = lsrc::RadioState();

    // ── Card 1: Battery ──
    Fill(cr, r1.x, r1.y, r1.w, r1.h, 12, F(cardBg));
    {
        RoundRect(cr, r1.x + 16, r1.y + 10, 13, 9, 2);
        Source(cr, F(labelCol));
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        Fill(cr, r1.x + 29.5, r1.y + 12.5, 1.5, 4, 0.8, F(labelCol));
        SText(cr, "Battery", r1.x + 34, r1.y + 8, 86, 14, FLabel, F(labelCol));
        if (bat.has && bat.health >= 0) SText(cr, "Health " + std::to_string(bat.health) + "%", r1.x + r1.w - 106, r1.y + 8, 90, 14, FSmall, F(grey), 2);
        else if (!bat.has) SText(cr, "AC Power", r1.x + r1.w - 106, r1.y + 8, 90, 14, FSmall, F(grey), 2);
        const int pct = bat.has ? bat.percent : 100;
        const std::string big = std::to_string(pct) + "%";
        SText(cr, big, r1.x + 16, r1.y + 26, 90, 30, FBig, F(white));
        std::string sub;
        if (!bat.has) sub = "Plugged in";
        else if (bat.full || (pct == 100 && bat.onPower)) sub = "Fully charged";
        else if (bat.charging) sub = "Charging";
        else if (bat.secondsLeft > 0) sub = std::to_string(bat.secondsLeft / 3600) + "h " + std::to_string(bat.secondsLeft % 3600 / 60) + "m remaining";
        else sub = "On battery";
        const double bw = Measure(cr, big, FBig);
        SText(cr, sub, r1.x + 24 + bw, r1.y + 34, r1.w - 40 - bw, 18, FLabel, F(grey));
        Fill(cr, r1.x + 16, r1.y + 64, r1.w - 32, 7, 3.5, F(C(0x28282a)));
        Fill(cr, r1.x + 16, r1.y + 64, (r1.w - 32) * std::clamp(pct / 100.0, 0.0, 1.0), 7, 3.5, F(bat.charging ? green : accent));
    }

    // ── Card 2: Network, with the Wi-Fi and Bluetooth switches ──
    if (netCard) {
    Fill(cr, r2.x, r2.y, r2.w, r2.h, 12, F(cardBg));
        const double wx = r2.x + 22, wy = r2.y + 17;
        Source(cr, F(accent));
        cairo_set_line_width(cr, 1.2);
        for (double rr : {2.5, 5.5, 8.5}) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, wx, wy, rr, -2.4, -0.74);
            cairo_stroke(cr);
        }
        SText(cr, "Network", r2.x + 34, r2.y + 8, 86, 14, FLabel, F(labelCol));
        // The chips: on in the accent; a radio the system can't switch is dimmed and opens settings.
        double chipR = r2.x + r2.w - 12;
        auto chip = [&](const char* label, bool known, bool on, int idx) {
            const double w = Measure(cr, label, FSmall) + 20;
            const Rect c{chipR - w, r2.y + 6, w, 18};
            const bool hover = NextHitHovered();
            Fill(cr, c.x, c.y, c.w, c.h, 9, F(on ? WithA(accent, hover ? 1.0 : 0.85) : (hover ? C(0x3a3a3c) : C(0x2c2c2e))));
            Dot(cr, c.x + 8, c.y + c.h / 2, 2.5, F(on ? white : grey));
            SText(cr, label, c.x + 13, c.y, c.w - 17, c.h, FSmall, F(on ? white : known ? grey : C(0x5a5a5e)), 1);
            g_hits.push_back({c, known ? Action::RadioToggle : Action::RadioSettings, idx});
            chipR = c.x - 5;
        };
        chip("Bluetooth", radios.btKnown, radios.bt, 1);
        chip("Wi-Fi", radios.wifiKnown, radios.wifi, 0);
        std::string name = radios.network;
        if (name.empty()) name = radios.wifiKnown && !radios.wifi ? "Wi-Fi is off" : "Not Connected";
        SText(cr, name, r2.x + 16, r2.y + 23, r2.w - 32, 22, FTitle, F(white));
        g_hits.push_back({{r2.x + 12, r2.y + 23, std::min(r2.w - 24, Measure(cr, name, FTitle) + 8), 22}, Action::RadioSettings, 0});
        std::string sub = radios.network.empty() ? "Disconnected"
                          : radios.wired ? "Ethernet" + kDot + "Connected"
                          : radios.signal >= 0 ? "Wi-Fi" + kDot + std::to_string(radios.signal) + "% signal" : "Wi-Fi" + kDot + "Connected";
        SText(cr, sub, r2.x + 16, r2.y + 45, r2.w - 32, 14, FSmall, F(grey));
        g_downWave.Push(load.rxKBs, now);
        g_upWave.Push(load.txKBs, now);
        auto speed = [&](const std::string& text, double x, double right, const WaveHistory& w) {
            const double tw = Measure(cr, text, FSmall);
            SText(cr, text, x, r2.y + 63, tw + 4, 14, FSmall, F(white));
            double top = 1;
            for (size_t k = 0; k < w.count; ++k) top = std::max(top, w.buf[k]);
            SmoothWave(cr, {x + tw + 6, r2.y + 64, std::max(0.0, right - (x + tw + 6)), 12}, w, 0, top, accent, 1.2);
        };
        const double upStart = r2.x + r2.w * 0.54;
        speed("\xE2\x86\x93 " + BitRate(load.rxKBs), r2.x + 16, upStart - 8, g_downWave);
        speed("\xE2\x86\x91 " + BitRate(load.txKBs), upStart, r2.x + r2.w - 14, g_upWave);
    }

    // ── Card 3: Performance (GPU only when the driver reports it) ──
    if (perfCard) {
    Fill(cr, r3.x, r3.y, r3.w, r3.h, 12, F(cardBg));
        Icon(cr, Page::Hardware, r3.x + 22, r3.y + 14, 13, F(accent));
        SText(cr, "Performance", r3.x + 34, r3.y + 8, 106, 14, FLabel, F(labelCol));
        g_cpuWave.Push(load.cpu, now);
        g_ramWave.Push(load.mem, now);
        g_diskWave.Push(load.disk, now);
        if (load.gpu >= 0) g_gpuWave.Push(load.gpu, now);
        struct Col4 { const char* label; int value; const WaveHistory* wave; bool filled; };
        std::vector<Col4> cols = {{"CPU", load.cpu, &g_cpuWave, false}};
        if (load.gpu >= 0) cols.push_back({"GPU", load.gpu, &g_gpuWave, false});
        cols.push_back({"RAM", load.mem, &g_ramWave, true});
        cols.push_back({"Disk", load.disk, &g_diskWave, false});
        const double pColW = (r3.w - 32) / cols.size();
        for (size_t i = 0; i < cols.size(); ++i) {
            const double x = r3.x + 16 + pColW * i;
            SText(cr, std::to_string(cols[i].value) + "%", x, r3.y + 25, pColW, 20, FTitle, F(white));
            SText(cr, cols[i].label, x, r3.y + 45, pColW, 12, FSmall, F(grey));
            SmoothWave(cr, {x, r3.y + (cols[i].filled ? 60 : 64), pColW - 4, cols[i].filled ? 16.0 : 12.0}, *cols[i].wave, 0, 100, accent, 1.3,
                       cols[i].filled);
        }
    }

    // ── Card 4: Volume ──
    Fill(cr, r4.x, r4.y, r4.w, r4.h, 12, F(cardBg));
    {
        int vol = 0;
        bool muted = false;
        const bool have = bb::lfeat::VolumeNow(&vol, &muted);
        SpeakerGlyph(cr, r4.x + 22, r4.y + 14, 13, muted, accent);
        SText(cr, "Volume", r4.x + 34, r4.y + 8, 56, 14, FLabel, F(labelCol));
        const std::string dev = bb::lfeat::AudioOutputName();  // the real name only, never a made-up one
        if (!dev.empty()) SText(cr, dev, r4.x + 92, r4.y + 8, r4.w - 106, 14, FSmall, F(bb::lfeat::BluetoothAudio() ? green : grey), 2);
        if (!have) {
            SText(cr, "No sound control", r4.x + 16, r4.y + 30, r4.w - 32, 24, FBody, F(grey));
            return;
        }
        SText(cr, std::to_string(vol) + "%", r4.x + 16, r4.y + 26, 90, 30, FBig, F(white));
        const Rect mute{r4.x + r4.w - 44, r4.y + 26, 28, 28};
        const bool mh = NextHitHovered();
        Fill(cr, mute.x, mute.y, mute.w, mute.h, 14, F(muted ? C(0x501b1b) : mh ? C(0x3a3a3c) : C(0x242426)));
        SpeakerGlyph(cr, mute.x + 14, mute.y + 14, 13, muted, white);
        g_hits.push_back({mute, Action::VolumeMute});
        const double slL = r4.x + 16, slR = r4.x + r4.w - 16, kx = slL + (slR - slL) * std::clamp(vol / 100.0, 0.0, 1.0);
        Fill(cr, slL, r4.y + 66, slR - slL, 4, 2, F(C(0x28282a)));
        Fill(cr, slL, r4.y + 66, kx - slL, 4, 2, F(accent));
        Dot(cr, kx, r4.y + 68, 5.5, F(white));
        g_hits.push_back({{slL, r4.y + 58, slR - slL, 20}, Action::VolumeSet});
    }
}
