// Bunbun: Renderer: construction, device setup and shutdown.
// Split out of island_core.cpp; included inside class Renderer at the
// original position, so declaration order is unchanged. Not a standalone file.

    bool Initialize(HWND hwnd) {
        hwnd_ = hwnd;

        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       __uuidof(ID2D1Factory),
                                       reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0.0f, 0.0f,
            D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);

        hr = d2dFactory_->CreateDCRenderTarget(&props, &target_);
        if (FAILED(hr)) {
            return false;
        }

        const Settings initialSettings = GetSettingsCopy();
        EnsureTextFormats(initialSettings.sizeScale, initialSettings.fontFamily,
                          initialSettings.textScale);

        return CreateBackingBitmap(520, 140);
    }

    bool Render(const SharedState& state, const Settings& settings, const Activity& primary,
                const std::optional<Activity>& secondary, float width, float height,
                float nudge, bool hover, bool pinned, double now) {
        EnsureTextFormats(settings.sizeScale, settings.fontFamily, settings.textScale);
        // Bunbun: the agent card's buttons belong to this frame only; DrawAgent records
        // them again whenever it draws. They used to outlive the card, so a pill that
        // took its place (a water reminder's Drank) sent its clicks to a stale Reply.
        g_agentButtons.clear();

        width = std::max(width, 0.0f);
        if (UsesFixedStage(settings)) {
            // A pill can never be bigger than the stage it lives in.
            width = std::min(width, kStageContentWidth * settings.sizeScale);
            height = std::min(height, kStageContentHeight * settings.sizeScale);
        }

        const StageLayout stage = ComputeStageLayout(settings, width, height);
        const int pixelWidth = stage.width;
        const int pixelHeight = stage.height;

        const bool flushTop = settings.notchStyle || settings.borderMergedMode;
        const float top = stage.pillTop + (flushTop ? std::max(0.0f, nudge) : nudge);
        const float left = stage.pillLeft;

        const BYTE alpha = static_cast<BYTE>(Clamp(settings.pillOpacity, 0.35f, 1.0f) * 255.0f);
        const bool sizeChanged = pixelWidth != bitmapWidth_ || pixelHeight != bitmapHeight_;
        const bool moved = stage.origin.x != lastOrigin_.x || stage.origin.y != lastOrigin_.y;

        // Full repaint when anything about the window itself changed, plus once a
        // second as a safety net (sleep/resume, display changes).
        const bool fullPaint = !kUseDirtyRect || !stageValid_ || sizeChanged || moved ||
                               alpha != lastAlpha_ || (now - lastFullPaint_) > 1.0;

        if (sizeChanged) {
            if (!CreateBackingBitmap(pixelWidth, pixelHeight)) {
                return false;
            }
        } else if (g_layoutDirty.exchange(false)) {
            upkeepPending_ = true;
        }
        if (upkeepPending_) {
            if (now - lastUpkeepAt_ > 0.25) {
                upkeepPending_ = false;
                lastUpkeepAt_ = now;
                ApplyOverlayZOrder(hwnd_, pixelWidth, pixelHeight);
            } else {
                g_layoutDirty = true;  // retry once the throttle expires
            }
        }

        // Region that can contain pixels this frame: the pill plus its shadow and
        // nudge slack, unioned with where it was last frame so the old pixels get erased.
        RECT bounds;
        bounds.left = std::max(0, static_cast<int>(std::floor(left - kRenderPadX - 4.0f)));
        bounds.top = std::max(0, static_cast<int>(std::floor(top - kRenderPadY - 12.0f)));
        bounds.right = std::min(pixelWidth, static_cast<int>(std::ceil(left + width + kRenderPadX + 4.0f)));
        bounds.bottom = std::min(pixelHeight, static_cast<int>(std::ceil(top + height + kRenderPadY + 12.0f)));

        RECT dirty = {0, 0, pixelWidth, pixelHeight};
        if (!fullPaint) {
            UnionRect(&dirty, &lastBounds_, &bounds);
        }

        if (IsRectEmpty(&dirty)) {
            dirty = RECT{0, 0, pixelWidth, pixelHeight};
        }

        // Bind only the region being repainted. Binding the whole stage made D2D copy
        // all of it in at BeginDraw and out at EndDraw every frame, even for a small pill.
        // The bound rect's top-left becomes (0,0); the translation restores stage
        // coordinates, and every draw routine composes with the current transform.
        RECT rc = dirty;
        HRESULT hr = target_->BindDC(memDc_, &rc);
        if (FAILED(hr)) {
            return false;
        }

        pillCount_ = 0;  // DrawPill records each pill it draws for the backdrop windows

        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(dirty.left),
                                                            -static_cast<float>(dirty.top)));
        // Bunbun: Studio click spots are kept in window coordinates, so SHit adds this
        // back after the live transform (it changes with every frame's dirty region).
        bindOffset_ = D2D1::Point2F(static_cast<float>(dirty.left), static_cast<float>(dirty.top));
        // Clear() honours the active clip, so this only wipes the dirty region.
        target_->PushAxisAlignedClip(
            D2D1::RectF(static_cast<float>(dirty.left), static_cast<float>(dirty.top),
                        static_cast<float>(dirty.right), static_cast<float>(dirty.bottom)),
            D2D1_ANTIALIAS_MODE_ALIASED);
        target_->Clear(D2D1::ColorF(0, 0.0f));
        BeginFrameResources();

        EnsureBrushes(settings, state, now);
        settingsOpacity_ = settings.pillOpacity;

        const bool gameMetricsPresent = primary.kind == IslandKind::Idle && GameOverlayShown(settings.gameOverlay);
        // True while the island is collapsing to nothing (auto-hide / fullscreen).
        islandHiding_ = primary.width < 1.0f && primary.height < 1.0f && !secondary;

        const float hoverScale = (settings.expandOnHover && ((hover && !gameMetricsPresent) || pinned)) ? 1.025f : 1.0f;
        const float scale = hoverScale;
        hoverScale_ = scale;
        EdgeLight::Note(state, settings, hover, hover || pinned, primary.kind, now);  // Bunbun: plays on the chosen triggers


        if (width >= 2.0f && height >= 2.0f) {
            if (secondary) {
                // Bunbun: one dock. Both activities live inside a single island
                // surface, split by a hairline in the middle of what used to be
                // the gap between two pills. The springs still animate the total
                // width, so the island grows and shrinks as one shape.
                const float gap = 12.0f * settings.sizeScale;
                const float maxH = std::max(primary.height, secondary->height);
                const float cxAll = left + width * 0.5f;
                const float cyAll = top + maxH * 0.5f;
                const D2D1_RECT_F all = D2D1::RectF(cxAll - width * scale * 0.5f, cyAll - maxH * scale * 0.5f,
                                                    cxAll + width * scale * 0.5f, cyAll + maxH * scale * 0.5f);
                DrawIslandSurface(all, primary.kind, settings);

                // Sections share the full height so their content centres in the island.
                const float splitX = left + primary.width + gap * 0.5f;
                DrawPill(state, settings, primary, D2D1::RectF(left, top, left + primary.width, top + maxH),
                         scale, now, false);
                mutedBrush_->SetOpacity(0.18f);
                target_->DrawLine(D2D1::Point2F(splitX, cyAll - maxH * 0.28f), D2D1::Point2F(splitX, cyAll + maxH * 0.28f),
                                  mutedBrush_.Get(), 1.0f);
                mutedBrush_->SetOpacity(0.50f);
                DrawPill(state, settings, *secondary,
                         D2D1::RectF(left + primary.width + gap, top, left + primary.width + gap + secondary->width,
                                     top + maxH),
                         scale, now, false);
            } else if (g_bunSlotWidth > 0.5f && width > g_bunSlotWidth + 40.0f) {
                // Bunbun: one surface, Bun in the slot on the left, the activity
                // (clock or music pill) in the rest.
                const float cx = left + width * 0.5f, cy = top + height * 0.5f;
                DrawIslandSurface(D2D1::RectF(cx - width * scale * 0.5f, cy - height * scale * 0.5f,
                                              cx + width * scale * 0.5f, cy + height * scale * 0.5f),
                                  primary.kind, settings);
                // Bunbun: with nothing beside it (Bunbun.IdleStripItems = nothing), the
                // face sits in the middle of the island, like a creature in the notch.
                // Bunbun: with a status ring, the face stays in the middle too, the
                // ring on its right and the time (or what's chosen) on its left.
                const bool centred = primary.kind == IslandKind::Idle && FaceCentred(state, settings, now);
                const bool faceOnly = primary.kind == IslandKind::Idle && !settings.idleShowClock &&
                                      !(settings.weather && settings.idleShowWeather);
                const float slotLeft = (faceOnly || centred) ? left + (width - g_bunSlotWidth) * 0.5f : left;
                DrawBunSlot(state, settings, primary, D2D1::RectF(slotLeft, top, slotLeft + g_bunSlotWidth, top + height), now);
                faceGapOn_ = centred;
                faceGapL_ = slotLeft;
                faceGapR_ = slotLeft + g_bunSlotWidth;
                DrawPill(state, settings, primary,
                         D2D1::RectF(centred ? left : left + g_bunSlotWidth * 0.82f, top, left + width, top + height), scale, now, false);
                faceGapOn_ = false;
            } else {
                DrawPill(state, settings, primary,
                         D2D1::RectF(left, top, left + width, top + height), scale, now);
            }
        }
        DrawEdgeLight(settings, now);  // Bunbun: over the content, along the outline

        target_->PopAxisAlignedClip();
        hr = target_->EndDraw();
        if (FAILED(hr)) {
            stageValid_ = false;
            return false;
        }

        POINT src = {0, 0};
        SIZE size = {bitmapWidth_, bitmapHeight_};
        POINT dst = stage.origin;  // constant while the pill animates

        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = alpha;
        blend.AlphaFormat = AC_SRC_ALPHA;

        UPDATELAYEREDWINDOWINFO info = {};
        info.cbSize = sizeof(info);
        info.pptDst = &dst;
        info.psize = &size;
        info.hdcSrc = memDc_;
        info.pptSrc = &src;
        info.pblend = &blend;
        info.dwFlags = ULW_ALPHA;
        info.prcDirty = fullPaint ? nullptr : &dirty;  // only this rect is re-composited

        const double ulwStart = QpcMs();
        const bool presented = UpdateLayeredWindowIndirect(hwnd_, &info) != FALSE;
        g_ulwMsTotal += QpcMs() - ulwStart;
        if (presented) {
            stageValid_ = true;
            lastOrigin_ = stage.origin;
            lastBounds_ = bounds;
            lastAlpha_ = alpha;
            if (fullPaint) lastFullPaint_ = now;
            g_pillClientRect = PillClientRect{left, top, left + width, top + height, true};
            // Bunbun: the same pixels on the other displays.
            Mirrors::Present(memDc_, settings,
                             RECT{static_cast<LONG>(std::floor(left)), static_cast<LONG>(std::floor(top)),
                                  static_cast<LONG>(std::ceil(left + width)), static_cast<LONG>(std::ceil(top + height))},
                             size, alpha, now, height > 60.0f * settings.sizeScale);
        } else {
            stageValid_ = false;
        }

        if (presented) {
            DwmBackdrop::Sync(hwnd_, stage.origin, pillRects_, pillRadii_, pillCount_,
                              settings.notchStyle, settings.backdropMaterial, pillBgColor_,
                              settings.backdropTint);
        }
        return presented;
    }

    // Bunbun: Separate displays. Draws an island at rest into this renderer's own
    // bitmap, for another display: no window, no click geometry, no shared tab state
    // (offscreen_). `rest` is in pixels at settings.sizeScale; `bunSlot` the mascot's
    // slot. Returns the bitmap and where the pill is in it.
    bool offscreen_ = false;
    bool restTuck_ = false;  // Bunbun: this display's island is folded to a line (RenderRestIsland)
    bool RenderOffscreen(const SharedState& state, const Settings& settings, const Activity& rest, float bunSlot,
                         double now, HDC* dc, SIZE* size, RECT* pill) {
        offscreen_ = true;
        EnsureTextFormats(settings.sizeScale, settings.fontFamily, settings.textScale);
        const float width = rest.width + bunSlot, height = rest.height;
        if (width < 2.0f || height < 2.0f) return false;
        const float pad = std::ceil(26.0f * settings.sizeScale);
        const int w = static_cast<int>(std::ceil(width + pad * 2)), h = static_cast<int>(std::ceil(height + pad * 2));
        if (!CreateBackingBitmap(w, h)) return false;
        const RECT rc{0, 0, w, h};
        if (FAILED(target_->BindDC(memDc_, &rc))) return false;
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        bindOffset_ = D2D1::Point2F(0, 0);
        target_->Clear(D2D1::ColorF(0, 0.0f));
        BeginFrameResources();
        EnsureBrushes(settings, state, now);
        settingsOpacity_ = settings.pillOpacity;
        islandHiding_ = false;
        hoverScale_ = 1.0f;
        const float left = pad, top = pad;
        const float savedSlot = g_bunSlotWidth;  // read by the drawing below
        g_bunSlotWidth = bunSlot;
        // The real island's click spots: DrawPill clears and re-records them, and this
        // runs after the real island drew (it broke every Studio button, the Timers modes too).
        std::vector<StudioHit> savedHits = std::move(g_studioHits);
        const uint64_t savedHitsFrame = g_studioHitsFrame;
        g_studioHits.clear();
        if (bunSlot > 0.5f && width > bunSlot + 40.0f) {
            DrawIslandSurface(D2D1::RectF(left, top, left + width, top + height), rest.kind, settings);
            const bool centred = rest.kind == IslandKind::Idle && FaceCentred(state, settings, now);
            const bool faceOnly = rest.kind == IslandKind::Idle && !settings.idleShowClock &&
                                  !(settings.weather && settings.idleShowWeather);
            const float slotLeft = (faceOnly || centred) ? left + (width - bunSlot) * 0.5f : left;
            DrawBunSlot(state, settings, rest, D2D1::RectF(slotLeft, top, slotLeft + bunSlot, top + height), now);
            faceGapOn_ = centred;
            faceGapL_ = slotLeft;
            faceGapR_ = slotLeft + bunSlot;
            DrawPill(state, settings, rest, D2D1::RectF(centred ? left : left + bunSlot * 0.82f, top, left + width, top + height), 1.0f, now, false);
            faceGapOn_ = false;
        } else {
            DrawPill(state, settings, rest, D2D1::RectF(left, top, left + width, top + height), 1.0f, now);
        }
        g_bunSlotWidth = savedSlot;
        g_studioHits = std::move(savedHits);
        g_studioHitsFrame = savedHitsFrame;
        if (FAILED(target_->EndDraw())) return false;
        *dc = memDc_;
        *size = SIZE{w, h};
        *pill = RECT{static_cast<LONG>(left), static_cast<LONG>(top),
                     static_cast<LONG>(std::lround(left + width)), static_cast<LONG>(std::lround(top + height))};
        return true;
    }

    void Shutdown() {
        ReleaseCachedResources();
        marqueeTitleCache_.layout.Reset();
        marqueeArtistCache_.layout.Reset();
        marqueeAlbumCache_.layout.Reset();
        marqueeClipboardCache_.layout.Reset();
        marqueeNotificationCache_.layout.Reset();
        marqueeAgentCache_.layout.Reset();
        scratchColorBrush_.Reset();
        agentInkBrush_.Reset();
        lyricsActiveTextFormat_.Reset();
        lyricsActiveFitTextFormat_.Reset();
        karaokeCache_.layout.Reset();

        artBitmap_.Reset();
        notificationIconBitmap_.Reset();
        mediaSourceIconBitmap_.Reset();
        clipboardIconBitmap_.Reset();
        clipboardImageBitmap_.Reset();
        accentBrush_.Reset();
        redBrush_.Reset();
        textBrush_.Reset();
        mutedBrush_.Reset();
        tintBrush_.Reset();
        shadowBrush_.Reset();
        micDotBrush_.Reset();
        micGlowBrush_.Reset();
        camDotBrush_.Reset();
        camGlowBrush_.Reset();
        weatherDescFormat_.Reset();
        micGlowBrush_.Reset();
        camDotBrush_.Reset();
        camGlowBrush_.Reset();
        target_.Reset();
        textFormat_.Reset();
        smallTextFormat_.Reset();
        boldTextFormat_.Reset();
        hugeTextFormat_.Reset();
        clockFormat_.Reset();
        iconFormat_.Reset();
        mediaPlayIconFormat_.Reset();
        mediaNavIconFormat_.Reset();
        idleTextFormat_.Reset();
        calDayLargeFormat_.Reset();
        calGridFormat_.Reset();
        dwriteFactory_.Reset();
        skipTriFull_.Reset();
        skipTriNotch_.Reset();
        titleOld_.clear();
        titleNew_.clear();
        titleAnimActive_ = false;
        roundJoinStyle_.Reset();
        artFlipOld_.Reset();
        pillArtOld_.Reset();
        pillArtLast_.Reset();
        pillArtStart_ = -1.0;
        pillArtInit_ = false;
        swapArtist_.prevCache.layout.Reset();
        swapAlbum_.prevCache.layout.Reset();
        d2dFactory_.Reset();

        if (oldBitmap_) {
            SelectObject(memDc_, oldBitmap_);
            oldBitmap_ = nullptr;
        }
        if (dib_) {
            DeleteObject(dib_);
            dib_ = nullptr;
        }
        if (memDc_) {
            DeleteDC(memDc_);
            memDc_ = nullptr;
        }
    }

    // Measures the collapsed idle strip so the render loop can size the island to
    // the text it is actually about to paint, instead of the old fixed 96/170px.
    // DrawIdleDashboard calls this too and lays the slots out from the same
    // numbers, so the pill can never be sized for a clock width the painter is
    // not using.
    // Bunbun: the status ring beside the clock (Bunbun.StatusRing). Its fill (0..1)
    // and colour, or false for no ring: the focus timer (left of the session), today's
    // water against the goal, or the to-dos ticked off. Auto: focus while a timer
    // runs, else water when it's tracked (Water in Timers, or its reminders).
    static constexpr float kStatusRingSize = 15.0f, kStatusRingGap = 8.0f;
    // Bunbun: the face in the middle of the small island (a status ring is showing and
    // the mascot sits on the island). While DrawPill draws the strip around it, the
    // face's slot is faceGapL_..faceGapR_.
    bool faceGapOn_ = false;
    float faceGapL_ = 0.0f, faceGapR_ = 0.0f;
    bool FaceCentred(const SharedState& state, const Settings& settings, double now) {
        float f = 0.0f;
        D2D1_COLOR_F c{};
        return settings.bunbunMascot && settings.bunbunMascotOnIsland && StatusRing(state, settings, now, &f, &c);
    }
    bool StatusRing(const SharedState& state, const Settings& settings, double now, float* frac, D2D1_COLOR_F* col) {
        const int mode = settings.statusRing;
        if (mode == 0) return false;
        const TimerSnapshot& tm = state.timer;
        if ((mode == 1 || mode == 4) && tm.active && tm.totalSeconds > 0) {
            const double left = tm.running ? std::max(0.0, tm.endsAt - now) : tm.remainingAtPause;
            *frac = Clamp(static_cast<float>(left / tm.totalSeconds), 0.0f, 1.0f);
            *col = tm.isBreak ? D2D1::ColorF(0x30d158) : material_.accent;
            return true;
        }
        if ((mode == 1 && (settings.waterPage || settings.water.reminders)) || mode == 2) {
            const WaterSnapshot& w = state.water;
            if (w.goalMl <= 0) return false;
            *frac = Clamp(static_cast<float>(w.takenMl) / w.goalMl, 0.0f, 1.0f);
            *col = D2D1::ColorF(0x5ac8fa);
            return true;
        }
        if (mode == 3 && !state.todos.empty()) {
            int done = 0;
            for (const TodoItem& t : state.todos) done += t.done ? 1 : 0;
            *frac = static_cast<float>(done) / static_cast<float>(state.todos.size());
            *col = D2D1::ColorF(0x30d158);
            return true;
        }
        return false;
    }

    IdleStripMetrics MeasureIdleStrip(const SharedState& state, const Settings& settings,
                                      double now) {
        // Idempotent and cheap when nothing changed, but necessary here: textScale
        // drives the idle font size, so measuring before the formats are rebuilt
        // would size the pill for the previous Text size setting.
        EnsureTextFormats(settings.sizeScale, settings.fontFamily, settings.textScale);

        IdleStripMetrics metrics;
        IDWriteTextFormat* fmt = idleTextFormat_ ? idleTextFormat_.Get() : smallTextFormat_.Get();

        SYSTEMTIME local = {};
        GetLocalTime(&local);
        const std::wstring clock = FormatIslandTime(local, settings.clockFollowSystem,
                                                    settings.use24HourClock, settings.showSeconds);
        // Bunbun: Bunbun.IdleStripItems can leave out the time and/or the weather.
        metrics.clockWidth = settings.idleShowClock
            ? MeasureTextWidthCached(settings.idleName.empty() ? WidestDigitForm(clock) : settings.idleName, fmt, idleClockWidthCache_) : 0.0f;

        metrics.hasWeather = settings.weather && settings.idleShowWeather;
        if (metrics.hasWeather) {
            // Mirrors the label DrawIdleDashboard builds, including the no-data
            // placeholder, so the reserved slot matches what gets drawn.
            const bool hasData = state.weather.hasData && (now - state.weather.lastUpdated < 3600.0);
            wchar_t label[32] = {};
            if (hasData) {
                std::wstring icon = L"\U0001F321\uFE0F";
                std::wstring desc = state.weather.weatherDesc;
                GetWeatherIconAndText(state.weather.weatherCode, icon, desc);
                swprintf_s(label, L"%s %.0f\x00B0", icon.c_str(), state.weather.temperature);
            } else {
                wcscpy_s(label, ARRAYSIZE(label), L"\U0001F321\uFE0F --\x00B0");
            }
            metrics.weatherWidth =
                MeasureTextWidthCached(WidestDigitForm(label), fmt, idleWeatherWidthCache_);
        }

        metrics.privacyReserve = PrivacyDotsReserve(state, settings);  // Bunbun: one slot per lit dot
        metrics.hasPrivacy = metrics.privacyReserve > 0.0f;
        float ringFrac = 0.0f;
        D2D1_COLOR_F ringCol{};
        // With no clock ("just the face") the ring stands beside the face on its own.
        if (StatusRing(state, settings, now, &ringFrac, &ringCol))
            metrics.ringWidth = (metrics.clockWidth > 0.0f ? kStatusRingGap : 0.0f) + kStatusRingSize;  // Bunbun

        float total = IdleStripLayout::kPadX * 2.0f + metrics.clockWidth + metrics.ringWidth;
        if (metrics.hasWeather) {
            total += metrics.clockWidth > 0.0f  // Bunbun: no divider without a clock
                ? IdleStripLayout::kSlotGap * 2.0f + IdleStripLayout::kDividerWidth + metrics.weatherWidth
                : metrics.weatherWidth;
        }
        // Bunbun: with a ring, the face sits in the middle: the time (or weather, or
        // your text) on its left, the ring on its right, both sides as wide as the
        // wider one so the face stays centred. The render loop adds the face's slot.
        if (metrics.ringWidth > 0.0f && FaceCentred(state, settings, now)) {
            const float leftSide = total - IdleStripLayout::kPadX * 2.0f - metrics.ringWidth;
            const float side = std::max(leftSide, kStatusRingSize) + kStatusRingGap;
            total = IdleStripLayout::kPadX * 2.0f + side * 2.0f;
        }
        total += metrics.privacyReserve;

        total = std::ceil(total / IdleStripLayout::kWidthQuantum) * IdleStripLayout::kWidthQuantum;
        metrics.totalWidth = Clamp(total, IdleStripLayout::kMinWidth, IdleStripLayout::kMaxWidth);
        return metrics;
    }

    // Collapsed media pill, with the optional clock. Returns content-space
    // sizes; the render loop multiplies by sizeScale like every other collapsed
    // width. Measured in the widest-digit form so the width does not wobble as
    // the digits change.
    MediaPillMetrics MeasureMediaPill(const SharedState& state, const Settings& settings) {
        (void)state;
        MediaPillMetrics metrics;
        if (!settings.mediaPillClock) {
            return metrics;
        }

        EnsureTextFormats(settings.sizeScale, settings.fontFamily, settings.textScale);
        IDWriteTextFormat* fmt = idleTextFormat_ ? idleTextFormat_.Get() : smallTextFormat_.Get();

        SYSTEMTIME local = {};
        GetLocalTime(&local);
        const std::wstring clock = FormatIslandTime(local, settings.clockFollowSystem,
                                                    settings.use24HourClock, settings.showSeconds);
        metrics.clockWidth =
            MeasureTextWidthCached(WidestDigitForm(clock), fmt, idleClockWidthCache_);

        float section = MediaPillLayout::kClockPadLeft + metrics.clockWidth +
                        MediaPillLayout::kSlotGap + MediaPillLayout::kDividerWidth;
        section = std::ceil(section / MediaPillLayout::kWidthQuantum) * MediaPillLayout::kWidthQuantum;
        metrics.sectionWidth = section;
        metrics.totalWidth = MediaPillLayout::kBaseWidth + section;
        return metrics;
    }

   private:
    // Keyed on the string alone. EnsureTextFormats invalidates both caches
    // whenever it rebuilds the formats, so a cached width can never outlive the
    // font size it was measured at -- comparing the IDWriteTextFormat pointer
    // would not be enough, since a rebuilt format can land on the freed address.
    struct TextWidthCache {
        std::wstring key;
        float width = 0.0f;
        bool valid = false;
    };
    TextWidthCache idleClockWidthCache_;
    TextWidthCache idleWeatherWidthCache_;

    float MeasureTextWidthCached(const std::wstring& text, IDWriteTextFormat* fmt,
                                 TextWidthCache& cache) {
        if (cache.valid && cache.key == text) {
            return cache.width;
        }

        float width = 0.0f;
        if (fmt && dwriteFactory_ && !text.empty()) {
            ComPtr<IDWriteTextLayout> layout;
            // Effectively unbounded wrap width: the idle formats are NO_WRAP, and
            // we want the natural advance, not a wrapped block.
            if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                    text.c_str(), static_cast<UINT32>(text.size()), fmt,
                    4096.0f, IdleStripLayout::kHeight, &layout)) &&
                layout) {
                DWRITE_TEXT_METRICS tm = {};
                if (SUCCEEDED(layout->GetMetrics(&tm))) {
                    width = std::max(tm.width, tm.widthIncludingTrailingWhitespace);
                }
            }
        }

        cache.key = text;
        cache.width = width;
        cache.valid = true;
        return width;
    }

