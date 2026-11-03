// Bunbun — Studio layout drawing. #included inside the Renderer class.
//
// The Studio frame wraps the dashboard (idle and media views) once the island is
// open: a header with the page name, a tiny mascot, the date and time, and a
// bottom tab bar with one icon per page and a volume slider. The page itself is
// drawn by its usual function into the rect between the two, so every existing
// page and its hit tests keep working. Also here: the Home tiles, the Agent,
// Clipboard and Note pages (shared by both layouts) and the tuck-away line.
// State and click handling live in studio_module.inc.

    // ── Metrics (content units; DrawPill applies sizeScale) ──
    static constexpr float kStudioIslandHeader = 34.0f;
    static constexpr float kStudioFooter = 38.0f;
    static constexpr float kStudioSide = 24.0f;
    static constexpr float kStudioPageH = 184.0f;  // the dashboard pages' own height

    // ── Fonts ──
    enum StudioFontId { SFTitle, SFLabel, SFBody, SFBig, SFHero, SFMono, SFSmall, SFSong, SFAnswer, SFAsk, SFInput, SFCount };
    ComPtr<IDWriteTextFormat> studioFonts_[SFCount];
    std::wstring studioFontFamily_ = L"\x01";  // never a real family, so the first call builds them

    IDWriteTextFormat* SFont(StudioFontId id) {
        if (studioFontFamily_ != lastFontFamily_) {
            studioFontFamily_ = lastFontFamily_;
            const wchar_t* family = lastFontFamily_.empty() ? L"Segoe UI Variable Display" : lastFontFamily_.c_str();
            struct Spec { const wchar_t* family; DWRITE_FONT_WEIGHT weight; float size; };
            const Spec specs[SFCount] = {
                {family, DWRITE_FONT_WEIGHT_BOLD, 13.0f},       // SFTitle
                {family, DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.0f},  // SFLabel
                {family, DWRITE_FONT_WEIGHT_MEDIUM, 12.0f},     // SFBody
                {family, DWRITE_FONT_WEIGHT_BOLD, 26.0f},       // SFBig
                {family, DWRITE_FONT_WEIGHT_BOLD, 40.0f},       // SFHero
                {L"Cascadia Mono", DWRITE_FONT_WEIGHT_NORMAL, 11.0f},  // SFMono
                {family, DWRITE_FONT_WEIGHT_MEDIUM, 10.0f},     // SFSmall
                {family, DWRITE_FONT_WEIGHT_BOLD, 15.0f},       // SFSong
                {family, DWRITE_FONT_WEIGHT_NORMAL, 16.0f},     // SFAnswer (wraps)
                {family, DWRITE_FONT_WEIGHT_MEDIUM, 13.0f},     // SFAsk (wraps)
                {family, DWRITE_FONT_WEIGHT_NORMAL, 14.0f},     // SFInput
            };
            for (int i = 0; i < SFCount; ++i) {
                studioFonts_[i] = nullptr;
                HRESULT hr = dwriteFactory_->CreateTextFormat(specs[i].family, nullptr, specs[i].weight,
                                                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                              specs[i].size, L"", &studioFonts_[i]);
                if (FAILED(hr) || !studioFonts_[i]) {
                    dwriteFactory_->CreateTextFormat(i == SFMono ? L"Consolas" : L"Segoe UI", nullptr, specs[i].weight,
                                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     specs[i].size, L"", &studioFonts_[i]);
                }
                if (!studioFonts_[i]) continue;
                const bool wraps = i == SFAnswer || i == SFAsk;
                studioFonts_[i]->SetWordWrapping(wraps ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
                studioFonts_[i]->SetParagraphAlignment(wraps ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                if (wraps) continue;  // no ellipsis: wrapped text is measured and shown whole
                DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                ComPtr<IDWriteInlineObject> ellipsis;
                dwriteFactory_->CreateEllipsisTrimmingSign(studioFonts_[i].Get(), &ellipsis);
                studioFonts_[i]->SetTrimming(&trim, ellipsis.Get());
            }
        }
        return studioFonts_[id].Get();
    }

    // align: 0 left, 1 centre, 2 right. Text is vertically centred in r.
    void SText(const std::wstring& text, D2D1_RECT_F r, StudioFontId font, D2D1_COLOR_F color, int align = 0) {
        IDWriteTextFormat* f = SFont(font);
        if (!f || text.empty() || r.right <= r.left) return;
        f->SetTextAlignment(align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER
                            : align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        ID2D1SolidColorBrush* b = AcquireBrush(color);
        if (!b) return;
        target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), f, r, b,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }

    float SMeasure(const std::wstring& text, StudioFontId font) {
        IDWriteTextFormat* f = SFont(font);
        if (!f || text.empty()) return 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), f, 2000.0f,
                                                    100.0f, &layout)) || !layout)
            return 0.0f;
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        return m.widthIncludingTrailingWhitespace;
    }

    // ── Small drawing helpers ──
    float studioAlpha_ = 1.0f;          // the frame's fade while the island opens
    D2D1_POINT_2F studioOrigin_{};      // published content rect's top-left, for hit rects

    static D2D1_COLOR_F SCol(uint32_t rgb, float a = 1.0f) { return D2D1::ColorF(rgb, a); }
    D2D1_COLOR_F SFade(D2D1_COLOR_F c) const { c.a *= studioAlpha_; return c; }

    // Records a clickable spot in window (client) coordinates, through the transform
    // in force right now: the island's size scale, a page sliding in, a nudge. The
    // pointer is matched against these directly, so nothing else drawn later in the
    // frame (an agent pill publishing its own content rect) can shift them.
    void SHit(D2D1_RECT_F r, StudioAction action, int arg = 0) {
        D2D1_MATRIX_3X2_F m;
        target_->GetTransform(&m);
        // The live transform maps into the region bound this frame, not the window;
        // bindOffset_ turns that back into window (client) coordinates.
        auto map = [&](float x, float y) {
            return D2D1::Point2F(x * m._11 + y * m._21 + m._31 + bindOffset_.x,
                                 x * m._12 + y * m._22 + m._32 + bindOffset_.y);
        };
        const D2D1_POINT_2F a = map(r.left, r.top), b = map(r.right, r.bottom);
        g_studioHits.push_back({D2D1::RectF(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)),
                                action, arg});
        g_studioHitsFrame = GetTickCount64();
    }

    // Hover as an eased 0..1 per button (about 80 ms each way), so highlights fade
    // in and out instead of switching. Keeps frames coming while one is moving.
    struct HoverAnim { float value = 0.0f; double at = 0.0; };
    std::map<std::pair<int, int>, HoverAnim> hoverAnim_;

    float SHover(StudioAction action, int arg = INT_MIN) {
        HoverAnim& h = hoverAnim_[{static_cast<int>(action), arg}];
        const double now = NowSeconds();
        const float dt = h.at > 0.0 ? static_cast<float>(std::min(0.1, now - h.at)) : 0.1f;
        h.at = now;
        const float target = SHovered(action, arg) ? 1.0f : 0.0f;
        h.value += (target - h.value) * (1.0f - std::exp(-dt / 0.08f));
        if (std::fabs(target - h.value) < 0.01f) h.value = target;
        else g_layoutDirty = true;
        return h.value;
    }

    static float SLerp(float a, float b, float t) { return a + (b - a) * t; }
    static D2D1_COLOR_F SLerpCol(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
        return D2D1::ColorF(SLerp(a.r, b.r, t), SLerp(a.g, b.g, t), SLerp(a.b, b.b, t), SLerp(a.a, b.a, t));
    }

    bool SHovered(StudioAction action, int arg = INT_MIN) const {
        return g_studioHover.on && g_studioHover.action == action && (arg == INT_MIN || g_studioHover.arg == arg);
    }

    void SFillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
        if (ID2D1SolidColorBrush* b = AcquireBrush(c))
            target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
    }

    void SLine(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float w) {
        if (ID2D1SolidColorBrush* br = AcquireBrush(c)) target_->DrawLine(a, b, br, w, studioStroke_.Get());
    }

    ComPtr<ID2D1StrokeStyle> studioStroke_;
    void EnsureRoundStroke() {
        if (studioStroke_) return;
        D2D1_STROKE_STYLE_PROPERTIES p = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        d2dFactory_->CreateStrokeStyle(p, nullptr, 0, &studioStroke_);
    }

    // The icons and rings are shared with the Linux island (src/island/studio_icons.h).
    bb::studio::Pen SPen() {
        EnsureRoundStroke();
        return {target_.Get(), d2dFactory_.Get(), studioStroke_.Get(),
                [this](const D2D1_COLOR_F& c) { return AcquireBrush(c); }};
    }

    // Open or closed polyline, in a 24-unit icon grid placed at `c` with `scale`.
    void SPoly(std::initializer_list<D2D1_POINT_2F> pts, D2D1_POINT_2F c, float scale, D2D1_COLOR_F col,
               float width, bool closed = false, bool fill = false) {
        bb::studio::Poly(SPen(), pts, c, scale, col, width, closed, fill);
    }

    // An arc around (cx, cy) from angle a0 to a1 (radians, 0 = 3 o'clock, clockwise).
    void SArc(D2D1_POINT_2F c, float r, float a0, float a1, D2D1_COLOR_F col, float width) {
        bb::studio::Arc(SPen(), c, r, a0, a1, col, width);
    }

    // Progress ring from 12 o'clock: track plus a coloured arc.
    void SRing(D2D1_POINT_2F c, float r, float width, float fraction, D2D1_COLOR_F col) {
        bb::studio::Ring(SPen(), c, r, width, fraction, col, studioAlpha_);
    }

    void SIcon(StudioPage page, D2D1_POINT_2F c, float size, D2D1_COLOR_F col) { bb::studio::Icon(SPen(), page, c, size, col); }
    void SGear(D2D1_POINT_2F c, float size, D2D1_COLOR_F col) { bb::studio::Gear(SPen(), c, size, col); }
    void SEyedropper(D2D1_POINT_2F c, float size, D2D1_COLOR_F col) { bb::studio::Eyedropper(SPen(), c, size, col); }
    void SSpeaker(D2D1_POINT_2F c, float size, int level, bool muted, D2D1_COLOR_F col) {
        bb::studio::Speaker(SPen(), c, size, level, muted, col);
    }

    // ── Page names and colours ──
    static void StudioName(StudioPage page, const bb::AgentView& agent, std::wstring* first, std::wstring* second) {
        bb::studio::PageName(page, agent.agent, first, second);
    }

    static const wchar_t* StudioTabLabel(StudioPage page) { return bb::studio::TabLabel(page); }

    // Each coding agent keeps its own brand colour; everything else on the island
    // follows the dynamic accent (SAccent).
    static D2D1_COLOR_F AgentBrand(const std::wstring& agent) { return SCol(bb::studio::AgentBrand(agent)); }

    // The island's live accent: album art in Auto mode, else the system or custom
    // accent (Themes.AccentColorMode), already eased between changes.
    D2D1_COLOR_F SAccent() const {
        D2D1_COLOR_F c = currentAccent_;
        if (c.a <= 0.0f) c = SCol(0xff375f);
        c.a = 1.0f;
        return c;
    }

    static D2D1_COLOR_F AgentStatusColor(bb::AgentPhase p) { return SCol(bb::studio::AgentStatus(p)); }

    D2D1_COLOR_F StudioPageColor(StudioPage page, const SharedState& state) const {
        if (page == StudioPage::Agent) return AgentBrand(state.agent.agent);
        return SAccent();
    }

    static std::wstring Clock(double seconds) {
        const int s = std::max(0, static_cast<int>(seconds));
        wchar_t b[32];
        if (s >= 3600) swprintf_s(b, L"%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
        else swprintf_s(b, L"%d:%02d", s / 60, s % 60);
        return b;
    }

    static const wchar_t* StageWord(int stage) {
        switch (stage) {
            case 0: return L"Reading";
            case 1: return L"Editing";
            case 2: return L"Testing";
            case 3: return L"Done";
            default: return L"Thinking";
        }
    }

    // Whose usage the header ring and the Agent tile show: the main agent's, when it
    // has numbers (Claude Code's statusLine, Antigravity's cached quota) and is
    // switched on. On Auto, whichever of the two has numbers. False = no ring.
    bool PickUsage(const SharedState& state, const Settings& settings, double now, float* usedPct, const wchar_t** tag) {
        const int main = MainAgentKind(settings);
        const bool claude = settings.usageLimits && settings.showAgent[AgentClaude] && state.agent.usage5h >= 0.0f &&
                            now - state.agent.usageAt < 5 * 3600.0;
        const AgQuota::Snapshot ag = settings.antigravityQuota && settings.showAgent[AgentAntigravity]
                                         ? AgQuota::Get() : AgQuota::Snapshot{};
        const bool auto_ = settings.mainAgent < 0;
        if (claude && (main == AgentClaude || (auto_ && !(main == AgentAntigravity && ag.valid)))) {
            *usedPct = state.agent.usage5h;
            *tag = L"5h";
            return true;
        }
        if (ag.valid && (main == AgentAntigravity || auto_)) {
            *usedPct = (1.0f - ag.remaining) * 100.0f;
            *tag = L"AG";
            return true;
        }
        return false;
    }

    // ── Frame ──
    struct StudioFrame {
        bool on = false;
        float mix = 0.0f;        // 0 collapsed .. 1 fully open
        D2D1_RECT_F island{};    // the whole island (content units)
        D2D1_RECT_F page{};      // where the page is drawn
        int context = 1;
        std::vector<int> tabs;
        int tabIdx = 0;
        StudioPage page_ = StudioPage::Home;
    };
    StudioFrame studio_;
    std::vector<float> studioTabW_;  // eased tab widths, one per tab

    // Called by DrawPill before the page is drawn. Returns the rect the page gets:
    // the island itself while collapsed, the space between header and tab bar when open.
    D2D1_RECT_F BeginStudioFrame(const SharedState& state, const Settings& settings, IslandKind kind,
                                 D2D1_RECT_F island, double now) {
        studio_ = StudioFrame{};
        if (!settings.studioLayout || (kind != IslandKind::Media && kind != IslandKind::Idle) ||
            GameOverlayShown(settings.gameOverlay)) {
            return island;
        }
        const float h = island.bottom - island.top;
        const float mix = SmoothStep01(Clamp((h - 120.0f) / (kStudioIslandH - 120.0f), 0.0f, 1.0f));
        if (mix <= 0.001f) return island;

        studio_.on = true;
        studio_.mix = mix;
        studio_.island = island;
        studio_.context = kind == IslandKind::Media ? 0 : 1;
        studio_.tabs = DashboardTabIds(studio_.context, settings);
        studio_.tabIdx = studio_.tabs.empty() ? 0
            : std::min(NormalizedTabIndex(settings), static_cast<int>(studio_.tabs.size()) - 1);
        studio_.page_ = studio_.tabs.empty() ? StudioPage::Home
            : StudioPageFor(studio_.context, studio_.tabs[static_cast<size_t>(studio_.tabIdx)]);
        studio_.page = D2D1::RectF(island.left + kStudioSide * mix, island.top + kStudioIslandHeader * mix,
                                   island.right - kStudioSide * mix, island.bottom - kStudioFooter * mix);

        // Music pages glow from the bottom in the album's colour; Home and the
        // Agent page in the agent's status colour while it works.
        D2D1_COLOR_F tint{};
        float strength = 0.0f;
        if (studio_.page_ == StudioPage::Music || studio_.page_ == StudioPage::Lyrics) {
            tint = StudioPageColor(studio_.page_, state);
            strength = state.media.playing ? 0.55f : 0.30f;
        } else if (studio_.page_ == StudioPage::Agent && state.agent.phase != bb::AgentPhase::None) {
            tint = AgentStatusColor(state.agent.phase);
            strength = 0.35f;
        }
        if (strength > 0.0f) {
            const float radius = ContentIslandRadius(h);
            ComPtr<ID2D1Geometry> mask = CreateIslandMaskGeometry(island, radius, settings.notchStyle);
            D2D1_GRADIENT_STOP stops[3] = {
                {0.0f, WithAlpha(tint, 0.0f)},
                {0.55f, WithAlpha(tint, 0.10f * strength * mix)},
                {1.0f, WithAlpha(tint, 0.62f * strength * mix)},
            };
            ComPtr<ID2D1GradientStopCollection> coll;
            ComPtr<ID2D1LinearGradientBrush> grad;
            if (mask && SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, &coll)) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, island.top + h * 0.2f),
                                                        D2D1::Point2F(0, island.bottom)),
                    coll.Get(), &grad))) {
                target_->FillGeometry(mask.Get(), grad.Get());
            }
        }
        (void)now;
        return studio_.page;
    }

    // Called by DrawPill after the page: header and tab bar over the frame.
    void EndStudioFrame(const SharedState& state, const Settings& settings, double now) {
        if (!studio_.on) return;
        EnsureRoundStroke();
        studioAlpha_ = studio_.mix * studio_.mix;
        studioOrigin_ = publishedOrigin_;
        const D2D1_RECT_F isl = studio_.island;
        const float cx = (isl.left + isl.right) * 0.5f;

        // Header ─ page name (two-tone), mini mascot + date + time, gear.
        const float hy = isl.top + 17.0f;
        std::wstring first, second;
        StudioName(studio_.page_, state.agent, &first, &second);
        const D2D1_COLOR_F pageCol = StudioPageColor(studio_.page_, state);
        if (studio_.page_ != StudioPage::Home) {
            const float fx = isl.left + 22.0f;
            const float w1 = SMeasure(first, SFTitle);
            SText(first, D2D1::RectF(fx, hy - 10, fx + w1 + 2, hy + 10), SFTitle, SFade(SCol(0xffffff)));
            SText(second, D2D1::RectF(fx + w1, hy - 10, fx + w1 + 150, hy + 10), SFTitle,
                  SFade(studio_.page_ == StudioPage::Home ? SCol(0xffffff) : pageCol));
        }

        SYSTEMTIME t{};
        GetLocalTime(&t);
        static const wchar_t* kDays[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
        static const wchar_t* kMonths[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                           L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
        wchar_t date[32];
        swprintf_s(date, L"%s %d %s", kDays[t.wDayOfWeek % 7], t.wDay, kMonths[(t.wMonth + 11) % 12]);
        const std::wstring timeStr = FormatIslandTime(t, settings.clockFollowSystem, settings.use24HourClock, false);
        const float dateW = SMeasure(date, SFBody), timeW = SMeasure(timeStr, SFTitle);
        const float mascotW = settings.bunbunMascot ? 22.0f : 0.0f;
        const float groupW = mascotW + dateW + 6.0f + timeW;
        float gx = cx - groupW * 0.5f;
        if (settings.bunbunMascot && studioAlpha_ > 0.3f && settings.idleEyes) {
            const auto mood = state.agent.phase != bb::AgentPhase::None ? AgentMood(state.agent.phase)
                              : state.media.playing ? bb::mascot::Mood::Music : bb::mascot::Mood::Idle;
            DrawIdleEyes(state, D2D1::Point2F(gx + 9, hy), 14.0f, mood, now, state.agent.phaseSince);
        } else if (settings.bunbunMascot && studioAlpha_ > 0.3f) {
            D2D1_POINT_2F mc{};
            float msize = 0.0f;
            bb::mascot::FitToBox(D2D1::RectF(gx, hy - 9, gx + 18, hy + 9), &mc, &msize);
            bb::AgentPhase phase = state.agent.phase;
            const auto mood = phase != bb::AgentPhase::None ? AgentMood(phase)
                              : state.media.playing ? bb::mascot::Mood::Music : bb::mascot::Mood::Idle;
            bb::mascot::Draw(target_.Get(), mc, msize, mood, now, state.agent.phaseSince, MascotLook(mc));
        }
        gx += mascotW;
        SText(date, D2D1::RectF(gx, hy - 9, gx + dateW + 2, hy + 9), SFBody, SFade(SCol(0x8e8e93)));
        gx += dateW + 6.0f;
        SText(timeStr, D2D1::RectF(gx, hy - 10, gx + timeW + 2, hy + 10), SFTitle, SFade(SCol(0xffffff)));

        // Usage ring (Claude Code's 5-hour window) and the settings gear on the right.
        const D2D1_POINT_2F gear = D2D1::Point2F(isl.right - 26.0f, hy);
        const float gearHover = SHover(StudioAction::Settings);
        if (gearHover > 0.01f)
            SFillRound(D2D1::RectF(gear.x - 13, gear.y - 13, gear.x + 13, gear.y + 13), 13, SFade(WithAlpha(SCol(0x3a3a3c), gearHover)));
        SGear(gear, 15.0f * (1.0f + 0.1f * gearHover), SFade(SLerpCol(SCol(0x8e8e93), SCol(0xffffff), gearHover)));
        SHit(D2D1::RectF(gear.x - 13, gear.y - 13, gear.x + 13, gear.y + 13), StudioAction::Settings);

        // Eyedropper: pick a colour anywhere on screen.
        const D2D1_POINT_2F drop = D2D1::Point2F(gear.x - 30.0f, hy);
        const float dropHover = SHover(StudioAction::ColorPicker);
        if (dropHover > 0.01f)
            SFillRound(D2D1::RectF(drop.x - 13, drop.y - 13, drop.x + 13, drop.y + 13), 13, SFade(WithAlpha(SCol(0x3a3a3c), dropHover)));
        SEyedropper(drop, 15.0f * (1.0f + 0.1f * dropHover), SFade(SLerpCol(SCol(0x8e8e93), SCol(0xffffff), dropHover)));
        SHit(D2D1::RectF(drop.x - 13, drop.y - 13, drop.x + 13, drop.y + 13), StudioAction::ColorPicker);
        // Claude Code's 5-hour limit, from its statusLine; hidden once the reading is
        // older than the window it describes.
        // The main agent's usage (see PickUsage); none while no agent is shown.
        float used = 0.0f;
        const wchar_t* tag = L"";
        if (AgentsShown(settings) && PickUsage(state, settings, now, &used, &tag)) {
            const float u = used / 100.0f;
            const D2D1_COLOR_F uc = u >= 0.85f ? SCol(0xff3b3b) : u >= 0.6f ? SCol(0xff9a1f) : SCol(0x22e05a);
            wchar_t pct[16];
            swprintf_s(pct, L"%s %d%%", tag, static_cast<int>(used + 0.5f));
            const float pw = SMeasure(pct, SFLabel);
            const D2D1_POINT_2F rc = D2D1::Point2F(gear.x - 56.0f - pw, hy);
            SRing(rc, 6.0f, 2.4f, u, uc);
            SText(pct, D2D1::RectF(rc.x + 10, hy - 8, rc.x + 12 + pw, hy + 8), SFLabel, SFade(SCol(0x8e8e93)));
            SHit(D2D1::RectF(rc.x - 9, hy - 10, rc.x + 12 + pw, hy + 10), StudioAction::AgentPage);
        }

        // Tab bar ─ one icon per page; the open one widens into a tinted capsule.
        const float fy = isl.bottom - 19.0f;
        const int n = static_cast<int>(studio_.tabs.size());
        if (static_cast<int>(studioTabW_.size()) != n) studioTabW_.assign(static_cast<size_t>(n), 30.0f);
        std::vector<std::wstring> labels(static_cast<size_t>(n));
        std::vector<StudioPage> pages(static_cast<size_t>(n));
        float total = 0.0f;
        bool easing = false;
        const float maxBar = (isl.right - isl.left) - 2 * 64.0f;
        for (int i = 0; i < n; ++i) {
            pages[i] = StudioPageFor(studio_.context, studio_.tabs[static_cast<size_t>(i)]);
            labels[i] = StudioTabLabel(pages[i]);
            const float want = i == studio_.tabIdx ? 30.0f + 10.0f + SMeasure(labels[i], SFLabel) : 30.0f;
            float& cur = studioTabW_[static_cast<size_t>(i)];
            cur += (want - cur) * 0.25f;
            if (std::fabs(want - cur) < 0.3f) cur = want; else easing = true;
            total += cur + (i ? 2.0f : 0.0f);
        }
        if (easing) g_layoutDirty = true;
        // Too many tabs for the bar: the inactive ones shrink (down to 20), the open
        // one keeps its full width so its name always shows.
        float inactiveScale = 1.0f;
        if (total > maxBar && n > 1) {
            float activeW = studioTabW_[static_cast<size_t>(std::clamp(studio_.tabIdx, 0, n - 1))];
            float others = total - activeW;
            inactiveScale = std::max(20.0f / 30.0f, (maxBar - activeW) / std::max(1.0f, others));
            total = activeW + others * inactiveScale;
        }
        const bool squeeze = false;
        float tx = cx - total * 0.5f;
        for (int i = 0; i < n; ++i) {
            float w = studioTabW_[static_cast<size_t>(i)];
            if (i != studio_.tabIdx) w *= inactiveScale;
            const bool active = i == studio_.tabIdx;
            const D2D1_COLOR_F tint = StudioPageColor(pages[i], state);
            const float hoverAmt = SHover(StudioAction::Tab, i);
            if (active) {
                const bool white = pages[i] == StudioPage::Home || pages[i] == StudioPage::Hardware;
                SFillRound(D2D1::RectF(tx, fy - 13, tx + w, fy + 13), 13, SFade(white ? SCol(0x2c2c2e) : WithAlpha(tint, 0.22f)));
                const D2D1_COLOR_F ink = SFade(white ? SCol(0xffffff) : tint);
                SIcon(pages[i], D2D1::Point2F(tx + 15, fy), 15.0f, ink);
                if (w > 44.0f && !squeeze) SText(labels[i], D2D1::RectF(tx + 27, fy - 9, tx + w - 4, fy + 9), SFLabel, ink);
            } else {
                if (hoverAmt > 0.01f) {
                    if (ID2D1SolidColorBrush* b = AcquireBrush(SFade(WithAlpha(SCol(0x3a3a3c), hoverAmt))))
                        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx + w * 0.5f, fy), 13 * hoverAmt + 6 * (1 - hoverAmt), 13), b);
                }
                SIcon(pages[i], D2D1::Point2F(tx + w * 0.5f, fy - 1.5f * hoverAmt), 15.0f * (1.0f + 0.12f * hoverAmt),
                      SFade(SLerpCol(SCol(0x8e8e93), SCol(0xffffff), hoverAmt)));
            }
            SHit(D2D1::RectF(tx, fy - 15, tx + w, fy + 15), StudioAction::Tab, i);
            tx += w + 2.0f;
        }

        // Volume ─ the speaker widens into a slider while the pointer is on it.
        // While dragging, show the level just set; the system reading follows a beat later.
        const int vol = g_studioVolumeDrag && g_studioVolumeLevelPending >= 0.0f
                            ? static_cast<int>(g_studioVolumeLevelPending * 100.0f + 0.5f)
                            : state.system.volumePercent;
        const bool muted = state.system.volumeMuted;
        const bool volHover = SHovered(StudioAction::Volume) || g_studioVolumeDrag;
        studioVolW_ += ((volHover ? 76.0f : 0.0f) - studioVolW_) * 0.25f;
        if (std::fabs(studioVolW_ - (volHover ? 76.0f : 0.0f)) > 0.3f) g_layoutDirty = true;
        const D2D1_POINT_2F spk = D2D1::Point2F(isl.right - 25.0f - studioVolW_, fy);
        SSpeaker(spk, 16.0f, muted ? 0 : vol == 0 ? 0 : vol < 50 ? 1 : 2, muted, SFade(SCol(volHover ? 0xffffff : 0x8e8e93)));
        SHit(D2D1::RectF(spk.x - 12, fy - 14, isl.right - 14, fy + 14), StudioAction::Volume, -1);
        if (studioVolW_ > 4.0f) {
            const float x0 = spk.x + 14.0f, x1 = x0 + studioVolW_ - 8.0f;
            if (x1 > x0 + 4) {
                SFillRound(D2D1::RectF(x0, fy - 2, x1, fy + 2), 2, SFade(D2D1::ColorF(1, 1, 1, 0.2f)));
                const float kx = x0 + (x1 - x0) * Clamp(vol / 100.0f, 0.0f, 1.0f);
                SFillRound(D2D1::RectF(x0, fy - 2, kx, fy + 2), 2, SFade(SCol(0xffffff)));
                if (ID2D1SolidColorBrush* b = AcquireBrush(SFade(SCol(0xffffff))))
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, fy), 5, 5), b);
                // The slider's own rect: a click sets the level from its x.
                SHit(D2D1::RectF(x0, fy - 10, x1, fy + 10), StudioAction::Volume, 0);
            }
        }
        DrawTrayDropOverlay(now);
        studioAlpha_ = 1.0f;
    }
    float studioVolW_ = 0.0f;

    // A file dragged over the open island: a drop zone with a marching dashed
    // outline. Just after a drop: a card that fills up and ticks "In your tray".
    void DrawTrayDropOverlay(double now) {
        const D2D1_RECT_F pg = studio_.page;
        const D2D1_COLOR_F yellow = SAccent();
        if (g_trayDragOver.load(std::memory_order_relaxed)) {
            SFillRound(pg, 22, SFade(D2D1::ColorF(0.04f, 0.04f, 0.05f, 0.92f)));
            const D2D1_RECT_F zone = D2D1::RectF(pg.left + 8, pg.top + 6, pg.right - 8, pg.bottom - 6);
            const float dashes[] = {5.0f, 4.0f};
            ComPtr<ID2D1StrokeStyle> dashed;
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.0f,
                D2D1_DASH_STYLE_CUSTOM, -static_cast<float>(std::fmod(now * 18.0, 9.0)));  // marching
            d2dFactory_->CreateStrokeStyle(props, dashes, 2, &dashed);
            if (ID2D1SolidColorBrush* b = AcquireBrush(SFade(yellow)))
                target_->DrawRoundedRectangle(D2D1::RoundedRect(zone, 20, 20), b, 2.0f, dashed.Get());
            const float cx = (pg.left + pg.right) * 0.5f, cy = (pg.top + pg.bottom) * 0.5f;
            SIcon(StudioPage::Tray, D2D1::Point2F(cx, cy - 22), 30.0f, SFade(yellow));
            SText(L"Let go, I'll catch it!", D2D1::RectF(pg.left, cy + 2, pg.right, cy + 28), SFSong, SFade(SCol(0xffffff)), 1);
            g_layoutDirty = true;  // keep the outline marching
            return;
        }
        const double since = now - g_trayDropAt.load();
        if (since < 0.0 || since > 1.6) return;
        g_layoutDirty = true;
        // Pop in, fill the bar, tick, fade out.
        const float in = SmoothStep01(Clamp(static_cast<float>(since / 0.25), 0.0f, 1.0f));
        const float out = 1.0f - SmoothStep01(Clamp(static_cast<float>((since - 1.3) / 0.3), 0.0f, 1.0f));
        const float a = in * out;
        const float cx = (pg.left + pg.right) * 0.5f, cy = (pg.top + pg.bottom) * 0.5f;
        const float w = 300.0f * (0.9f + 0.1f * in), h = 92.0f;
        const D2D1_RECT_F card = D2D1::RectF(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
        SFillRound(card, 20, D2D1::ColorF(0.11f, 0.11f, 0.12f, 0.97f * a));
        SIcon(StudioPage::Tray, D2D1::Point2F(card.left + 30, cy - 10), 24.0f, WithAlpha(yellow, a));
        std::wstring name = g_trayDropName;
        if (g_trayDropCount > 1) name += L" + " + std::to_wstring(g_trayDropCount - 1) + L" more";
        SText(name, D2D1::RectF(card.left + 56, card.top + 14, card.right - 16, card.top + 34), SFTitle, D2D1::ColorF(1, 1, 1, a));
        const float fill = SmoothStep01(Clamp(static_cast<float>((since - 0.3) / 0.65), 0.0f, 1.0f));
        const D2D1_RECT_F bar = D2D1::RectF(card.left + 56, card.top + 44, card.right - 20, card.top + 50);
        SFillRound(bar, 3, D2D1::ColorF(1, 1, 1, 0.12f * a));
        SFillRound(D2D1::RectF(bar.left, bar.top, bar.left + (bar.right - bar.left) * fill, bar.bottom), 3, WithAlpha(yellow, a));
        if (since > 0.95) {
            const float tick = SmoothStep01(Clamp(static_cast<float>((since - 0.95) / 0.2), 0.0f, 1.0f));
            if (ID2D1SolidColorBrush* b = AcquireBrush(WithAlpha(SCol(0x30d158), a * tick)))
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(card.left + 66, card.top + 70), 8, 8), b);
            auto P = [](float x, float y) { return D2D1::Point2F(x, y); };
            SPoly({P(8.5f, 12), P(11, 14.5f), P(15.5f, 9.5f)}, D2D1::Point2F(card.left + 66, card.top + 70), 0.9f,
                  D2D1::ColorF(0, 0, 0, a * tick), 1.8f);
            SText(L"In your tray", D2D1::RectF(card.left + 80, card.top + 60, card.right - 16, card.top + 80), SFBody,
                  WithAlpha(SCol(0x30d158), a * tick));
        }
    }
    D2D1_POINT_2F publishedOrigin_{};

    // ── Home tiles ──
    void DrawStudioTile(D2D1_RECT_F r, D2D1_COLOR_F glow, bool live, double now) {
        const float w = r.right - r.left, h = r.bottom - r.top;
        SFillRound(r, 22, D2D1::ColorF(1, 1, 1, 0.055f));
        float a = live ? 0.82f + 0.30f * (0.5f + 0.5f * std::sin(static_cast<float>(now) * 6.2831853f / 2.6f)) : 0.55f;
        D2D1_GRADIENT_STOP stops[3] = {
            {0.0f, WithAlpha(glow, 0.0f)},
            {0.55f, WithAlpha(glow, 0.22f * a)},
            {1.0f, WithAlpha(glow, 0.62f * a)},
        };
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1LinearGradientBrush> grad;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, &coll)) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, r.top + h * 0.18f), D2D1::Point2F(0, r.bottom)),
                coll.Get(), &grad))) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(r, 22, 22), grad.Get());
        }
        if (ID2D1SolidColorBrush* rim = AcquireBrush(D2D1::ColorF(1, 1, 1, 0.10f)))
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 21.5f, 21.5f), rim, 1.0f);
        (void)w;
    }

    // A round white button with a glyph (0 play, 1 pause, 2 stop) in `ink`.
    void SRoundButton(D2D1_POINT_2F c, float r, int glyph, D2D1_COLOR_F ink, float hover) {
        r *= 1.0f + 0.08f * hover;  // lifts a little under the pointer
        if (hover > 0.01f) {
            if (ID2D1SolidColorBrush* g = AcquireBrush(D2D1::ColorF(1, 1, 1, 0.18f * hover)))
                target_->FillEllipse(D2D1::Ellipse(c, r + 4.0f, r + 4.0f), g);
        }
        if (ID2D1SolidColorBrush* b = AcquireBrush(D2D1::ColorF(1, 1, 1, SLerp(0.92f, 1.0f, hover))))
            target_->FillEllipse(D2D1::Ellipse(c, r, r), b);
        const float g = r * 0.42f;
        auto P = [](float x, float y) { return D2D1::Point2F(x, y); };
        if (glyph == 0) {
            SPoly({P(12 - g * 0.55f, 12 - g * 0.75f), P(12 + g * 0.8f, 12), P(12 - g * 0.55f, 12 + g * 0.75f)}, c, 1.0f, ink, 0, true, true);
        } else if (glyph == 1) {
            SFillRound(D2D1::RectF(c.x - g * 0.6f, c.y - g * 0.7f, c.x - g * 0.15f, c.y + g * 0.7f), 1.2f, ink);
            SFillRound(D2D1::RectF(c.x + g * 0.15f, c.y - g * 0.7f, c.x + g * 0.6f, c.y + g * 0.7f), 1.2f, ink);
        } else {
            SFillRound(D2D1::RectF(c.x - g * 0.5f, c.y - g * 0.5f, c.x + g * 0.5f, c.y + g * 0.5f), 2.0f, ink);
        }
    }

    void DrawStudioHome(const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now) {
        EnsureRoundStroke();
        studioOrigin_ = publishedOrigin_;
        const float W = rect.right - rect.left;
        const float gap = 10.0f;
        const float tileW = (W - 2 * gap) / 3.0f;
        const float tileH = std::min(tileW, (rect.bottom - rect.top) - 34.0f);
        const float top = rect.top + 2.0f;
        const D2D1_COLOR_F white = SCol(0xffffff);
        const bb::AgentView& a = state.agent;

        // First tile: the main agent, or the weather when no agent is shown.
        auto tileWeather = [&](const D2D1_RECT_F r) {
            const D2D1_COLOR_F sky = SCol(0x64d2ff);  // Home cards keep their own colours; only Music follows the album
            if (const int tab = DashboardPageIndex(StudioPage::Weather, settings); tab >= 0) SHit(r, StudioAction::Tab, tab);
            DrawStudioTile(r, sky, false, now);
            SText(state.weather.city.empty() ? std::wstring(L"Weather") : state.weather.city,
                  D2D1::RectF(r.left + 14, r.top + 14, r.right - 52, r.top + 32), SFLabel, D2D1::ColorF(1, 1, 1, 0.65f));
            SIcon(StudioPage::Weather, D2D1::Point2F(r.right - 30, r.top + 30), 20, sky);
            if (state.weather.hasData) {
                std::wstring icon, desc = state.weather.weatherDesc;
                GetWeatherIconAndText(state.weather.weatherCode, icon, desc);
                wchar_t temp[16];
                swprintf_s(temp, L"%.0f\x00B0", state.weather.temperature);
                SText(temp, D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 10, r.bottom - 28), SFBig, white);
                SText(desc, D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 12, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
            } else {
                SText(L"--\x00B0", D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 10, r.bottom - 28), SFBig, white);
                SText(L"Weather is loading", D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 12, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
            }
        };
        auto tileAgent = [&](const D2D1_RECT_F r) {
            const bool busy = a.phase == bb::AgentPhase::Working || a.phase == bb::AgentPhase::Thinking;
            const bool waiting = a.phase == bb::AgentPhase::Approval || a.phase == bb::AgentPhase::Question;
            // Idle, the tile wears the main agent's colour (Claude orange, Antigravity blue...).
            const std::wstring brandName = a.agent.empty() ? std::wstring(AgentKindName(MainAgentKind(settings))) : a.agent;
            const D2D1_COLOR_F col = a.phase == bb::AgentPhase::None ? AgentBrand(brandName) : AgentStatusColor(a.phase);
            SHit(r, StudioAction::AgentFocus);  // opens the agent's window, or the Claude app
            DrawStudioTile(r, busy ? AgentBrand(a.agent) : col, busy || waiting, now);
            const std::wstring tileName = a.phase != bb::AgentPhase::None && !a.agent.empty()
                                              ? a.agent : std::wstring(AgentKindName(MainAgentKind(settings)));
            SText(tileName, D2D1::RectF(r.left + 14, r.top + 14, r.right - 52, r.top + 32), SFLabel, D2D1::ColorF(1, 1, 1, 0.65f));
            const D2D1_POINT_2F rc = D2D1::Point2F(r.right - 30, r.top + 30);
            std::wstring big, line;
            if (busy) {
                SRing(rc, 15, 4.5f, (std::max(a.stage, 0) + 0.5f) / 4.0f, AgentBrand(a.agent));
                SIcon(StudioPage::Agent, rc, 13, AgentBrand(a.agent));
                big = Clock(a.turnStart > 0 ? now - a.turnStart : now - a.phaseSince);
                line = StageWord(a.phase == bb::AgentPhase::Thinking && a.stage < 0 ? -1 : a.stage);
                if (!a.project.empty()) line += L" \x00B7 " + a.project;
            } else if (waiting) {
                SRing(rc, 15, 4.5f, 1.0f, col);
                SIcon(StudioPage::Agent, rc, 13, col);
                big = a.phase == bb::AgentPhase::Question ? L"Your turn" : L"May I?";
                line = a.tool.empty() ? a.detail : a.tool;
            } else if (a.phase == bb::AgentPhase::Finished) {
                SRing(rc, 15, 4.5f, 1.0f, col);
                big = L"Done";
                line = a.prompt.empty() ? a.step : a.prompt;
            } else if (a.phase == bb::AgentPhase::Error) {
                SRing(rc, 15, 4.5f, 1.0f, col);
                big = L"Stopped";
                line = a.step;
            } else {
                float used = 0.0f;
                const wchar_t* tag = L"";
                if (PickUsage(state, settings, now, &used, &tag)) {
                    const float u = used / 100.0f;
                    SRing(rc, 15, 4.5f, u, u >= 0.85f ? SCol(0xff3b3b) : u >= 0.6f ? SCol(0xff9a1f) : SCol(0x22e05a));
                    wchar_t pct[8];
                    swprintf_s(pct, L"%d", static_cast<int>(used + 0.5f));
                    SText(pct, D2D1::RectF(rc.x - 12, rc.y - 8, rc.x + 12, rc.y + 8), SFSmall, white, 1);
                } else {
                    SRing(rc, 15, 4.5f, 0.0f, white);
                    SIcon(StudioPage::Agent, rc, 13, D2D1::ColorF(1, 1, 1, 0.5f));
                }
                big = std::to_wstring(a.jobsToday);
                line = a.jobsToday == 1 ? L"task today" : L"tasks today";
            }
            SText(big, D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 10, r.bottom - 28), big.size() > 6 ? SFSong : SFBig, white);
            SText(line, D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 12, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
        };
        auto tileMusic = [&](const D2D1_RECT_F r) {
            const bool has = state.media.available && !state.media.title.empty();
            const D2D1_COLOR_F col = SAccent();
            SHit(r, StudioAction::OpenMusic);
            DrawStudioTile(r, col, state.media.playing, now);
            SText(L"Music", D2D1::RectF(r.left + 14, r.top + 14, r.right - 60, r.top + 32), SFLabel, D2D1::ColorF(1, 1, 1, 0.65f));
            if (has) {
                DrawAlbumArt(state.media, D2D1::RectF(r.right - 54, r.top + 12, r.right - 12, r.top + 54), now, 10.0f, false);
                SText(state.media.title, D2D1::RectF(r.left + 14, r.bottom - 62, r.right - 46, r.bottom - 42), SFSong, white);
                SText(state.media.artist, D2D1::RectF(r.left + 14, r.bottom - 40, r.right - 46, r.bottom - 24), SFBody, D2D1::ColorF(1, 1, 1, 0.6f));
                const D2D1_POINT_2F bc = D2D1::Point2F(r.right - 27, r.bottom - 27);
                SRoundButton(bc, 15, state.media.playing ? 1 : 0, SCol(0x000000), SHover(StudioAction::PlayPause));
                SHit(D2D1::RectF(bc.x - 17, bc.y - 17, bc.x + 17, bc.y + 17), StudioAction::PlayPause);
            } else {
                SIcon(StudioPage::Music, D2D1::Point2F(r.right - 30, r.top + 30), 18, D2D1::ColorF(1, 1, 1, 0.5f));
                SText(L"Quiet", D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 10, r.bottom - 28), SFBig, white);
                SText(L"Nothing playing", D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 12, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
            }
        };
        auto tileFocus = [&](const D2D1_RECT_F r) {
            const D2D1_COLOR_F purple = SCol(0xbf5af2);
            const TimerSnapshot& tm = state.timer;
            if (settings.focusPage) SHit(r, StudioAction::FocusPage);
            DrawStudioTile(r, purple, tm.active && tm.running, now);
            SText(tm.active && tm.isBreak ? L"Break" : L"Focus", D2D1::RectF(r.left + 14, r.top + 14, r.right - 52, r.top + 32), SFLabel, D2D1::ColorF(1, 1, 1, 0.65f));
            const D2D1_POINT_2F rc = D2D1::Point2F(r.right - 30, r.top + 30);
            const D2D1_POINT_2F bc = D2D1::Point2F(r.right - 27, r.bottom - 27);
            if (tm.active) {
                const double left = tm.running ? std::max(0.0, tm.endsAt - now) : tm.remainingAtPause;
                SRing(rc, 15, 4.5f, tm.totalSeconds > 0 ? static_cast<float>(left / tm.totalSeconds) : 0.0f, purple);
                SText(Clock(left), D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 46, r.bottom - 28), SFBig, white);
                const int mins = static_cast<int>(std::ceil(left / 60.0));
                SText(tm.running ? std::to_wstring(mins) + (mins == 1 ? L" min left" : L" mins left") : std::wstring(L"Paused"),
                      D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 46, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
                SRoundButton(bc, 15, 2, purple, SHover(StudioAction::FocusStop));
                SHit(D2D1::RectF(bc.x - 17, bc.y - 17, bc.x + 17, bc.y + 17), StudioAction::FocusStop);
            } else {
                SRing(rc, 15, 4.5f, 0.0f, purple);
                SText(L"25:00", D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 46, r.bottom - 28), SFBig, white);
                SText(L"Ready to start", D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 46, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
                SRoundButton(bc, 15, 0, purple, SHover(StudioAction::FocusStart));
                SHit(D2D1::RectF(bc.x - 17, bc.y - 17, bc.x + 17, bc.y + 17), StudioAction::FocusStart, 25);
            }
        };
        auto tileCalendar = [&](const D2D1_RECT_F r) {
            const D2D1_COLOR_F red = SCol(0xff453a);
            if (const int tab = DashboardPageIndex(StudioPage::Calendar, settings); tab >= 0) SHit(r, StudioAction::Tab, tab);
            DrawStudioTile(r, red, state.meeting.valid, now);
            SYSTEMTIME st{};
            GetLocalTime(&st);
            static const wchar_t* kDays[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
            static const wchar_t* kMonths[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
            SText(kDays[st.wDayOfWeek % 7], D2D1::RectF(r.left + 14, r.top + 14, r.right - 52, r.top + 32), SFLabel, red);
            SIcon(StudioPage::Calendar, D2D1::Point2F(r.right - 30, r.top + 30), 20, red);
            SText(std::to_wstring(st.wDay) + L" " + kMonths[(st.wMonth + 11) % 12],
                  D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 10, r.bottom - 28), SFBig, white);
            std::wstring line = L"No meetings coming up";
            if (state.meeting.valid) {
                const time_t t = static_cast<time_t>(state.meeting.startsAt);
                tm lt{};
                localtime_s(&lt, &t);
                wchar_t b[16];
                swprintf_s(b, L"%02d:%02d ", lt.tm_hour, lt.tm_min);
                line = b + (state.meeting.title.empty() ? std::wstring(L"Meeting") : state.meeting.title);
            }
            SText(line, D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 12, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
        };
        auto tileBattery = [&](const D2D1_RECT_F r) {
            const int pct = state.battery.percent;
            const D2D1_COLOR_F col = !state.battery.hasBattery ? SCol(0x8e8e93)
                                     : state.battery.charging ? SCol(0x30d158) : pct <= 20 ? SCol(0xff453a) : SCol(0x30d158);
            DrawStudioTile(r, col, state.battery.reallyCharging, now);
            SText(L"Battery", D2D1::RectF(r.left + 14, r.top + 14, r.right - 52, r.top + 32), SFLabel, D2D1::ColorF(1, 1, 1, 0.65f));
            SRing(D2D1::Point2F(r.right - 30, r.top + 30), 15, 4.5f, state.battery.hasBattery ? pct / 100.0f : 0.0f, col);
            SText(state.battery.hasBattery ? std::to_wstring(pct) + L"%" : std::wstring(L"Plugged in"),
                  D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 10, r.bottom - 28), state.battery.hasBattery ? SFBig : SFSong, white);
            std::wstring line = !state.battery.hasBattery ? L"No battery" : state.battery.charging ? L"Charging" : L"On battery";
            if (state.battery.hasBattery && state.battery.charging && state.battery.minutesToFull > 0)
                {
                const int m = state.battery.minutesToFull;
                line = L"Full in " + (m >= 60 ? std::to_wstring(m / 60) + L" h " + std::to_wstring(m % 60) + L" min" : std::to_wstring(m) + L" min");
            }
            SText(line, D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 12, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
        };
        auto tileSystem = [&](const D2D1_RECT_F r) {
            const D2D1_COLOR_F green = SCol(0x30d158);
            if (const int tab = DashboardPageIndex(StudioPage::Hardware, settings); tab >= 0) SHit(r, StudioAction::Tab, tab);
            DrawStudioTile(r, green, false, now);
            SText(L"System", D2D1::RectF(r.left + 14, r.top + 14, r.right - 52, r.top + 32), SFLabel, D2D1::ColorF(1, 1, 1, 0.65f));
            SRing(D2D1::Point2F(r.right - 30, r.top + 30), 15, 4.5f, state.system.memoryPercent / 100.0f, SCol(0x64d2ff));
            SText(std::to_wstring(state.system.cpuPercent) + L"%", D2D1::RectF(r.left + 14, r.bottom - 58, r.right - 10, r.bottom - 28), SFBig, white);
            SText(L"CPU \x00B7 RAM " + std::to_wstring(state.system.memoryPercent) + L"%",
                  D2D1::RectF(r.left + 14, r.bottom - 26, r.right - 12, r.bottom - 10), SFBody, D2D1::ColorF(1, 1, 1, 0.8f));
        };

        // Three slots; each shows the tile picked in Settings (Bunbun.HomeTile1-3)
        // or with Edit on Home. Auto = agent (weather when no agent is shown), music, focus.
        for (int slot = 0; slot < 3; ++slot) {
            const float x = rect.left + slot * (tileW + gap);
            const D2D1_RECT_F r = D2D1::RectF(x, top, x + tileW, top + tileH);
            switch (HomeTileKind(settings, slot)) {
                case HomeTile::Agent: tileAgent(r); break;
                case HomeTile::Music: tileMusic(r); break;
                case HomeTile::Focus: tileFocus(r); break;
                case HomeTile::Weather: tileWeather(r); break;
                case HomeTile::Calendar: tileCalendar(r); break;
                case HomeTile::Battery: tileBattery(r); break;
                case HomeTile::System: tileSystem(r); break;
            }
        }
        // The cards are chosen in Settings → Island pages (the island has no Edit mode).

        // Your links, as chips under the tiles (they take the info line's place).
        if (const auto links = ParseHomeLinks(settings.homeLinks); !links.empty()) {
            float total = 0.0f;
            std::vector<float> widths;
            for (const auto& [name, target] : links) {
                widths.push_back(SMeasure(name, SFLabel) + 40.0f);
                total += widths.back() + 8.0f;
            }
            float cx = (rect.left + rect.right - 60.0f - (total - 8.0f)) * 0.5f;
            const float cy = top + tileH + 6;
            for (size_t i = 0; i < links.size(); ++i) {
                const D2D1_RECT_F chip = D2D1::RectF(cx, cy, cx + widths[i], cy + 24);
                const float hover = SHover(StudioAction::OpenLink, static_cast<int>(i));
                SFillRound(chip, 12, D2D1::ColorF(1, 1, 1, SLerp(0.09f, 0.18f, hover)));
                // A coloured dot with the first letter, its hue from the address.
                unsigned hash = 0;
                for (wchar_t ch : links[i].second) hash = hash * 31 + ch;
                const float hue = static_cast<float>(hash % 360) / 360.0f;
                auto channel = [&](float n) {
                    const float k = std::fmod(n + hue * 12.0f, 12.0f);
                    return 0.52f - 0.62f * 0.48f * std::max(-1.0f, std::min({k - 3.0f, 9.0f - k, 1.0f}));
                };
                const D2D1_COLOR_F dot = D2D1::ColorF(channel(0), channel(8), channel(4));
                if (ID2D1SolidColorBrush* b = AcquireBrush(dot))
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(chip.left + 14, cy + 12), 8, 8), b);
                SText(std::wstring(1, static_cast<wchar_t>(towupper(links[i].first.empty() ? L'?' : links[i].first[0]))),
                      D2D1::RectF(chip.left + 6, cy, chip.left + 22, cy + 24), SFSmall, white, 1);
                SText(links[i].first, D2D1::RectF(chip.left + 28, cy, chip.right - 8, cy + 24), SFLabel, white);
                SHit(chip, StudioAction::OpenLink, static_cast<int>(i));
                cx += widths[i] + 8.0f;
            }
            return;
        }

        // One quiet line under the tiles: weather and battery.
        std::wstring info;
        if (state.weather.hasData) {
            std::wstring icon = L"", text = state.weather.weatherDesc;
            GetWeatherIconAndText(state.weather.weatherCode, icon, text);
            wchar_t b[96];
            swprintf_s(b, L"%s %.0f\x00B0  %s", icon.c_str(), state.weather.temperature, text.c_str());
            info = b;
            if (!state.weather.city.empty()) info += L" \x00B7 " + state.weather.city;
        }
        if (state.battery.hasBattery) {
            if (!info.empty()) info += L"   ";
            info += (state.battery.charging ? L"\x26A1 " : L"\U0001F50B ") + std::to_wstring(state.battery.percent) + L"%";
        }
        SText(info, D2D1::RectF(rect.left + 4, top + tileH + 6, rect.right - 60, top + tileH + 28), SFBody, D2D1::ColorF(1, 1, 1, 0.55f), 1);
    }

    // ── Extra pages (both layouts) ──
    bool DrawStudioExtraPage(int tabId, const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now) {
        if (tabId != kTabAgent && tabId != kTabClipboard && tabId != kTabNote && tabId != kTabFocus && tabId != kTabApps &&
            tabId != kTabMirror && tabId != kTabHome)
            return false;
        if (tabId == kTabHome) {
            DrawStudioHome(state, rect, settings, now);
            return true;
        }
        EnsureRoundStroke();
        studioOrigin_ = publishedOrigin_;
        // Classic pages have no frame, so keep clear of the pagination dots on the right.
        D2D1_RECT_F r = rect;
        if (!settings.studioLayout) r = D2D1::RectF(rect.left + 18, rect.top + 14, rect.right - 24, rect.bottom - 12);
        if (tabId == kTabAgent) DrawStudioAgentPage(state, r, now);
        else if (tabId == kTabClipboard) DrawStudioClipboardPage(r);
        else if (tabId == kTabFocus) DrawStudioFocusPage(state, r, now);
        else if (tabId == kTabApps) DrawStudioAppsPage(state, r);
        else if (tabId == kTabMirror) DrawStudioMirrorPage(r);
        else DrawStudioNotePage(r, now);
        return true;
    }

    void DrawStudioAgentPage(const SharedState& state, D2D1_RECT_F r, double now) {
        const bb::AgentView& a = state.agent;
        const float W = r.right - r.left;
        const D2D1_COLOR_F status = AgentStatusColor(a.phase);
        const D2D1_COLOR_F brand = AgentBrand(a.agent);
        const bool busy = a.phase == bb::AgentPhase::Working || a.phase == bb::AgentPhase::Thinking;
        SHit(r, StudioAction::AgentFocus);

        // Top row: lamp + agent · project, status on the right.
        const bool blink = busy && std::fmod(now, 1.2) < 0.6;
        if (ID2D1SolidColorBrush* b = AcquireBrush(WithAlpha(status, blink ? 0.45f : 1.0f)))
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 5, r.top + 10), 4, 4), b);
        std::wstring who = a.agent.empty() ? L"No agent running" : a.agent;
        if (!a.project.empty()) who += L" \x00B7 " + a.project;
        SText(who, D2D1::RectF(r.left + 16, r.top, r.left + W * 0.6f, r.top + 20), SFTitle, SCol(0xffffff));
        std::wstring stat;
        switch (a.phase) {
            case bb::AgentPhase::Thinking:
            case bb::AgentPhase::Working: stat = L"Working \x00B7 " + Clock(a.turnStart > 0 ? now - a.turnStart : now - a.phaseSince); break;
            case bb::AgentPhase::Approval: stat = L"Needs your OK"; break;
            case bb::AgentPhase::Question: stat = L"Has a question"; break;
            case bb::AgentPhase::Finished: stat = L"Done"; break;
            case bb::AgentPhase::Error: stat = L"Stopped"; break;
            default: stat = L"Idle"; break;
        }
        SText(stat, D2D1::RectF(r.left + W * 0.6f, r.top, r.right, r.top + 20), SFBody, SCol(0x8e8e93), 2);
        // Claude Code's model, as a chip that cycles Default / Opus / Sonnet / Haiku.
        if (AgentKindFor(a.agent) == AgentClaude && g_settings.showAgent[AgentClaude]) {
            const std::wstring model = L"Model: " + ClaudeModelLabel();
            const float mw = SMeasure(model, SFLabel) + 20;
            const float sw = SMeasure(stat, SFBody);
            const D2D1_RECT_F chip = D2D1::RectF(r.right - sw - 12 - mw, r.top, r.right - sw - 12, r.top + 20);
            SFillRound(chip, 10, D2D1::ColorF(1, 1, 1, SLerp(0.09f, 0.16f, SHover(StudioAction::ModelCycle))));
            SText(model, chip, SFLabel, SCol(0xe8e8ee), 1);
            SHit(chip, StudioAction::ModelCycle);
        }

        // Left: hero number or word, and a sub-line.
        const float leftW = W * 0.40f;
        std::wstring hero, sub;
        if (busy) {
            hero = Clock(a.turnStart > 0 ? now - a.turnStart : now - a.phaseSince);
            const int step = std::clamp(a.stage + 1, 1, 4);
            sub = std::wstring(StageWord(a.stage)) + L" \x00B7 step " + std::to_wstring(step) + L" of 4";
        } else if (a.phase == bb::AgentPhase::Approval) { hero = L"May I?"; sub = a.tool; }
        else if (a.phase == bb::AgentPhase::Question) { hero = L"Your turn"; sub = L"Answer on the island"; }
        else if (a.phase == bb::AgentPhase::Finished) { hero = L"Done"; sub = a.turnSeconds > 0 ? L"took " + Clock(a.turnSeconds) : L""; }
        else if (a.phase == bb::AgentPhase::Error) { hero = L"Stopped"; sub = a.step; }
        else { hero = std::to_wstring(a.jobsToday); sub = a.jobsToday == 1 ? L"task done today" : L"tasks done today"; }
        SText(hero, D2D1::RectF(r.left, r.top + 30, r.left + leftW, r.top + 82), hero.size() > 7 ? SFBig : SFHero, busy ? brand : SCol(0xffffff));
        SText(sub, D2D1::RectF(r.left, r.top + 84, r.left + leftW, r.top + 102), SFBody, SCol(0x8e8e93));

        // Right: a card with the command, the diff, the prompt, or recent tasks.
        const D2D1_RECT_F card = D2D1::RectF(r.left + leftW + 12, r.top + 28, r.right, r.bottom - 40);
        SFillRound(card, 12, D2D1::ColorF(1, 1, 1, 0.07f));
        const float lineH = 17.0f;
        float y = card.top + 8;
        auto row = [&](const std::wstring& text, StudioFontId f, D2D1_COLOR_F c, D2D1_COLOR_F bg = D2D1::ColorF(0, 0, 0, 0)) {
            if (y + lineH > card.bottom - 4) return;
            if (bg.a > 0) SFillRound(D2D1::RectF(card.left + 6, y, card.right - 6, y + lineH), 4, bg);
            SText(text, D2D1::RectF(card.left + 12, y, card.right - 10, y + lineH), f, c);
            y += lineH + 2;
        };
        if (!a.command.empty() && (a.stage == 2 || busy) && (a.stage == 2 || a.diffNew.empty())) {
            row(L"$ " + a.command, SFMono, SCol(0x32d74b));
            for (const auto& l : a.output) row(l, SFMono, a.commandFailed ? SCol(0xff6961) : SCol(0xaeaeb2));
            if (a.output.empty() && busy) row(L"running\x2026", SFMono, SCol(0x636366));
        } else if (!a.diffOld.empty() || !a.diffNew.empty()) {
            if (!a.diffOld.empty()) row(L"\x2212 " + a.diffOld, SFMono, SCol(0xff8a80), D2D1::ColorF(1.0f, 0.27f, 0.23f, 0.13f));
            if (!a.diffNew.empty()) row(L"+ " + a.diffNew, SFMono, SCol(0x7ee787), D2D1::ColorF(0.2f, 0.84f, 0.29f, 0.12f));
            if (a.added >= 0) row(L"+" + std::to_wstring(a.added) + L"  \x2212" + std::to_wstring(a.removed) + L" lines", SFSmall, SCol(0x8e8e93));
        } else if (busy || a.phase == bb::AgentPhase::Approval) {
            if (!a.prompt.empty()) row(L"\x201C" + a.prompt + L"\x201D", SFBody, SCol(0xffffff));
            row(a.phase == bb::AgentPhase::Approval ? a.detail : (a.step.empty() ? L"Thinking\x2026" : a.step), SFBody, SCol(0xaeaeb2));
        } else {
            row(L"Recently", SFLabel, SCol(0x8e8e93));
            if (a.history.empty()) row(L"Finished tasks show up here.", SFBody, SCol(0x636366));
            for (size_t i = 0; i < a.history.size() && i < 4; ++i) {
                const auto& h = a.history[i];
                const time_t when = static_cast<time_t>(h.finishedAt);
                tm lt{};
                localtime_s(&lt, &when);
                wchar_t right[32];
                swprintf_s(right, L"%02d:%02d \x00B7 %s", lt.tm_hour, lt.tm_min, Clock(h.seconds).c_str());
                const float rw = SMeasure(right, SFSmall);
                if (y + lineH > card.bottom - 4) break;
                SText((h.failed ? L"\x2715 " : L"") + (h.project.empty() ? h.agent : h.project) + L" \x00B7 " + h.prompt,
                      D2D1::RectF(card.left + 12, y, card.right - rw - 16, y + lineH), SFBody, SCol(0xffffff));
                SText(right, D2D1::RectF(card.right - rw - 12, y, card.right - 10, y + lineH), SFSmall, SCol(0x8e8e93), 2);
                y += lineH + 2;
            }
        }

        // A finished task still listening for your next message: Reply.
        if (a.replyId && !busy) {
            const std::wstring label = L"Reply\x2026";
            const D2D1_RECT_F rb = D2D1::RectF(card.right - 92, card.bottom - 34, card.right - 8, card.bottom - 8);
            SFillRound(rb, 13, D2D1::ColorF(1, 1, 1, SLerp(0.92f, 1.0f, SHover(StudioAction::AgentReply))));
            SText(label, rb, SFLabel, SCol(0x141416), 1);
            SHit(rb, StudioAction::AgentReply);
        }

        // Bottom: Read / Edit / Test / Done while working, else the usage limits.
        const float by = r.bottom - 30;
        if (busy || a.phase == bb::AgentPhase::Finished) {
            static const wchar_t* kSteps[] = {L"Read", L"Edit", L"Test", L"Done"};
            const float segW = (W - 3 * 8.0f) / 4.0f;
            for (int i = 0; i < 4; ++i) {
                const float x = r.left + i * (segW + 8.0f);
                const bool done = a.stage > i || a.phase == bb::AgentPhase::Finished;
                const bool current = !done && (a.stage == i || (a.stage < 0 && i == 0));
                SFillRound(D2D1::RectF(x, by, x + segW, by + 4), 2, D2D1::ColorF(1, 1, 1, done ? 0.95f : 0.16f));
                if (current) {
                    const float t = 0.5f + 0.5f * std::sin(static_cast<float>(now) * 6.2831853f / 1.3f);
                    const float sw = segW * 0.45f, sx = x + (segW - sw) * t;
                    SFillRound(D2D1::RectF(sx, by, sx + sw, by + 4), 2, brand);
                }
                SText(kSteps[i], D2D1::RectF(x, by + 7, x + segW, by + 22), SFSmall, done || current ? SCol(0xffffff) : SCol(0x636366));
            }
        } else if ((g_settings.usageLimits && g_settings.showAgent[AgentClaude] && (a.usage5h >= 0.0f || a.contextPct >= 0.0f)) ||
                   (g_settings.antigravityQuota && g_settings.showAgent[AgentAntigravity] && AgQuota::Get().valid)) {
            struct Bar { const wchar_t* name; float pct; };
            const AgQuota::Snapshot ag = g_settings.antigravityQuota && g_settings.showAgent[AgentAntigravity]
                                             ? AgQuota::Get() : AgQuota::Snapshot{};
            const bool claude = g_settings.usageLimits && g_settings.showAgent[AgentClaude];
            const Bar bars[3] = {{L"5-hour limit", claude ? a.usage5h : -1.0f}, {L"Week", claude ? a.usage7d : -1.0f},
                                 ag.valid ? Bar{L"Antigravity", (1.0f - ag.remaining) * 100.0f}
                                          : Bar{L"Context", claude ? a.contextPct : -1.0f}};
            const float segW = (W - 2 * 14.0f) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                const float x = r.left + i * (segW + 14.0f);
                const float p = bars[i].pct;
                const D2D1_COLOR_F c = p >= 85 ? SCol(0xff3b3b) : p >= 60 ? SCol(0xff9a1f) : SCol(0x22e05a);
                SFillRound(D2D1::RectF(x, by, x + segW, by + 4), 2, D2D1::ColorF(1, 1, 1, 0.16f));
                if (p >= 0) SFillRound(D2D1::RectF(x, by, x + segW * Clamp(p / 100.0f, 0.0f, 1.0f), by + 4), 2, c);
                std::wstring label = bars[i].name;
                if (p >= 0) label += L" \x00B7 " + std::to_wstring(static_cast<int>(p + 0.5f)) + L"%";
                if (i == 2 && ag.valid && ag.resetAt > 0) {
                    const long long mins = (ag.resetAt - static_cast<long long>(time(nullptr))) / 60;
                    if (mins > 0) label += L" \x00B7 resets in " + (mins >= 60 ? std::to_wstring(mins / 60) + L"h" : std::to_wstring(mins) + L"m");
                }
                if (i == 0 && p >= 0 && a.usage5hResets > 0) {
                    const long long mins = (a.usage5hResets - static_cast<long long>(time(nullptr))) / 60;
                    if (mins > 0) label += L" \x00B7 resets in " + (mins >= 60 ? std::to_wstring(mins / 60) + L"h" : std::to_wstring(mins) + L"m");
                }
                SText(label, D2D1::RectF(x, by + 7, x + segW, by + 22), SFSmall, p >= 0 ? SCol(0xaeaeb2) : SCol(0x636366));
            }
        } else if (g_settings.usageLimits && g_settings.showAgent[AgentClaude]) {
            SText(L"Connect Claude Code in Settings to see your usage limits here.",
                  D2D1::RectF(r.left, by, r.right, by + 20), SFSmall, SCol(0x636366));
        }
    }

    void DrawStudioClipboardPage(D2D1_RECT_F r) {
        const std::vector<std::wstring> items = ClipHistorySnapshot();
        SText(L"Recent copies", D2D1::RectF(r.left, r.top, r.right - 60, r.top + 20), SFLabel, SCol(0x8e8e93));
        if (!items.empty()) {
            const D2D1_RECT_F clear = D2D1::RectF(r.right - 52, r.top + 1, r.right, r.top + 19);
            SFillRound(clear, 9, D2D1::ColorF(1.0f, 0.27f, 0.23f, SLerp(0.18f, 0.3f, SHover(StudioAction::ClipClear))));
            SText(L"Clear", clear, SFLabel, SCol(0xff6961), 1);
            SHit(clear, StudioAction::ClipClear);
        }
        if (items.empty()) {
            SText(L"Copy some text and it shows up here.", D2D1::RectF(r.left, r.top + 40, r.right, r.top + 70), SFBody, SCol(0x636366), 1);
            return;
        }
        const float rowH = 24.0f;
        float y = r.top + 26;
        for (size_t i = 0; i < items.size() && y + rowH <= r.bottom + 2; ++i) {
            std::wstring text = items[i];
            for (auto& c : text) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
            const D2D1_RECT_F row = D2D1::RectF(r.left, y, r.right, y + rowH - 2);
            const float hoverAmt = SHover(StudioAction::ClipCopy, static_cast<int>(i));
            const bool hover = hoverAmt > 0.5f;
            SFillRound(row, 8, D2D1::ColorF(1, 1, 1, SLerp(0.05f, 0.13f, hoverAmt)));
            SIcon(StudioPage::Clipboard, D2D1::Point2F(row.left + 14, (row.top + row.bottom) * 0.5f), 13, SAccent());
            SText(text, D2D1::RectF(row.left + 28, row.top, row.right - 64, row.bottom), SFBody, SCol(0xffffff));
            if (hover) SText(L"Copy", D2D1::RectF(row.right - 60, row.top, row.right - 10, row.bottom), SFLabel, SAccent(), 2);
            SHit(row, StudioAction::ClipCopy, static_cast<int>(i));
            y += rowH;
        }
    }

    void DrawStudioNotePage(D2D1_RECT_F r, double now) {
        const std::vector<std::wstring>& lines = QuickNote::Lines(now);
        const D2D1_RECT_F edit = D2D1::RectF(r.right - 52, r.top + 1, r.right, r.top + 19);
        SFillRound(edit, 9, WithAlpha(SAccent(), SLerp(0.18f, 0.3f, SHover(StudioAction::NoteEdit))));
        SText(L"Edit", edit, SFLabel, SAccent(), 1);
        SHit(edit, StudioAction::NoteEdit);
        if (lines.empty()) {
            SText(L"No note yet", D2D1::RectF(r.left, r.top, r.right - 60, r.top + 20), SFSong, SCol(0xffffff));
            SText(L"Click Edit to write one. Lines that start with [ ] get a checkbox.",
                  D2D1::RectF(r.left, r.top + 40, r.right, r.top + 60), SFBody, SCol(0x636366));
            return;
        }
        float y = r.top;
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::wstring& line = lines[i];
            if (i == 0) {
                SText(line, D2D1::RectF(r.left, y, r.right - 60, y + 20), SFSong, SCol(0xffffff));
                y += 26;
                continue;
            }
            const float h = line.empty() ? 8.0f : 20.0f;
            if (y + h > r.bottom + 2) break;
            bool checked = false;
            if (QuickNote::IsCheckbox(line, &checked)) {
                const D2D1_POINT_2F c = D2D1::Point2F(r.left + 8, y + 10);
                const bool hover = SHovered(StudioAction::NoteToggle, static_cast<int>(i));
                if (ID2D1SolidColorBrush* b = AcquireBrush(checked ? SAccent() : D2D1::ColorF(1, 1, 1, hover ? 0.9f : 0.5f))) {
                    if (checked) target_->FillEllipse(D2D1::Ellipse(c, 7, 7), b);
                    else target_->DrawEllipse(D2D1::Ellipse(c, 7, 7), b, 1.5f);
                }
                if (checked) {
                    auto P = [](float x, float yy) { return D2D1::Point2F(x, yy); };
                    SPoly({P(8.5f, 12), P(11, 14.5f), P(15.5f, 9.5f)}, c, 1.0f, SCol(0x000000), 1.8f);
                }
                const std::wstring text = line.substr(line.find(L']') + 1);
                SText(text, D2D1::RectF(r.left + 22, y, r.right, y + 20), SFBody,
                      checked ? SCol(0x636366) : SCol(0xffffff));
                SHit(D2D1::RectF(r.left, y, r.right, y + 20), StudioAction::NoteToggle, static_cast<int>(i));
            } else if (line.rfind(L"- ", 0) == 0) {
                if (ID2D1SolidColorBrush* b = AcquireBrush(SAccent()))
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 8, y + 10), 2.5f, 2.5f), b);
                SText(line.substr(2), D2D1::RectF(r.left + 22, y, r.right, y + 20), SFBody, SCol(0xffffff));
            } else {
                SText(line, D2D1::RectF(r.left, y, r.right, y + 20), SFBody, SCol(0xaeaeb2));
            }
            y += h;
        }
    }

    // ── Mirror page ──
    ComPtr<ID2D1Bitmap> mirrorBmp_;
    uint64_t mirrorBmpId_ = 0;

    void DrawStudioMirrorPage(D2D1_RECT_F r) {
        EnsureRoundStroke();
        const D2D1_COLOR_F sky = SAccent();
        if (!Mirror::g_running.load()) {
            mirrorBmp_.Reset();
            std::wstring err;
            {
                std::lock_guard lock(Mirror::g_mutex);
                err = Mirror::g_error;
            }
            const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
            SIcon(StudioPage::Mirror, D2D1::Point2F(cx, cy - 42), 34.0f, sky);
            const D2D1_RECT_F b = D2D1::RectF(cx - 74, cy - 10, cx + 74, cy + 22);
            SFillRound(b, 16, D2D1::ColorF(1, 1, 1, SLerp(0.92f, 1.0f, SHover(StudioAction::MirrorStart))));
            SText(L"Turn on camera", b, SFLabel, SCol(0x141416), 1);
            SHit(b, StudioAction::MirrorStart);
            SText(err.empty() ? std::wstring(L"Only while this page is open. Nothing is saved.") : err,
                  D2D1::RectF(r.left, cy + 32, r.right, cy + 50), SFSmall, err.empty() ? SCol(0x8e8e93) : SCol(0xff6961), 1);
            return;
        }
        Mirror::KeepAlive();
        {
            std::lock_guard lock(Mirror::g_mutex);
            if (Mirror::g_frameId != mirrorBmpId_ && !Mirror::g_frame.empty()) {
                mirrorBmpId_ = Mirror::g_frameId;
                const D2D1_SIZE_U size = D2D1::SizeU(Mirror::g_width, Mirror::g_height);
                const D2D1_BITMAP_PROPERTIES props =
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                if (!mirrorBmp_ || mirrorBmp_->GetPixelSize().width != size.width || mirrorBmp_->GetPixelSize().height != size.height) {
                    mirrorBmp_.Reset();
                    target_->CreateBitmap(size, Mirror::g_frame.data(), size.width * 4, props, &mirrorBmp_);
                } else {
                    mirrorBmp_->CopyFromMemory(nullptr, Mirror::g_frame.data(), size.width * 4);
                }
            }
        }
        const D2D1_RECT_F view = D2D1::RectF(r.left, r.top, r.right, r.bottom);
        SFillRound(view, 16, SCol(0x000000));
        if (mirrorBmp_) {
            // Cover-fit, flipped like a mirror, clipped to a rounded frame.
            const D2D1_SIZE_F src = mirrorBmp_->GetSize();
            const float vw = view.right - view.left, vh = view.bottom - view.top;
            const float scale = std::max(vw / src.width, vh / src.height);
            const float dw = src.width * scale, dh = src.height * scale;
            const D2D1_RECT_F dst = D2D1::RectF((view.left + view.right - dw) * 0.5f, (view.top + view.bottom - dh) * 0.5f,
                                                (view.left + view.right + dw) * 0.5f, (view.top + view.bottom + dh) * 0.5f);
            ComPtr<ID2D1RoundedRectangleGeometry> clip;
            d2dFactory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(view, 16, 16), &clip);
            ID2D1Layer* layer = AcquireLayer();
            if (clip && layer) target_->PushLayer(D2D1::LayerParameters(view, clip.Get()), layer);
            D2D1_MATRIX_3X2_F old;
            target_->GetTransform(&old);
            const float mid = (view.left + view.right) * 0.5f;
            target_->SetTransform(D2D1::Matrix3x2F::Scale(-1.0f, 1.0f, D2D1::Point2F(mid, 0)) * old);
            target_->DrawBitmap(mirrorBmp_.Get(), dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            target_->SetTransform(old);
            if (clip && layer) target_->PopLayer();
        } else {
            SText(L"Starting the camera\x2026", view, SFBody, SCol(0x8e8e93), 1);
        }
        const D2D1_RECT_F off = D2D1::RectF(view.right - 96, view.top + 8, view.right - 8, view.top + 34);
        SFillRound(off, 13, D2D1::ColorF(0, 0, 0, SLerp(0.55f, 0.75f, SHover(StudioAction::MirrorStop))));
        SText(L"Turn off", off, SFLabel, SCol(0xffffff), 1);
        SHit(off, StudioAction::MirrorStop);
    }

    // ── All apps page ──
    // Every page of the island, plus the colour picker and Settings, in one grid.
    void DrawStudioAppsPage(const SharedState& state, D2D1_RECT_F r) {
        EnsureRoundStroke();
        struct App { StudioPage page; int tab; StudioAction action; const wchar_t* name; };
        std::vector<App> apps;
        const Settings& s = g_settings;
        const int ctx = g_dashboardContext.load();
        for (int id : DashboardTabIds(ctx, s)) {
            const StudioPage p = StudioPageFor(ctx, id);
            if (p == StudioPage::Apps) continue;
            apps.push_back({p, id, StudioAction::OpenTab, StudioTabLabel(p)});
        }
        apps.push_back({StudioPage::Home, -1, StudioAction::ColorPicker, L"Colour"});
        apps.push_back({StudioPage::Home, -2, StudioAction::Settings, L"Settings"});

        const int cols = 6;
        const float cellW = (r.right - r.left) / cols;
        const float cellH = 84.0f;
        const bool waiting = state.agent.phase == bb::AgentPhase::Approval || state.agent.phase == bb::AgentPhase::Question;
        size_t trayCount = state.fileTrayItems.size();
        for (size_t i = 0; i < apps.size() && i < 12; ++i) {
            const float x = r.left + (i % cols) * cellW;
            const float y = r.top + 4 + (i / cols) * cellH;
            const D2D1_RECT_F icon = D2D1::RectF(x + cellW * 0.5f - 22, y, x + cellW * 0.5f + 22, y + 44);
            const App& app = apps[i];
            const int arg = app.action == StudioAction::OpenTab ? app.tab : 0;
            const float hover = SHover(app.action, app.action == StudioAction::OpenTab ? arg : INT_MIN);
            SFillRound(icon, 10, SLerpCol(SCol(0x1c1c1e), SCol(0x3a3a3c), hover));
            const D2D1_POINT_2F c = D2D1::Point2F((icon.left + icon.right) * 0.5f, (icon.top + icon.bottom) * 0.5f);
            const D2D1_COLOR_F ink = SCol(0xe8e8ee);
            if (app.tab == -1) SEyedropper(c, 20.0f, ink);
            else if (app.tab == -2) SGear(c, 20.0f, ink);
            else SIcon(app.page, c, 20.0f, ink);
            SText(app.name, D2D1::RectF(x, y + 48, x + cellW, y + 64), SFSmall, SCol(0xffffff), 1);
            // Badges: an agent waiting for you, files in the tray.
            int badge = 0;
            if (app.page == StudioPage::Agent && waiting) badge = 1;
            if (app.page == StudioPage::Tray && trayCount > 0) badge = static_cast<int>(trayCount);
            if (badge > 0) {
                const D2D1_POINT_2F bc = D2D1::Point2F(icon.right - 2, icon.top + 2);
                if (ID2D1SolidColorBrush* b = AcquireBrush(SCol(0xff4245))) target_->FillEllipse(D2D1::Ellipse(bc, 9, 9), b);
                SText(badge > 9 ? std::wstring(L"9+") : std::to_wstring(badge), D2D1::RectF(bc.x - 9, bc.y - 9, bc.x + 9, bc.y + 9),
                      SFSmall, SCol(0xffffff), 1);
            }
            SHit(D2D1::RectF(x + 4, y, x + cellW - 4, y + 66), app.action, arg);
        }
    }

    // ── Focus page ──
    void DrawStudioFocusPage(const SharedState& state, D2D1_RECT_F r, double now) {
        EnsureRoundStroke();
        if (g_settings.focusGuard) EnsureFocusGuard();
        LoadFocusPicker();
        const D2D1_COLOR_F purple = SAccent(), white = SCol(0xffffff), grey = SCol(0x8e8e93);
        const TimerSnapshot& tm = state.timer;
        const float W = r.right - r.left;
        auto pill = [&](D2D1_RECT_F b, const std::wstring& label, bool primary, StudioAction action, int arg) {
            const float hover = SHover(action, arg);
            SFillRound(b, (b.bottom - b.top) * 0.5f,
                       primary ? D2D1::ColorF(1, 1, 1, SLerp(0.92f, 1.0f, hover)) : D2D1::ColorF(1, 1, 1, SLerp(0.09f, 0.18f, hover)));
            SText(label, b, SFLabel, primary ? SCol(0x141416) : white, 1);
            SHit(b, action, arg);
        };

        if (tm.active) {
            const double left = tm.running ? std::max(0.0, tm.endsAt - now) : tm.remainingAtPause;
            const float frac = tm.totalSeconds > 0 ? static_cast<float>(left / tm.totalSeconds) : 0.0f;
            SText(Clock(left), D2D1::RectF(r.left, r.top + 6, r.left + W * 0.62f, r.top + 70), SFHero, white);
            SText(tm.isBreak ? L"Break" : (tm.running ? L"Focus" : L"Focus \x00B7 paused"),
                  D2D1::RectF(r.left + 2, r.top + 72, r.left + W * 0.62f, r.top + 90), SFBody, purple);
            const D2D1_RECT_F bar = D2D1::RectF(r.left, r.top + 104, r.right, r.top + 108);
            SFillRound(bar, 2, D2D1::ColorF(1, 1, 1, 0.12f));
            SFillRound(D2D1::RectF(bar.left, bar.top, bar.left + (bar.right - bar.left) * Clamp(frac, 0.0f, 1.0f), bar.bottom), 2, purple);
            const float bx = r.right - 120;
            pill(D2D1::RectF(bx, r.top + 8, r.right, r.top + 38), tm.running ? L"Pause" : L"Resume", true, StudioAction::FocusPause, 0);
            pill(D2D1::RectF(bx, r.top + 44, r.right, r.top + 74), L"+5 min", false, StudioAction::FocusAdd5, 0);
            pill(D2D1::RectF(bx, r.top + 80 - 6, r.right, r.top + 80 + 24 - 6), L"Stop", false, StudioAction::FocusStop, 0);
            SText(L"Bun lets you know when time is up.", D2D1::RectF(r.left, r.top + 118, r.right, r.top + 136), SFSmall, grey);
            return;
        }

        // Idle: the picker on the left, start and presets on the right.
        const float colW = 64.0f, rowH = 30.0f;
        const float pickTop = r.top + 14.0f;
        const float pickH = rowH * 3;
        SFillRound(D2D1::RectF(r.left, pickTop + rowH, r.left + colW * 2 + 40, pickTop + rowH * 2), 9, D2D1::ColorF(1, 1, 1, 0.07f));
        for (int col = 0; col < 2; ++col) {
            const float x = r.left + col * (colW + 20);
            const int value = col == 0 ? g_focusPickH : g_focusPickM;
            const int mod = col == 0 ? 6 : 60;
            for (int k = -1; k <= 1; ++k) {
                const int v = ((value + k) % mod + mod) % mod;
                wchar_t b[8];
                swprintf_s(b, col == 0 ? L"%d" : L"%02d", v);
                SText(b, D2D1::RectF(x, pickTop + (k + 1) * rowH, x + colW, pickTop + (k + 2) * rowH),
                      k == 0 ? SFSong : SFBody, k == 0 ? white : SCol(0x48484a), 1);
            }
            SText(col == 0 ? L"h" : L"min", D2D1::RectF(x + colW - 6, pickTop + rowH, x + colW + 20, pickTop + rowH * 2), SFSmall, grey);
            SHit(D2D1::RectF(x, pickTop, x + colW, pickTop + pickH), StudioAction::FocusWheel, col);
        }
        const int total = FocusPickerMinutes();
        SYSTEMTIME st{};
        GetLocalTime(&st);
        const int doneMin = (st.wHour * 60 + st.wMinute + total) % (24 * 60);
        wchar_t done[32];
        swprintf_s(done, L"Done at %02d:%02d", doneMin / 60, doneMin % 60);
        SText(done, D2D1::RectF(r.left, pickTop + pickH + 6, r.left + colW * 2 + 60, pickTop + pickH + 24), SFSmall, grey);

        const float rx = r.left + colW * 2 + 80;
        SText(Clock(total * 60.0), D2D1::RectF(rx, r.top + 8, r.right, r.top + 56), SFBig, white);
        pill(D2D1::RectF(rx, r.top + 62, rx + 110, r.top + 94), L"Start", true, StudioAction::FocusStart, total);
        float cx = rx;
        for (int m : {5, 15, 25, 50}) {
            const std::wstring label = std::to_wstring(m) + L" min";
            const float cw = SMeasure(label, SFLabel) + 22;
            const bool on = total == m;
            const D2D1_RECT_F chip = D2D1::RectF(cx, r.top + 104, cx + cw, r.top + 128);
            SFillRound(chip, 12, on ? WithAlpha(purple, 0.35f) : D2D1::ColorF(1, 1, 1, SLerp(0.09f, 0.16f, SHover(StudioAction::FocusPreset, m))));
            SText(label, chip, SFLabel, on ? white : SCol(0xe8e8ee), 1);
            SHit(chip, StudioAction::FocusPreset, m);
            cx += cw + 6;
        }
    }

    // ── Agent cards: a plan to approve, several questions ──
    // Drawn instead of the plain approval card (DrawAgent) when the agent sent a
    // plan (ExitPlanMode) or questions the quick buttons can't answer. Buttons go
    // into g_agentButtons like the plain card's, with an action for the new ones.
    static std::wstring CleanPlanText(const std::wstring& plan) { return bb::studio::CleanPlanText(plan); }

    bool DrawAgentRichCard(const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now) {
        const bb::AgentView& a = state.agent;
        const bool plan = a.phase == bb::AgentPhase::Approval && !a.plan.empty();
        const bool questions = NeedsQuestionCard(a);
        if (!plan) g_planScrollMax = 0.0f;
        if (!plan && !questions) return false;
        EnsureRoundStroke();

        const float L = rect.left + 16.0f, R = rect.right - 16.0f, T = rect.top + 12.0f;
        const D2D1_COLOR_F white = SCol(0xffffff), grey = SCol(0x8e8e93);
        float tx = L;
        if (settings.bunbunMascot) {
            D2D1_POINT_2F fc{};
            float fs = 0.0f;
            bb::mascot::FitToBox(D2D1::RectF(L, T, L + 34, T + 34), &fc, &fs);
            bb::mascot::Draw(target_.Get(), fc, fs, plan ? bb::mascot::Mood::Thinking : bb::mascot::Mood::Alert, now,
                             a.phaseSince, MascotLook(fc));
            tx = L + 44;
        }
        const std::wstring who = a.project.empty() ? a.agent : a.agent + L" \x00B7 " + a.project;
        SText(who, D2D1::RectF(tx, T, R - 60, T + 16), SFSmall, grey);

        auto button = [&](D2D1_RECT_F r, const std::wstring& label, bool primary, int action, int arg,
                          bb::AgentDecision decision, bool enabled) {
            const int index = static_cast<int>(g_agentButtons.size());
            const bool hovered = index == g_agentHoveredButton && enabled;
            const D2D1_COLOR_F fill = primary ? D2D1::ColorF(1, 1, 1, enabled ? (hovered ? 1.0f : 0.92f) : 0.25f)
                                              : D2D1::ColorF(1, 1, 1, hovered ? 0.18f : 0.11f);
            SFillRound(r, 10, fill);
            SText(label, r, SFLabel, primary ? SCol(0x141416) : (enabled ? white : grey), 1);
            if (!enabled) return;
            AgentButton b;
            b.rect = D2D1::RectF(r.left - rect.left, r.top - rect.top, r.right - rect.left, r.bottom - rect.top);
            b.decision = decision;
            b.action = action;
            b.arg = arg;
            g_agentButtons.push_back(b);
        };
        const float bh = 30.0f;
        const float by = rect.bottom - 12.0f - bh;

        if (plan) {
            SText((a.agent.empty() ? std::wstring(L"Claude") : a.agent) + L" has a plan",
                  D2D1::RectF(tx, T + 15, R, T + 36), SFSong, white);
            // The plan, scrollable with the wheel.
            if (g_planScrollFor != a.requestId) { g_planScrollFor = a.requestId; g_planScroll = 0.0f; }
            const D2D1_RECT_F area = D2D1::RectF(L, T + 46, R, by - 10);
            SFillRound(area, 12, D2D1::ColorF(1, 1, 1, 0.06f));
            const std::wstring text = CleanPlanText(a.plan);
            const float innerW = area.right - area.left - 24;
            const float h = SWrapHeight(text, SFAsk, innerW);
            const float viewH = area.bottom - area.top - 16;
            g_planScrollMax = std::max(0.0f, h - viewH);
            g_planScroll = Clamp(g_planScroll, 0.0f, g_planScrollMax);
            target_->PushAxisAlignedClip(D2D1::RectF(area.left, area.top + 6, area.right, area.bottom - 6),
                                         D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (IDWriteTextFormat* f = SFont(SFAsk)) {
                f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                if (ID2D1SolidColorBrush* b = AcquireBrush(SCol(0xe8e8ee)))
                    target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), f,
                                       D2D1::RectF(area.left + 12, area.top + 8 - g_planScroll, area.right - 12,
                                                   area.top + 8 - g_planScroll + h + 4), b);
            }
            target_->PopAxisAlignedClip();
            if (g_planScrollMax > 0.0f) {
                // A thin scroll bar on the right edge.
                const float trackH = area.bottom - area.top - 16;
                const float thumbH = std::max(18.0f, trackH * viewH / h);
                const float ty = area.top + 8 + (trackH - thumbH) * (g_planScroll / g_planScrollMax);
                SFillRound(D2D1::RectF(area.right - 6, ty, area.right - 3, ty + thumbH), 1.5f, D2D1::ColorF(1, 1, 1, 0.3f));
            }
            button(D2D1::RectF(L, by, L + 150, by + bh), L"Answer in terminal", false, 0, 0, bb::AgentDecision::Terminal, true);
            button(D2D1::RectF(R - 228, by, R - 118, by + bh), L"Change it\x2026", false, 1 /*ActPlanChange*/, 0,
                   bb::AgentDecision::Terminal, true);
            button(D2D1::RectF(R - 110, by, R, by + bh), L"Approve", true, 0, 0, bb::AgentDecision::Allow, true);
            return true;
        }

        // Several questions: one at a time, then send them all together.
        SyncQuestions(a);
        const int count = static_cast<int>(a.questions.size());
        const int idx = std::clamp(g_questions.index, 0, count - 1);
        const auto& q = a.questions[static_cast<size_t>(idx)];
        if (count > 1) {
            wchar_t step[24];
            swprintf_s(step, L"%d of %d", idx + 1, count);
            SText(step, D2D1::RectF(R - 60, T, R, T + 16), SFSmall, grey, 2);
        }
        SText(q.text.empty() ? std::wstring(L"Has a question for you") : q.text, D2D1::RectF(tx, T + 15, R, T + 36), SFTitle, white);
        if (!q.header.empty() || q.multi)
            SText(q.multi ? (q.header.empty() ? std::wstring(L"Pick any") : q.header + L" \x00B7 pick any") : q.header,
                  D2D1::RectF(tx, T + 34, R, T + 48), SFSmall, grey);

        float y = T + 54;
        const auto& picks = g_questions.picks[static_cast<size_t>(idx)];
        const std::wstring& typed = g_questions.other[static_cast<size_t>(idx)];
        for (size_t i = 0; i < q.options.size() && i < 6; ++i) {
            const bool on = std::find(picks.begin(), picks.end(), static_cast<int>(i)) != picks.end();
            const D2D1_RECT_F row = D2D1::RectF(L, y, R, y + 26);
            const int index = static_cast<int>(g_agentButtons.size());
            const bool hovered = index == g_agentHoveredButton;
            SFillRound(row, 9, D2D1::ColorF(1, 1, 1, on ? 0.16f : (hovered ? 0.10f : 0.05f)));
            const D2D1_POINT_2F c = D2D1::Point2F(row.left + 16, (row.top + row.bottom) * 0.5f);
            if (ID2D1SolidColorBrush* b = AcquireBrush(on ? SAccent() : D2D1::ColorF(1, 1, 1, 0.45f))) {
                if (q.multi) {
                    const D2D1_RECT_F box = D2D1::RectF(c.x - 6, c.y - 6, c.x + 6, c.y + 6);
                    if (on) target_->FillRoundedRectangle(D2D1::RoundedRect(box, 3, 3), b);
                    else target_->DrawRoundedRectangle(D2D1::RoundedRect(box, 3, 3), b, 1.4f);
                } else if (on) {
                    target_->FillEllipse(D2D1::Ellipse(c, 6, 6), b);
                } else {
                    target_->DrawEllipse(D2D1::Ellipse(c, 6, 6), b, 1.4f);
                }
            }
            if (on) {
                auto P = [](float x, float yy) { return D2D1::Point2F(x, yy); };
                SPoly({P(8.5f, 12), P(11, 14.5f), P(15.5f, 9.5f)}, c, 0.9f, SCol(0xffffff), 1.8f);
            }
            SText(q.options[i], D2D1::RectF(row.left + 30, row.top, row.right - 10, row.bottom), SFBody, white);
            AgentButton b;
            b.rect = D2D1::RectF(row.left - rect.left, row.top - rect.top, row.right - rect.left, row.bottom - rect.top);
            b.action = 3;  // ActToggle
            b.arg = static_cast<int>(i);
            g_agentButtons.push_back(b);
            y += 30;
        }
        if (!typed.empty())
            SText(L"Your answer: \x201C" + typed + L"\x201D", D2D1::RectF(L, by - 18, R, by - 2), SFSmall, SAccent());

        float bx = L;
        if (idx > 0) {
            button(D2D1::RectF(bx, by, bx + 70, by + bh), L"Back", false, 6 /*ActBack*/, 0, bb::AgentDecision::Terminal, true);
            bx += 78;
        }
        button(D2D1::RectF(bx, by, bx + 86, by + bh), L"Other\x2026", false, 5 /*ActOther*/, 0, bb::AgentDecision::Terminal, true);
        button(D2D1::RectF(R - 210, by, R - 104, by + bh), L"In terminal", false, 0, 0, bb::AgentDecision::Terminal, true);
        button(D2D1::RectF(R - 96, by, R, by + bh), idx + 1 < count ? L"Next" : L"Send", true, 4 /*ActNext*/, 0,
               bb::AgentDecision::Terminal, QuestionAnswered(idx));
        return true;
    }

    // ── Calendar agenda (Studio width) ──
    // The calendar page's right-hand column when the page is wide enough: the next
    // meeting from the calendar link, then today's open to-dos with their times.
    void DrawCalendarAgenda(const SharedState& state, D2D1_RECT_F r, double now) {
        (void)now;
        EnsureRoundStroke();
        SFillRound(r, 12, D2D1::ColorF(1, 1, 1, 0.05f));
        float y = r.top + 8;
        SText(L"Agenda", D2D1::RectF(r.left + 10, y, r.right - 8, y + 16), SFLabel, SCol(0x8e8e93));
        y += 20;

        const double epoch = EpochNow();
        auto clockOf = [](double unix) {
            const time_t t = static_cast<time_t>(unix);
            tm lt{};
            localtime_s(&lt, &t);
            wchar_t b[16];
            swprintf_s(b, L"%02d:%02d", lt.tm_hour, lt.tm_min);
            return std::wstring(b);
        };
        auto row = [&](const std::wstring& when, const std::wstring& what, D2D1_COLOR_F dot) {
            if (y + 30 > r.bottom - 4) return false;
            if (ID2D1SolidColorBrush* b = AcquireBrush(dot))
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 10, y + 3, r.left + 13, y + 27), 1.5f, 1.5f), b);
            SText(when, D2D1::RectF(r.left + 19, y, r.right - 8, y + 13), SFSmall, SCol(0x8e8e93));
            SText(what, D2D1::RectF(r.left + 19, y + 13, r.right - 8, y + 29), SFBody, SCol(0xffffff));
            y += 34;
            return true;
        };

        bool any = false;
        if (state.meeting.valid && state.meeting.endsAt > epoch) {
            const bool live = state.meeting.startsAt <= epoch;
            row(live ? L"Now" : clockOf(state.meeting.startsAt), state.meeting.title.empty() ? L"Meeting" : state.meeting.title,
                SAccent());
            any = true;
        }
        // Today's open to-dos: timed ones in order, then the rest.
        SYSTEMTIME st{};
        GetLocalTime(&st);
        tm endTm{};
        endTm.tm_year = st.wYear - 1900; endTm.tm_mon = st.wMonth - 1; endTm.tm_mday = st.wDay + 1; endTm.tm_isdst = -1;
        const double dayEnd = static_cast<double>(mktime(&endTm));
        std::vector<const TodoItem*> todos;
        for (const TodoItem& t : state.todos)
            if (!t.done && (t.due <= 0.0 || t.due < dayEnd)) todos.push_back(&t);
        std::stable_sort(todos.begin(), todos.end(), [](const TodoItem* a, const TodoItem* b) {
            return (a->due > 0 ? a->due : 1e18) < (b->due > 0 ? b->due : 1e18);
        });
        for (const TodoItem* t : todos) {
            const bool overdue = t->due > 0 && t->due < epoch;
            if (!row(t->due > 0 ? (overdue ? L"Overdue \x00B7 " : L"") + clockOf(t->due) : L"To-do", t->text,
                     overdue ? SCol(0xff9f0a) : SAccent()))
                break;
            any = true;
        }
        if (!any) {
            SText(L"Nothing planned", D2D1::RectF(r.left + 10, y + 4, r.right - 8, y + 22), SFBody, SCol(0xaeaeb2));
            SText(L"Ask Bun: \x201Cremind me at 3pm\x2026\x201D", D2D1::RectF(r.left + 10, y + 22, r.right - 8, y + 38),
                  SFSmall, SCol(0x636366));
        }
    }

    // ── Ask Bun as a chat (Studio) ──

    float SWrapHeight(const std::wstring& text, StudioFontId font, float width) {
        IDWriteTextFormat* f = SFont(font);
        if (!f || text.empty()) return 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), f, width, 4000.0f,
                                                    &layout)) || !layout)
            return 0.0f;
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        return m.height;
    }

    D2D1_SIZE_F SMeasureBox(const std::wstring& text, StudioFontId font, float maxWidth) {
        IDWriteTextFormat* f = SFont(font);
        if (!f || text.empty()) return {0.0f, 0.0f};
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), f, maxWidth, 4000.0f,
                                                    &layout)) || !layout)
            return {0.0f, 0.0f};
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        return {std::ceil(m.widthIncludingTrailingWhitespace), std::ceil(m.height)};
    }

    // The answer as one block of text: the title when it adds something, the body,
    // and the example on its own line.
    static std::wstring ChatAnswerText(const ChatTurn& t) {
        const LookupSnapshot& a = t.answer;
        if (a.status == LookupStatus::NoResult) return a.body.empty() ? L"I couldn't find anything for that." : a.body;
        std::wstring text;
        if (!a.title.empty() && a.source != L"Bun" && !EqualsNoCase(a.title, t.question)) text = a.title + L" \x2014 ";
        text += a.body;
        if (!a.example.empty()) text += L"\n\x201C" + a.example + L"\x201D";
        return text;
    }

    void DrawStudioChat(const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now) {
        const LookupSnapshot& lk = state.lookup;
        const float h = rect.bottom - rect.top;
        const float w = rect.right - rect.left;
        if (h < 60.0f || w < 200.0f) return;
        PublishContentGeometry(rect);
        studioOrigin_ = publishedOrigin_;
        EnsureRoundStroke();
        const float alpha = SmoothStep01((h - kStudioChatH * 0.55f) / (kStudioChatH * 0.45f));
        if (alpha <= 0.01f) return;

        // Remember each question and keep its answer current while it arrives.
        const uint64_t seq = g_lookupRequestSeq.load();
        if (lk.status != LookupStatus::Search && !lk.query.empty()) {
            if (g_chat.empty() || g_chat.back().seq != seq) {
                g_chat.push_back({lk.query, lk, seq});
                if (g_chat.size() > 12) g_chat.erase(g_chat.begin());
            } else {
                g_chat.back().answer = lk;
            }
        }
        if (g_chatLastSeq != seq) {
            g_chatLastSeq = seq;
            g_chatScroll = 0.0f;  // New question or answer: jump to latest
        }

        ComPtr<ID2D1Geometry> mask = CreateIslandMaskGeometry(rect, ContentIslandRadius(h), settings.notchStyle);
        ID2D1Layer* layer = AcquireLayer();
        if (mask && layer) {
            target_->PushLayer(D2D1::LayerParameters(rect, mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                      D2D1::IdentityMatrix(), alpha, nullptr, D2D1_LAYER_OPTIONS_NONE),
                               layer);
        }
        SHit(rect, StudioAction::ChatPanel);

        // Top: a pill with the mascot and who's answering, and New chat.
        const float topY = rect.top + 8.0f;
        const bool aiBusy = lk.talking || BrainBusyForCurrentCard();
        const std::wstring who = g_compose.kind ? g_compose.label
                                 : settings.bunbunAssistant ? L"Bun \x00B7 on this PC" : L"Quick Lookup";
        const float whoW = SMeasure(who, SFLabel);
        const D2D1_RECT_F pill = D2D1::RectF(rect.left + 16, topY, rect.left + 16 + 26 + whoW + 14, topY + 26);
        SFillRound(pill, 13, D2D1::ColorF(1, 1, 1, 0.08f));
        if (ID2D1SolidColorBrush* b = AcquireBrush(SCol(0x5ee07a))) {
            const D2D1_POINT_2F dot = D2D1::Point2F(pill.left + 14, (pill.top + pill.bottom) * 0.5f);
            if (ID2D1SolidColorBrush* g = AcquireBrush(WithAlpha(SCol(0x5ee07a), 0.3f))) target_->FillEllipse(D2D1::Ellipse(dot, 6, 6), g);
            target_->FillEllipse(D2D1::Ellipse(dot, 3.5f, 3.5f), b);
        }
        SText(who, D2D1::RectF(pill.left + 26, pill.top, pill.right - 8, pill.bottom), SFLabel, SCol(0xe8e8ef));
        if (!g_chat.empty()) {
            const float nw = SMeasure(L"New chat", SFLabel) + 22;
            const D2D1_RECT_F nb = D2D1::RectF(rect.right - 14 - nw, topY + 2, rect.right - 14, topY + 24);
            SFillRound(nb, 11, D2D1::ColorF(1, 1, 1, SLerp(0.08f, 0.14f, SHover(StudioAction::ChatNew))));
            SText(L"New chat", nb, SFLabel, SCol(0xc9c9d2), 1);
            SHit(nb, StudioAction::ChatNew);
        }

        // Input pill at the bottom, with the caret and a send button.
        const LookupUiState& ui = g_lookupUi;
        const D2D1_RECT_F input = D2D1::RectF(rect.left + 12, rect.bottom - 56, rect.right - 12, rect.bottom - 10);
        SFillRound(input, 23, D2D1::ColorF(1, 1, 1, 0.09f));
        if (ID2D1SolidColorBrush* rim = AcquireBrush(D2D1::ColorF(1, 1, 1, ui.open ? 0.22f : 0.07f)))
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(input.left + 0.5f, input.top + 0.5f, input.right - 0.5f, input.bottom - 0.5f), 22.5f, 22.5f), rim, 1.0f);
        // The mascot sits at the left of the box: listening while you type,
        // thinking while Bun works, talking while the local AI answers.
        float textL = input.left + 18;
        if (settings.bunbunMascot) {
            DrawAskBunMascot(D2D1::RectF(input.left + 8, input.top + 3, input.left + 46, input.bottom - 3), lk, 0.0f, now);
            textL = input.left + 52;
        }
        const float textR = input.right - 50;
        if (ui.text.empty()) {
            SText(g_compose.kind ? L"Type your message and press Enter" : aiBusy ? L"Bun is answering\x2026" : L"Ask Bun anything, or tell it what to do",
                  D2D1::RectF(textL, input.top, textR, input.bottom), SFInput, SCol(0x8a8a96));
        } else {
            if (ui.selectAll) {
                const float sw = std::min(SMeasure(ui.text, SFInput), textR - textL);
                SFillRound(D2D1::RectF(textL - 2, input.top + 12, textL + sw + 2, input.bottom - 12), 4, D2D1::ColorF(0.3f, 0.55f, 1.0f, 0.45f));
            }
            SText(ui.text, D2D1::RectF(textL, input.top, textR, input.bottom), SFInput, SCol(0xffffff));
        }
        const double since = now - ui.caretResetAt;
        if (ui.open && std::fmod(since, LookupLayout::kCaretPeriod) < LookupLayout::kCaretOn) {
            const float cx = std::min(textL + SMeasure(ui.text.substr(0, std::min(ui.caret, ui.text.size())), SFInput), textR);
            SFillRound(D2D1::RectF(cx, input.top + 14, cx + 1.6f, input.bottom - 14), 0.8f, SCol(0xffffff));
        }
        const D2D1_POINT_2F send = D2D1::Point2F(input.right - 25, (input.top + input.bottom) * 0.5f);
        if (ID2D1SolidColorBrush* b = AcquireBrush(D2D1::ColorF(1, 1, 1, ui.text.empty() ? 0.25f : 0.95f)))
            target_->FillEllipse(D2D1::Ellipse(send, 16, 16), b);
        auto P = [](float x, float y) { return D2D1::Point2F(x, y); };
        SPoly({P(12, 17), P(12, 7)}, send, 1.0f, SCol(0x141416), 2.2f);
        SPoly({P(7.5f, 11), P(12, 6.5f), P(16.5f, 11)}, send, 1.0f, SCol(0x141416), 2.2f);

        // Messages, newest at the bottom; older ones scroll off the top.
        const float msgTop = rect.top + 42, msgBottom = input.top - 10;
        const float viewH = msgBottom - msgTop;
        const float msgL = rect.left + 20, msgR = rect.right - 18;
        const float maxChatW = msgR - msgL;
        g_chatChips.clear();
        if (g_chat.empty()) {
            SText(L"Hi! What can I do for you?", D2D1::RectF(msgL, msgTop + 6, msgR, msgTop + 36), SFSong, SCol(0xffffff));
            SText(L"Open apps, set timers, control music, ask about the weather, look up a word\x2026",
                  D2D1::RectF(msgL, msgTop + 36, msgR, msgTop + 56), SFBody, SCol(0x8a8a96));
            // Chips: your recent questions first, then a few ideas.
            for (const auto& r : state.lookupRecent) {
                if (g_chatChips.size() >= 2) break;
                g_chatChips.push_back(r.query);
            }
            for (const wchar_t* idea : {L"Weather tomorrow", L"Timer 25 min", L"Open Spotify", L"Battery"}) {
                if (g_chatChips.size() >= 4) break;
                if (std::find(g_chatChips.begin(), g_chatChips.end(), idea) == g_chatChips.end()) g_chatChips.push_back(idea);
            }
            float cx = msgL - 4;
            const float cy = msgBottom - 30;
            for (size_t i = 0; i < g_chatChips.size(); ++i) {
                const float cw = SMeasure(g_chatChips[i], SFLabel) + 28;
                if (cx + cw > msgR) break;
                const D2D1_RECT_F chip = D2D1::RectF(cx, cy, cx + cw, cy + 28);
                SFillRound(chip, 14, D2D1::ColorF(1, 1, 1, SLerp(0.10f, 0.16f, SHover(StudioAction::ChatChip, static_cast<int>(i)))));
                SText(g_chatChips[i], chip, SFLabel, SCol(0xffffff), 1);
                SHit(chip, StudioAction::ChatChip, static_cast<int>(i));
                cx += cw + 8;
            }
        } else {
            struct MeasuredTurn {
                D2D1_SIZE_F userTextSize{};
                float userBubbleW = 0.0f;
                float userBubbleH = 0.0f;

                bool waiting = false;
                bool tagged = false;
                std::wstring ansText;
                D2D1_SIZE_F ansTextSize{};
                float ansBubbleW = 0.0f;
                float ansBubbleH = 0.0f;

                float totalTurnH = 0.0f;
            };

            std::vector<MeasuredTurn> measured;
            measured.reserve(g_chat.size());
            constexpr float kQAGap = 6.0f;
            constexpr float kTurnGap = 12.0f;
            float totalH = 0.0f;

            for (size_t k = 0; k < g_chat.size(); ++k) {
                const ChatTurn& t = g_chat[k];
                MeasuredTurn mt;

                // User question bubble (right side)
                const float maxUserBubbleW = maxChatW * 0.72f;
                const float maxUserTextW = std::max(60.0f, maxUserBubbleW - 24.0f);
                mt.userTextSize = SMeasureBox(t.question, SFAsk, maxUserTextW);
                mt.userBubbleW = std::min(maxUserBubbleW, mt.userTextSize.width + 24.0f);
                mt.userBubbleH = std::max(28.0f, mt.userTextSize.height + 14.0f);

                // Bun answer bubble (left side)
                const bool latest = (k + 1 == g_chat.size());
                mt.waiting = (t.answer.status == LookupStatus::Loading) ||
                             (latest && aiBusy && t.answer.body.empty());

                if (mt.waiting) {
                    mt.ansBubbleW = 54.0f;
                    mt.ansBubbleH = 30.0f;
                } else {
                    mt.ansText = ChatAnswerText(t);
                    mt.tagged = !t.answer.source.empty() && t.answer.source != L"Bun";
                    const float maxAnsBubbleW = maxChatW * 0.88f;
                    const float maxAnsTextW = std::max(60.0f, maxAnsBubbleW - 24.0f);
                    mt.ansTextSize = SMeasureBox(mt.ansText, SFAnswer, maxAnsTextW);
                    const float tagExtra = mt.tagged ? 16.0f : 0.0f;
                    const float tagW = mt.tagged ? SMeasure(t.answer.source, SFSmall) + 24.0f : 0.0f;
                    mt.ansBubbleW = std::min(maxAnsBubbleW, std::max(mt.ansTextSize.width + 24.0f, tagW));
                    mt.ansBubbleH = std::max(32.0f, mt.ansTextSize.height + 16.0f + tagExtra);
                }

                mt.totalTurnH = mt.userBubbleH + kQAGap + mt.ansBubbleH;
                totalH += mt.totalTurnH;
                if (k + 1 < g_chat.size()) totalH += kTurnGap;
                measured.push_back(mt);
            }

            g_chatScrollMax = std::max(0.0f, totalH - viewH);
            g_chatScroll = Clamp(g_chatScroll, 0.0f, g_chatScrollMax);
            const float scrollOffset = (totalH > viewH) ? (g_chatScrollMax - g_chatScroll) : 0.0f;
            float curY = msgTop - scrollOffset;

            target_->PushAxisAlignedClip(D2D1::RectF(rect.left, msgTop, rect.right, msgBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            for (size_t k = 0; k < g_chat.size(); ++k) {
                const MeasuredTurn& m = measured[k];
                const ChatTurn& t = g_chat[k];

                // 1) User bubble (right side)
                const float userTop = curY;
                const float userBottom = curY + m.userBubbleH;
                if (userBottom >= msgTop - 4.0f && userTop <= msgBottom + 4.0f) {
                    const float userR = msgR;
                    const float userL = userR - m.userBubbleW;
                    const D2D1_RECT_F ub = D2D1::RectF(userL, userTop, userR, userBottom);
                    SFillRound(ub, 14.0f, D2D1::ColorF(0.18f, 0.19f, 0.23f, 0.95f));
                    if (ID2D1SolidColorBrush* rim = AcquireBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f))) {
                        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ub.left + 0.5f, ub.top + 0.5f, ub.right - 0.5f, ub.bottom - 0.5f), 13.5f, 13.5f), rim, 1.0f);
                    }
                    if (IDWriteTextFormat* f = SFont(SFAsk)) {
                        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                        if (ID2D1SolidColorBrush* b = AcquireBrush(D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f))) {
                            target_->DrawTextW(t.question.c_str(), static_cast<UINT32>(t.question.size()), f,
                                               D2D1::RectF(ub.left + 12.0f, ub.top + 7.0f, ub.right - 12.0f, ub.bottom - 7.0f),
                                               b, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                        }
                    }
                }
                curY += m.userBubbleH + kQAGap;

                // 2) Bun answer bubble (left side)
                const float ansTop = curY;
                const float ansBottom = curY + m.ansBubbleH;
                if (ansBottom >= msgTop - 4.0f && ansTop <= msgBottom + 4.0f) {
                    const float ansL = msgL;
                    const float ansR = ansL + m.ansBubbleW;
                    const D2D1_RECT_F ab = D2D1::RectF(ansL, ansTop, ansR, ansBottom);
                    if (m.waiting) {
                        SFillRound(ab, 14.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f));
                        if (ID2D1SolidColorBrush* rim = AcquireBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f))) {
                            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ab.left + 0.5f, ab.top + 0.5f, ab.right - 0.5f, ab.bottom - 0.5f), 13.5f, 13.5f), rim, 1.0f);
                        }
                        const float dotCenterY = (ab.top + ab.bottom) * 0.5f;
                        for (int d = 0; d < 3; ++d) {
                            const float ph = static_cast<float>(std::fmod(now * 1.0 + d * 0.15, 1.0));
                            const float lift = std::sin(ph * 3.14159f) * 3.5f;
                            const D2D1_COLOR_F dotCol = WithAlpha(SCol(0x5ee07a), 0.35f + 0.65f * std::sin(ph * 3.14159f));
                            if (ID2D1SolidColorBrush* b = AcquireBrush(dotCol)) {
                                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ab.left + 16.0f + d * 11.0f, dotCenterY - lift), 3.0f, 3.0f), b);
                            }
                        }
                        g_layoutDirty = true;  // keep the dots animating
                    } else {
                        const bool isNoResult = (t.answer.status == LookupStatus::NoResult);
                        const D2D1_COLOR_F bgCol = isNoResult ? D2D1::ColorF(0.35f, 0.12f, 0.10f, 0.40f)
                                                              : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f);
                        const D2D1_COLOR_F rimCol = isNoResult ? D2D1::ColorF(1.0f, 0.4f, 0.35f, 0.25f)
                                                               : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f);
                        SFillRound(ab, 14.0f, bgCol);
                        if (ID2D1SolidColorBrush* rim = AcquireBrush(rimCol)) {
                            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ab.left + 0.5f, ab.top + 0.5f, ab.right - 0.5f, ab.bottom - 0.5f), 13.5f, 13.5f), rim, 1.0f);
                        }

                        float textTop = ab.top + 8.0f;
                        if (m.tagged) {
                            SText(t.answer.source, D2D1::RectF(ab.left + 12.0f, textTop, ab.right - 12.0f, textTop + 14.0f), SFSmall, SCol(0x9a9aa8));
                            textTop += 15.0f;
                        }

                        IDWriteTextFormat* f = SFont(SFAnswer);
                        if (f) {
                            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                            const D2D1_COLOR_F tc = isNoResult ? SCol(0xffb4a8) : SCol(0xffffff);
                            if (ID2D1SolidColorBrush* b = AcquireBrush(tc)) {
                                target_->DrawTextW(m.ansText.c_str(), static_cast<UINT32>(m.ansText.size()), f,
                                                   D2D1::RectF(ab.left + 12.0f, textTop, ab.right - 12.0f, ab.bottom - 7.0f),
                                                   b, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                            }
                        }
                    }
                }
                curY += m.ansBubbleH + kTurnGap;
            }

            target_->PopAxisAlignedClip();

            // Subtle scrollbar thumb on the right
            if (g_chatScrollMax > 0.0f) {
                const float trackH = viewH;
                const float thumbH = std::max(22.0f, trackH * (viewH / (viewH + g_chatScrollMax)));
                const float thumbY = msgTop + (trackH - thumbH) * (1.0f - (g_chatScroll / g_chatScrollMax));
                const D2D1_RECT_F thumbRect = D2D1::RectF(rect.right - 7.0f, thumbY, rect.right - 4.0f, thumbY + thumbH);
                SFillRound(thumbRect, 1.5f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.28f));
            }
        }

        if (mask && layer) target_->PopLayer();
    }

    // ── Idle eyes (Bunbun.IdleFace = eyes) ──
    // The island as a face: two glowing eyes, 17 units apart at a 32-unit bar,
    // drawn at 0.62 of their design size (a 5 x 8 ellipse). Mint by default (or
    // Bunbun.EyeColor); orange and pulsing while an agent works, steady orange
    // when it needs you, green when done, red on an error, and a cross red when
    // poked too often. They follow the pointer by up to 2.2 x 0.8 units and blink
    // for 140 ms every 2.5-6 s, now and then a wink.
    static constexpr uint32_t kEyeColors[6] = {0xe3ffd4, 0xffffff, 0x8fd3ff, 0xffb3d1, 0xd4b3ff, 0xfff2a8};

    void DrawIdleEyes(const SharedState& state, D2D1_POINT_2F c, float height, bb::mascot::Mood mood, double now,
                      double moodSince) {
        EnsureRoundStroke();
        const float u = Clamp(height / 32.0f, 0.4f, 3.0f);  // one design unit at this size
        const float k = 0.62f * u;                         // eye design units

        // Colour and shape follow the agents first, then the mascot's own mood.
        D2D1_COLOR_F col = SCol(kEyeColors[ClampInt(g_settings.eyeColor, 0, 5)]);
        enum class Shape { Normal, Focus, Curious, Surprised, Happy, Closed, Dizzy, Sad } shape = Shape::Normal;
        switch (g_settings.eyeStatusColors ? state.agent.phase : bb::AgentPhase::None) {
            case bb::AgentPhase::Thinking:
            case bb::AgentPhase::Working: {
                const float pulse = 0.3f + 0.7f * (0.5f + 0.5f * std::cos(static_cast<float>(now) / 0.2f));
                col = WithAlpha(SCol(0xff9a1f), pulse);
                shape = Shape::Focus;
                break;
            }
            case bb::AgentPhase::Question: col = SCol(0xff9a1f); shape = Shape::Curious; break;
            case bb::AgentPhase::Approval: col = SCol(0xff9a1f); shape = Shape::Surprised; break;
            case bb::AgentPhase::Finished: col = SCol(0x22e05a); shape = Shape::Happy; break;
            case bb::AgentPhase::Error: col = SCol(0xff3b3b); break;
            default: break;
        }
        switch (mood) {  // the mascot's own reactions win while they last
            case bb::mascot::Mood::Confused: shape = Shape::Dizzy; break;
            case bb::mascot::Mood::Sleepy: shape = Shape::Closed; break;
            case bb::mascot::Mood::Happy: shape = Shape::Happy; break;
            case bb::mascot::Mood::Sad: shape = Shape::Sad; break;
            case bb::mascot::Mood::Alert:
                if (state.agent.phase == bb::AgentPhase::None) { col = SCol(0xff6b6b); shape = Shape::Surprised; }
                break;
            default: break;
        }

        // Pointer follow, straight from the cursor (no easing, like the original).
        float dx = 0.0f, dy = 0.0f;
        if (bb::mascot::GetStyle().eyesFollow) {
            POINT p{};
            if (GetCursorPos(&p) && ScreenToClient(hwnd_, &p)) {
                dx = Clamp((p.x - c.x) / (320.0f * u), -1.0f, 1.0f) * 2.2f * u;
                dy = Clamp((p.y - c.y) / (200.0f * u), -1.0f, 1.0f) * 0.8f * u;
            }
        }

        // Blink: 140 ms, the first at 2.5 s, then every 2.5-6 s; one in 40 is a wink.
        bool blink = false, wink = false;
        if (shape == Shape::Normal || shape == Shape::Focus) {
            static double s_next = -1.0, s_until = -1.0;
            static bool s_wink = false;
            if (s_next < 0.0) s_next = now + 2.5;
            if (now >= s_next) {
                s_until = now + 0.14;
                const double r = std::fmod(std::sin(now * 12.9898) * 43758.5453, 1.0);
                s_next = now + 2.5 + std::fabs(r) * 3.5;
                s_wink = std::fabs(std::fmod(r * 97.0, 1.0)) < 0.025;
            }
            if (now < s_until) { blink = !s_wink; wink = s_wink; g_layoutDirty = true; }
        }

        const float sep = 8.5f * u;
        const float bob = mood == bb::mascot::Mood::Music
                              ? std::sin(static_cast<float>(now) * 6.2831853f / 0.5f) * 1.0f * u : 0.0f;
        const float line = 3.0f * k;

        for (int side = -1; side <= 1; side += 2) {
            const D2D1_POINT_2F e = D2D1::Point2F(c.x + side * sep + dx, c.y + dy + bob);
            // A faint rim of light around a solid eye (TwoCode's small shadow), not a halo.
            if (ID2D1SolidColorBrush* b = AcquireBrush(WithAlpha(col, col.a * 0.16f)))
                target_->FillEllipse(D2D1::Ellipse(e, 5.0f * k + 1.2f * u, 8.0f * k + 1.2f * u), b);
            ID2D1SolidColorBrush* ink = AcquireBrush(col);
            if (!ink) return;
            Shape s = shape;
            if (blink || (wink && side > 0)) s = Shape::Closed;
            switch (s) {
                case Shape::Normal:
                    target_->FillEllipse(D2D1::Ellipse(e, 5.0f * k, 8.0f * k), ink);
                    break;
                case Shape::Focus:
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(e.x, e.y + 1.0f * k), 5.5f * k, 4.0f * k), ink);
                    break;
                case Shape::Curious:
                    target_->FillEllipse(D2D1::Ellipse(e, 6.0f * k, 9.5f * k), ink);
                    break;
                case Shape::Surprised:
                    target_->FillEllipse(D2D1::Ellipse(e, 7.0f * k, 7.0f * k), ink);
                    break;
                case Shape::Happy:  // ^: the top of a circle around (0, 3), 1.15 pi to 1.85 pi
                    SArc(D2D1::Point2F(e.x, e.y + 3.0f * k), 5.0f * k, 3.6128f, 5.8119f, col, line);
                    break;
                case Shape::Closed:  // u: the bottom of a circle around (0, -2); a wink is a flat line
                    if (wink && side > 0 && !blink) SLine(D2D1::Point2F(e.x - 5 * k, e.y + 2 * k), D2D1::Point2F(e.x + 5 * k, e.y + 2 * k), col, line);
                    else SArc(D2D1::Point2F(e.x, e.y - 2.0f * k), 5.0f * k, 0.4712f, 2.6704f, col, line);
                    break;
                case Shape::Sad: {
                    D2D1_MATRIX_3X2_F old;
                    target_->GetTransform(&old);
                    const D2D1_POINT_2F se = D2D1::Point2F(e.x, e.y + 2.0f * k);
                    target_->SetTransform(D2D1::Matrix3x2F::Rotation(side * -25.8f, se) * old);  // 0.45 rad
                    target_->FillEllipse(D2D1::Ellipse(se, 4.5f * k, 6.5f * k), ink);
                    target_->SetTransform(old);
                    break;
                }
                case Shape::Dizzy: {
                    // A two-turn spiral (radius 0.55 per radian) spinning a turn every 0.88 s.
                    const float turn = static_cast<float>(now - moodSince) / 0.14f * side;
                    ComPtr<ID2D1PathGeometry> spiral;
                    ComPtr<ID2D1GeometrySink> sink;
                    if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&spiral)) && SUCCEEDED(spiral->Open(&sink))) {
                        sink->BeginFigure(e, D2D1_FIGURE_BEGIN_HOLLOW);
                        for (float a = 0.3f; a < 12.566f; a += 0.3f) {
                            const float r = a * 0.55f * k;
                            sink->AddLine(D2D1::Point2F(e.x + std::cos(a + turn) * r, e.y + std::sin(a + turn) * r));
                        }
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        sink->Close();
                        target_->DrawGeometry(spiral.Get(), ink, 2.2f * k, studioStroke_.Get());
                    }
                    g_layoutDirty = true;
                    break;
                }
            }
        }
        if (g_settings.eyeStatusColors &&
            (state.agent.phase == bb::AgentPhase::Working || state.agent.phase == bb::AgentPhase::Thinking))
            g_layoutDirty = true;  // the pulse needs frames

        // Sleeping: little z's drifting up.
        if (shape == Shape::Closed && mood == bb::mascot::Mood::Sleepy) {
            const float t = static_cast<float>(std::fmod(now, 2.4) / 2.4);
            SText(L"z", D2D1::RectF(c.x + sep + 4 * u, c.y - 10 * u - t * 6 * u, c.x + sep + 14 * u, c.y - 2 * u - t * 6 * u),
                  SFSmall, WithAlpha(SCol(0xc6e994), 1.0f - t));
        }
    }

    // ── Tuck-away line ──
    void DrawTuckLine(const SharedState& state, D2D1_RECT_F rect, double now) {
        D2D1_COLOR_F c = D2D1::ColorF(1, 1, 1, 0.55f);
        const bb::AgentPhase p = state.agent.phase;
        if (p == bb::AgentPhase::Working || p == bb::AgentPhase::Thinking) c = SCol(0xff9a1f);
        else if (p == bb::AgentPhase::Finished) c = SCol(0x22e05a);
        else if (p == bb::AgentPhase::Error) c = SCol(0xff3b3b);
        else if (state.media.playing) c = D2D1::ColorF(1, 1, 1, 0.85f);
        const float grow = SmoothStep01(Clamp(static_cast<float>(now - g_tuckedAt) / 0.22f, 0.0f, 1.0f));
        const float cx = (rect.left + rect.right) * 0.5f;
        const float w = 56.0f * g_settings.sizeScale * (0.4f + 0.6f * grow);
        const float top = rect.top;
        const float h = 4.0f * g_settings.sizeScale;
        SFillRound(D2D1::RectF(cx - w * 0.5f - 2, top - 1, cx + w * 0.5f + 2, top + h + 1), h, WithAlpha(c, c.a * 0.25f));
        SFillRound(D2D1::RectF(cx - w * 0.5f, top, cx + w * 0.5f, top + h), h * 0.5f, c);
    }
