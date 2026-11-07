// ── Timers page (Focus, Timer, Stopwatch, Water) ─────────────────────────────
// #included inside studio_linux.cpp's anonymous namespace. The same layout as
// Windows (studio_draw.inc's DrawStudioTimersPage): a mode bar top right, the
// ring on the left, each mode's controls on the right. State: timers_linux.cpp.

void PaintTimers(cairo_t* cr, const Rect& r) {
    namespace lt = bb::ltimers;
    namespace bt = bb::timers;
    const Col accent = Accent(), white = W(1), grey = kGrey, green = kGreen, red = kRed;
    const bool water = lt::WaterMode();
    const bt::Mode mode = lt::Mode();

    // ── The mode bar ──
    static const char* kNames[] = {"Focus", "Timer", "Stopwatch", "Water"};
    const int modes = water ? 4 : 3;
    double segW = 0;
    for (int i = 0; i < modes; ++i) segW = std::max(segW, Measure(cr, kNames[i], FLabel) + 22);
    const Rect bar{r.x + r.w - segW * modes - 4, r.y, segW * modes + 4, 26};
    Fill(cr, bar.x, bar.y, bar.w, bar.h, 13, W(0.07));
    for (int i = 0; i < modes; ++i) {
        const Rect seg{bar.x + 2 + segW * i, bar.y + 2, segW, bar.h - 4};
        const bool on = i == static_cast<int>(mode);
        const bool hover = NextHitHovered();
        if (on) Fill(cr, seg.x, seg.y, seg.w, seg.h, 11, W(0.92));
        else if (hover) Fill(cr, seg.x, seg.y, seg.w, seg.h, 11, W(0.08));
        SText(cr, kNames[i], seg.x, seg.y, seg.w, seg.h, FLabel, on ? C(0x141416) : (hover ? white : C(0xaeaeb2)), 1);
        g_hits.push_back({seg, Action::TimerMode, i});
    }
    if (mode == bt::Mode::Water) {
        PaintWater(cr, {r.x, r.y + 30, r.w, r.h - 30});
        return;
    }

    // ── The ring ──
    const double H = r.h;
    double R = std::min(64.0, (H - 14) / 2);
    double cx = r.x + R + 6, cy = r.y + H / 2;
    if (bar.x < cx + R + 8) {  // a narrow page: the ring sits below the mode bar
        const double avail = r.y + r.h - (bar.y + bar.h + 6);
        R = std::max(30.0, std::min(R, avail / 2 - 2));
        cx = r.x + R + 6;
        cy = bar.y + bar.h + 6 + avail / 2;
    }
    const double ringW = 7, x0 = cx + R + 22, y0 = r.y + 38, xr = r.x + r.w - 4;
    const lsrc::Focus& t = lsrc::FocusState();
    const double left = t.remaining;
    auto up = [](double s) { return std::ceil(s - 1e-6); };
    auto ringText = [&](const std::string& big, const std::string& small, Col smallCol) {
        SText(cr, big, cx - R, cy - 22, 2 * R, 32, big.size() > 7 ? FSong : FBig, white, 1);
        SText(cr, small, cx - R + 8, cy + 12, 2 * R - 16, 16, FSmall, smallCol, 1);
    };
    auto pill = [&](const Rect& b, const std::string& label, bool primary, Action a, int index = -1) { Pill(cr, b, label, primary, a, index); };

    if (mode == bt::Mode::Focus) {
        // ── Pomodoro ──
        const bt::PomodoroConfig cfg = lt::Config();
        const bt::Pomodoro& p = lt::Pomo();
        const bool mine = t.active && t.kind == lt::kKindPomodoro;
        const int phaseSec = bt::PhaseMinutes(p.phase, cfg) * 60;
        const bool brk = p.phase != bt::Phase::Focus;
        const Col col = brk ? green : accent;
        const double frac = mine && t.total > 0 ? std::clamp(1.0 - left / t.total, 0.0, 1.0) : 0.0;
        // Before a focus session starts the ring is a dial for its length, as on Timer.
        if (!mine && !brk && lt::AutoStartAt() <= 0) {
            const double df = bt::FocusDialFraction(cfg.focusMinutes);
            const bool hover = NextHitHovered();
            Ring(cr, cx, cy, R, ringW, df, accent);
            const double a = df * 2 * kPi;
            Dot(cr, cx + R * std::sin(a), cy - R * std::cos(a), hover ? 8 : 6.5, white);
            ringText(Clock(cfg.focusMinutes * 60), "Click or scroll", grey);
            g_hits.push_back({{cx - R - 10, cy - R - 10, 2 * R + 20, 2 * R + 20}, Action::TimerRing, 1});
            g_timerRing = {cx, cy};
        } else {
            Ring(cr, cx, cy, R, ringW, frac, col);
            ringText(Clock(mine ? up(left) : phaseSec),
                     mine ? (t.running ? (brk ? "Break" : "Focus") : "Paused") : (lt::AutoStartAt() > 0 ? "Starting\xE2\x80\xA6" : "Ready"),
                     mine && t.running ? col : grey);
        }

        // Row 1: the phase, then a dot per focus session of the cycle.
        const std::string phase = U(bt::PhaseLabel(p, cfg));
        const double phaseW = Measure(cr, phase, FSong);
        SText(cr, phase, x0, y0, xr - x0, 20, FSong, white);
        for (int i = 0; i < cfg.cycles && i < 12; ++i) {
            const double dx = x0 + phaseW + 14 + i * 13;
            if (dx + 5 > xr) break;
            const bool done = i < p.cycle - 1 || (i == p.cycle - 1 && brk), current = i == p.cycle - 1 && !brk;
            Dot(cr, dx, y0 + 10, 4, done ? accent : current ? WithA(accent, 0.45) : W(0.14));
        }
        // Row 2: what comes next; the auto-start switch on the right.
        const double ny = y0 + 24;
        const std::string autoLabel = "Auto-start";
        const double autoW = 26 + 7 + Measure(cr, autoLabel, FSmall) + 8, ax = xr - autoW;
        const bt::Phase next = bt::NextPhase(p, cfg);
        SText(cr, "Then " + U(bt::PhaseName(next)) + kDot + std::to_string(bt::PhaseMinutes(next, cfg)) + " min", x0, ny, ax - 8 - x0, 18, FSmall, grey);
        const bool autoHover = NextHitHovered();
        Fill(cr, ax, ny + 2, 26, 14, 7, cfg.autoStart ? accent : W(autoHover ? 0.24 : 0.16));
        Dot(cr, cfg.autoStart ? ax + 19 : ax + 7, ny + 9, 5, white);
        SText(cr, autoLabel, ax + 33, ny - 1, xr + 4 - ax - 33, 20, FSmall, autoHover ? white : C(0xc7c7cc));
        g_hits.push_back({{ax, ny - 1, xr - ax, 20}, Action::PomoAuto});
        // Row 3: start / pause, skip, reset; today's count if there's room.
        const double by = y0 + 52;
        const bool narrow = xr - x0 < 250;
        const double goW = narrow ? 72 : 86, skipW = narrow ? 50 : 58, resetW = narrow ? 56 : 62, sp = narrow ? 5 : 6;
        double bx = x0;
        pill({bx, by, goW, 30}, mine ? (t.running ? "Pause" : "Resume") : "Start", true, Action::PomoToggle);
        bx += goW + sp;
        if (bx + skipW <= xr) { pill({bx, by, skipW, 30}, "Skip", false, Action::PomoSkip); bx += skipW + sp; }
        if (bx + resetW <= xr) { pill({bx, by, resetW, 30}, "Reset", false, Action::PomoReset); bx += resetW + sp; }
        const std::string today = std::to_string(p.sessionsToday) + " today";
        if (bx + Measure(cr, today, FSmall) + 6 <= xr) SText(cr, today, bx, by + 6, xr - bx, 18, FSmall, grey, 2);
        // Row 4: the focus length.
        const double chipY = by + 40;
        if (chipY + 24 <= r.y + r.h) {
            double chipX = x0;
            for (int m : {15, 25, 50, 90}) {
                const std::string label = std::to_string(m) + (narrow ? "m" : " min");
                const double cw = Measure(cr, label, FLabel) + (narrow ? 18 : 22);
                if (chipX + cw > xr) break;
                const bool on = cfg.focusMinutes == m, hover = NextHitHovered();
                Fill(cr, chipX, chipY, cw, 24, 12, on ? WithA(accent, 0.35) : W(hover ? 0.16 : 0.09));
                SText(cr, label, chipX, chipY, cw, 24, FLabel, on ? white : C(0xe8e8ee), 1);
                g_hits.push_back({{chipX, chipY, cw, 24}, Action::PomoLength, m});
                chipX += cw + 6;
            }
        }
        return;
    }

    if (mode == bt::Mode::Countdown) {
        // ── Countdown ──
        const bool mine = t.active && t.kind == lt::kKindTimer;
        const int set = lt::CountSet();
        if (mine) {
            Ring(cr, cx, cy, R, ringW, t.total > 0 ? std::clamp(left / t.total, 0.0, 1.0) : 0.0, accent);
            char ends[32] = "Paused";
            if (t.running) {
                const time_t at = time(nullptr) + static_cast<time_t>(left);
                strftime(ends, sizeof ends, "Ends at %H:%M", localtime(&at));
            }
            ringText(Clock(up(left)), ends, t.running ? accent : grey);
        } else {
            // The dial: one turn an hour; click a point on it, or scroll over it.
            const double frac = bt::DialFraction(set);
            const bool hover = NextHitHovered();
            Ring(cr, cx, cy, R, ringW, frac, accent);
            const double a = frac * 2 * kPi;
            Dot(cr, cx + R * std::sin(a), cy - R * std::cos(a), hover ? 8 : 6.5, white);
            ringText(Clock(set), "Click or scroll", grey);
            g_hits.push_back({{cx - R - 10, cy - R - 10, 2 * R + 20, 2 * R + 20}, Action::TimerRing});
            g_timerRing = {cx, cy};
        }
        SText(cr, "Timer", x0, y0, xr - x0, 20, FSong, white);
        SText(cr, mine ? (t.running ? "Running on the island" : "Paused") : "Scroll for minutes" + kDot + "Shift for seconds",
              x0, y0 + 20, xr - x0, 16, FSmall, grey);
        // Presets, then + Save when the ring shows a time that isn't one yet.
        const std::vector<int> presets = lt::Presets();
        double chipX = x0;
        const double chipY = y0 + 44;
        for (int s : presets) {
            const std::string label = U(bt::PresetLabel(s));
            const double cw = Measure(cr, label, FLabel) + 20;
            if (chipX + cw > xr) break;
            const bool on = !mine && s == set, hover = NextHitHovered();
            Fill(cr, chipX, chipY, cw, 24, 12, on ? WithA(accent, 0.4) : W(hover ? 0.16 : 0.08));
            SText(cr, label, chipX, chipY, cw, 24, FLabel, on ? white : C(0xe8e8ee), 1);
            g_hits.push_back({{chipX, chipY, cw, 24}, Action::CountPreset, s});
            chipX += cw + 6;
        }
        const bool known = std::find(presets.begin(), presets.end(), set) != presets.end();
        if (!mine && !known && presets.size() < bt::kMaxPresets) {
            const double cw = Measure(cr, "+ Save", FLabel) + 20;
            if (chipX + cw <= xr) {
                const bool hover = NextHitHovered();
                RoundRect(cr, chipX + 0.5, chipY + 0.5, cw - 1, 23, 12);
                Source(cr, W(hover ? 0.4 : 0.22));
                cairo_set_line_width(cr, 1);
                cairo_stroke(cr);
                SText(cr, "+ Save", chipX, chipY, cw, 24, FLabel, white, 1);
                g_hits.push_back({{chipX, chipY, cw, 24}, Action::CountSave});
            }
        }
        const double by = y0 + 78;
        double bx = x0;
        pill({bx, by, 86, 30}, mine ? (t.running ? "Pause" : "Resume") : "Start", true, Action::CountToggle);
        bx += 92;
        for (int m : {1, 5}) {
            const std::string label = "+" + std::to_string(m) + " min";
            const double w = Measure(cr, label, FLabel) + 28;
            if (bx + w > xr) break;
            pill({bx, by, w, 30}, label, false, Action::CountAdd, m);
            bx += w + 6;
        }
        if (mine && bx + 62 <= xr) pill({bx, by, 62, 30}, "Reset", false, Action::CountReset);
        return;
    }

    // ── Stopwatch ──
    const lt::Stopwatch& sw = lt::StopwatchState();
    const double elapsed = lt::StopwatchElapsed(Now());
    Ring(cr, cx, cy, R, ringW, std::fmod(elapsed, 60.0) / 60.0, accent);
    ringText(U(bt::StopwatchText(elapsed)), sw.running ? "Running" : sw.active ? "Paused" : "Ready", sw.running ? accent : grey);
    double bx = x0;
    const double by = y0;
    pill({bx, by, 86, 30}, sw.running ? "Pause" : sw.active ? "Resume" : "Start", true, Action::SwToggle);
    bx += 92;
    if (sw.running) { pill({bx, by, 58, 30}, "Lap", false, Action::SwLap); bx += 64; }
    else if (sw.active) { pill({bx, by, 62, 30}, "Reset", false, Action::SwReset); bx += 68; }
    const std::vector<bt::Lap>& laps = lt::Laps();
    if (!laps.empty() && bx + 62 <= xr) pill({bx, by, 62, 30}, "Copy", false, Action::SwCopy);
    // The laps, newest first; the fastest in green, the slowest in red.
    const double ly = by + 40;
    if (laps.empty()) {
        SText(cr, sw.running ? "Lap marks a split" : "Laps show here", x0, ly, xr - x0, 18, FSmall, grey);
        return;
    }
    const bt::LapMarks marks = bt::MarkLaps(laps);
    const int rows = std::max(1, static_cast<int>((r.y + r.h - ly) / 17));
    const int n = static_cast<int>(laps.size());
    for (int k = 0; k < rows && k < n; ++k) {
        const int i = n - 1 - k;
        const double y = ly + k * 17;
        const Col col = i == marks.fastest ? green : i == marks.slowest ? red : C(0xe8e8ee);
        SText(cr, "Lap " + std::to_string(i + 1), x0, y, 60, 17, FBody, col);
        SText(cr, U(bt::StopwatchText(laps[static_cast<size_t>(i)].split)), x0 + 60, y, xr - 90 - x0 - 60, 17, FBody, col, 2);
        SText(cr, U(bt::StopwatchText(laps[static_cast<size_t>(i)].total)), xr - 90, y, 90, 17, FSmall, grey, 2);
    }
}
