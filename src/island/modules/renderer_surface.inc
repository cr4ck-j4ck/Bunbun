// Bunbun: Renderer: backing bitmap, brushes, shape cache, shadows and masks.
// Split out of island_core.cpp; included inside class Renderer at the
// original position, so declaration order is unchanged. Not a standalone file.

    bool CreateBackingBitmap(int width, int height) {
        const int logicalW = width;
        const int logicalH = height;
        // Expand/collapse changes the needed size almost every frame. Reuse the
        // existing DIB whenever it is big enough; UpdateLayeredWindow only reads
        // the (0,0,width,height) region, so a larger DIB is fine.
        if (dib_ && width <= bitmapCapW_ && height <= bitmapCapH_) {
            bitmapWidth_ = width;
            bitmapHeight_ = height;
            return true;
        }

        // Round up with headroom so growing animations rarely reallocate.
        width = ((width + 63) / 64) * 64 + 64;
        height = ((height + 31) / 32) * 32 + 32;

        if (oldBitmap_) {
            SelectObject(memDc_, oldBitmap_);
            oldBitmap_ = nullptr;
        }
        if (dib_) {
            DeleteObject(dib_);
            dib_ = nullptr;
        }
        if (!memDc_) {
            HDC screen = GetDC(nullptr);
            memDc_ = CreateCompatibleDC(screen);
            ReleaseDC(nullptr, screen);
            if (!memDc_) {
                return false;
            }
        }

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        dib_ = CreateDIBSection(memDc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib_) {
            return false;
        }

        oldBitmap_ = static_cast<HBITMAP>(SelectObject(memDc_, dib_));
        bitmapCapW_ = width;
        bitmapCapH_ = height;
        bitmapWidth_ = logicalW;
        bitmapHeight_ = logicalH;
        return true;
    }

    float lastFontScale_ = 0.0f;
    float lastTextScale_ = 0.0f;
    std::wstring lastFontFamily_;
    // textScale is an independent typography multiplier (the Text size setting):
    // sizeScale magnifies the whole island via a transform, whereas this changes
    // only the type, so the island can stay compact while the clock and labels
    // get bigger.
    void EnsureTextFormats(float scale, const std::wstring& fontFamily, float textScale) {
        if (std::abs(scale - lastFontScale_) < 0.001f && fontFamily == lastFontFamily_ &&
            std::abs(textScale - lastTextScale_) < 0.001f) {
            return;
        }
        lastTextScale_ = textScale;
        const float ts = Clamp(textScale, 0.7f, 1.6f);

        // Every format below is about to be recreated at a new size, so any
        // width measured against the old ones is stale.
        idleClockWidthCache_.valid = false;
        idleWeatherWidthCache_.valid = false;

        textFormat_ = nullptr;
        smallTextFormat_ = nullptr;
        clockFormat_ = nullptr;
        boldTextFormat_ = nullptr;
        hugeTextFormat_ = nullptr;
        iconFormat_ = nullptr;
        mediaPlayIconFormat_ = nullptr;
        mediaNavIconFormat_ = nullptr;
        idleTextFormat_ = nullptr;
        calDayLargeFormat_ = nullptr;
        calGridFormat_ = nullptr;
        timeDashboardFormat_ = nullptr;
        dateDashboardFormat_ = nullptr;

        const wchar_t* defaultDisplay = L"Segoe UI Variable Display";
        const wchar_t* defaultSmall = L"Segoe UI Variable Small";
        const wchar_t* mainFamily = fontFamily.empty() ? defaultDisplay : fontFamily.c_str();
        const wchar_t* smallFamily = fontFamily.empty() ? defaultSmall : fontFamily.c_str();

        auto createFormat = [&](const wchar_t* family, const wchar_t* fallback,
                                DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                                DWRITE_FONT_STRETCH stretch, float size,
                                ComPtr<IDWriteTextFormat>& out) {
            HRESULT hr = dwriteFactory_->CreateTextFormat(
                family, nullptr, weight, style, stretch, size, L"", &out);
            if (FAILED(hr) || !out) {
                if (fallback && wcscmp(family, fallback) != 0) {
                    hr = dwriteFactory_->CreateTextFormat(
                        fallback, nullptr, weight, style, stretch, size, L"", &out);
                }
                if (FAILED(hr) || !out) {
                    dwriteFactory_->CreateTextFormat(
                        L"Segoe UI", nullptr, weight, style, stretch, size, L"", &out);
                }
            }
        };

        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 13.5f * ts, textFormat_);
        createFormat(smallFamily, defaultSmall,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 11.0f * ts, smallTextFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 18.0f * ts, clockFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 12.0f * ts, boldTextFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 42.0f * ts, hugeTextFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 13.0f * ts, idleTextFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 63.0f * ts, calDayLargeFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 14.4f * ts, calGridFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 56.0f * ts, timeDashboardFormat_);
        createFormat(mainFamily, defaultDisplay,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, 16.0f * ts, dateDashboardFormat_);

        usingFluentIcons_ = false;
        ComPtr<IDWriteFontCollection> sysFonts;
        if (dwriteFactory_ && SUCCEEDED(dwriteFactory_->GetSystemFontCollection(&sysFonts, FALSE)) && sysFonts) {
            UINT32 fontIdx = 0;
            BOOL fontFound = FALSE;
            if (SUCCEEDED(sysFonts->FindFamilyName(L"Segoe Fluent Icons", &fontIdx, &fontFound)) && fontFound) {
                usingFluentIcons_ = true;
            }
        }

        const wchar_t* iconFontFamily = usingFluentIcons_ ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";

        HRESULT hrIcon = dwriteFactory_->CreateTextFormat(
            iconFontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"", &iconFormat_);
        if (FAILED(hrIcon) || !iconFormat_) {
            dwriteFactory_->CreateTextFormat(
                L"Segoe MDL2 Assets", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"", &iconFormat_);
        }

        HRESULT hrPlay = dwriteFactory_->CreateTextFormat(
            iconFontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"", &mediaPlayIconFormat_);
        if (FAILED(hrPlay) || !mediaPlayIconFormat_) {
            dwriteFactory_->CreateTextFormat(
                L"Segoe MDL2 Assets", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"", &mediaPlayIconFormat_);
        }

        HRESULT hrNav = dwriteFactory_->CreateTextFormat(
            iconFontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", &mediaNavIconFormat_);
        if (FAILED(hrNav) || !mediaNavIconFormat_) {
            dwriteFactory_->CreateTextFormat(
                L"Segoe MDL2 Assets", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", &mediaNavIconFormat_);
        }

        if (textFormat_) {
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (smallTextFormat_) {
            smallTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (boldTextFormat_) {
            boldTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            // Match every other centered format: without this the text hugs the
            // top of its layout rect, which left the weather dashboard's city /
            // icon / temperature visually "stuck" near the top of the island.
            boldTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (hugeTextFormat_) {
            hugeTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            hugeTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            hugeTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (clockFormat_) {
            clockFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            clockFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            clockFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (idleTextFormat_) {
            idleTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            idleTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            idleTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (calDayLargeFormat_) {
            calDayLargeFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            calDayLargeFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            calDayLargeFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (calGridFormat_) {
            calGridFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            calGridFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            calGridFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (timeDashboardFormat_) {
            timeDashboardFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            timeDashboardFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            timeDashboardFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (dateDashboardFormat_) {
            dateDashboardFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            dateDashboardFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            dateDashboardFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (iconFormat_) {
            iconFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (mediaPlayIconFormat_) {
            mediaPlayIconFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            mediaPlayIconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            mediaPlayIconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (mediaNavIconFormat_) {
            mediaNavIconFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            mediaNavIconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            mediaNavIconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        if (fontFamily != lastFontFamily_) {
            marqueeTitleCache_.layout.Reset();
            marqueeArtistCache_.layout.Reset();
            marqueeAlbumCache_.layout.Reset();
            marqueeClipboardCache_.layout.Reset();
            marqueeNotificationCache_.layout.Reset();
            weatherDescFormat_.Reset();
            weatherDescFormatSize_ = -1.0f;
        }

        // The lyrics formats are built lazily in DrawLyricsDashboard; throw them
        // away so they pick up a new font / text size.
        lyricsActiveTextFormat_.Reset();
        lyricsActiveTextFormatSize_ = -1.0f;
        lyricsActiveFitTextFormat_.Reset();
        lyricsActiveFitTextFormatSize_ = -1.0f;
        karaokeCache_.layout.Reset();
        karaokeCache_.valid = false;

        // Collapsed lyric layouts were built from the old font; rebuild them.
        collapsedLyricCur_ = CollapsedLyricLine{};
        collapsedLyricPrev_ = CollapsedLyricLine{};
        collapsedLyricIdx_ = -2;

        lastFontScale_ = scale;
        lastFontFamily_ = fontFamily;
    }

    void EnsureBrushes(const Settings& settings, const SharedState& state, double now) {
        D2D1_COLOR_F targetAccent = settings.customAccent;
        if (settings.accentMode == AccentMode::System) {
            targetAccent = GetSystemAccentColor();
        } else if (settings.accentMode == AccentMode::Auto && state.media.art) {
            // Contrast-corrected when the art was decoded, against the theme of that
            // moment. Bunbun: again here, so a theme changed since (a light one, say)
            // doesn't keep an accent tuned for the old background.
            targetAccent = EnsureContrastAgainstBackground(state.media.art->sampledAccent, settings.pillBgColor);
        } else {
            // A custom or system accent was never checked against the island's
            // own background. That did not matter while every theme was dark,
            // but it does now that one is light: the default cyan sits at about
            // 1.6:1 on Porcelain. Corrected before the smoothing lerp so the
            // value is stable rather than re-derived every frame.
            targetAccent = EnsureContrastAgainstBackground(targetAccent, settings.pillBgColor);
        }
        if (!offscreen_) PublishAccent(targetAccent);  // Bunbun: the settings app wears it too

        // Smooth accent transition: exponential lerp toward target, ~300ms half-life.
        // On the very first frame (lastAccentTime_ < 0) snap immediately so there's
        // no fade-in from the default color on startup.
        if (lastAccentTime_ < 0.0) {
            currentAccent_ = targetAccent;
            lastAccentTime_ = now;
        } else {
            const double dt = std::max(0.0, std::min(now - lastAccentTime_, 0.1));  // cap at 100ms
            lastAccentTime_ = now;
            // k = 1 - exp(-dt / tau), tau ≈ 0.18s → reaches 95% in ~350ms
            const float k = 1.0f - static_cast<float>(std::exp(-dt / 0.18));
            currentAccent_.r += (targetAccent.r - currentAccent_.r) * k;
            currentAccent_.g += (targetAccent.g - currentAccent_.g) * k;
            currentAccent_.b += (targetAccent.b - currentAccent_.b) * k;
            currentAccent_.a = 1.0f;
        }

        const D2D1_COLOR_F accent = currentAccent_;

        if (!accentBrush_) target_->CreateSolidColorBrush(accent, &accentBrush_);
        else accentBrush_->SetColor(accent);

        if (!redBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.27f, 0.27f, 1.0f), &redBrush_);

        // Use user-configured text colors.
        D2D1_COLOR_F primary = settings.textPrimaryColor;
        primary.a = 0.98f;
        if (!textBrush_) target_->CreateSolidColorBrush(primary, &textBrush_);
        else textBrush_->SetColor(primary);

        D2D1_COLOR_F secondary = settings.textSecondaryColor;
        secondary.a = 0.90f;
        if (!mutedBrush_) target_->CreateSolidColorBrush(secondary, &mutedBrush_);
        else mutedBrush_->SetColor(secondary);

        // Keep the authored alpha. An 8-digit #RRGGBBAA background is how a
        // translucent island is requested independently of the global pill
        // transparency slider; DrawPillSurface combines the two.
        pillBgColor_ = settings.pillBgColor;

        D2D1_COLOR_F tintColor = D2D1::ColorF(0.010f, 0.010f, 0.012f, settings.tintOpacity);
        if (!tintBrush_) target_->CreateSolidColorBrush(tintColor, &tintBrush_);
        else tintBrush_->SetColor(tintColor);

        if (!shadowBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.70f), &shadowBrush_);

        BuildMaterialTokens(settings);
    }

    // ── Premium material system ─────────────────────────────────────────────
    // Every surface in the island is built from one small token set so the whole
    // UI shares a single visual language. Tokens are recomputed once per frame
    // from the resolved theme, and adapt to background luminance so a light
    // custom background gets dark separators instead of washed-out white ones.
    struct MaterialTokens {
        D2D1_COLOR_F base{};          // the pill's own fill
        D2D1_COLOR_F raised{};        // inner cards, chips, wells
        D2D1_COLOR_F raisedStrong{};  // pressed / active chips
        D2D1_COLOR_F hairline{};      // 1px separators between content
        D2D1_COLOR_F stroke{};        // outer contour
        D2D1_COLOR_F shadow{};        // drop shadow tint
        D2D1_COLOR_F textPrimary{};
        D2D1_COLOR_F textSecondary{};
        D2D1_COLOR_F textTertiary{};
        D2D1_COLOR_F accent{};
        D2D1_COLOR_F accentSoft{};
        bool onDark = true;
        float opacity = 1.0f;
    };

    static D2D1_COLOR_F MixColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
        return D2D1::ColorF(a.r + (b.r - a.r) * t,
                            a.g + (b.g - a.g) * t,
                            a.b + (b.b - a.b) * t,
                            a.a + (b.a - a.a) * t);
    }

    static D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float alpha) {
        c.a = Clamp(alpha, 0.0f, 1.0f);
        return c;
    }

    // ── Cached Direct2D resources ───────────────────────────────────────────
    // Nothing below is created per frame once it has warmed up. Render thread only.
    struct LinearGradientSlot { ComPtr<ID2D1LinearGradientBrush> brush; uint64_t key = 0; };
    struct RadialGradientSlot { ComPtr<ID2D1RadialGradientBrush> brush; uint64_t key = 0; };

    struct ShapeKey {
        float l = 0, t = 0, r = 0, b = 0, radius = 0;
        bool notch = false;
        bool operator==(const ShapeKey& o) const {
            return l == o.l && t == o.t && r == o.r && b == o.b &&
                   radius == o.radius && notch == o.notch;
        }
    };
    struct ShapeEntry {
        ShapeKey key;
        ComPtr<ID2D1Geometry> geom;
        uint64_t lastUsed = 0;
    };
    static constexpr size_t kShapeCacheSize = 32;

    static uint32_t PackColor8(const D2D1_COLOR_F& c) {
        auto q = [](float v) { return static_cast<uint32_t>(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
    }

    static uint64_t HashStops(const D2D1_GRADIENT_STOP* s, UINT32 n) {
        uint64_t h = 1469598103934665603ull;
        for (UINT32 i = 0; i < n; ++i) {
            h = (h ^ PackColor8(s[i].color)) * 1099511628211ull;
            h = (h ^ static_cast<uint64_t>(s[i].position * 1000.0f + 0.5f)) * 1099511628211ull;
        }
        return h;
    }

    // Call once per frame, right after BeginDraw().
    void BeginFrameResources() {
        brushPoolUsed_ = 0;
        layerPoolUsed_ = 0;
    }

    // Returns a brush that is unique for the rest of this frame. Colour and opacity are
    // reset, so a previous user's SetOpacity() can never leak into the next one.
    ID2D1SolidColorBrush* AcquireBrush(const D2D1_COLOR_F& colorIn) {
        const D2D1_COLOR_F color = studioInk_ ? StudioInk(colorIn) : colorIn;  // Bunbun: Studio on a light island
        if (brushPoolUsed_ >= brushPool_.size()) {
            ComPtr<ID2D1SolidColorBrush> b;
            if (FAILED(target_->CreateSolidColorBrush(color, &b)) || !b) {
                return nullptr;
            }
            brushPool_.push_back(std::move(b));
        }
        ID2D1SolidColorBrush* brush = brushPool_[brushPoolUsed_++].Get();
        brush->SetColor(color);
        brush->SetOpacity(1.0f);
        return brush;
    }

    // Same idea for layers. Each Acquire is distinct within a frame, so nested
    // PushLayer calls are safe, and the layer's backing store is kept between frames.
    ID2D1Layer* AcquireLayer() {
        if (layerPoolUsed_ >= layerPool_.size()) {
            ComPtr<ID2D1Layer> l;
            if (FAILED(target_->CreateLayer(&l)) || !l) {
                return nullptr;
            }
            layerPool_.push_back(std::move(l));
        }
        return layerPool_[layerPoolUsed_++].Get();
    }

    void SetPersistentBrush(ComPtr<ID2D1SolidColorBrush>& brush, const D2D1_COLOR_F& c) {
        if (!brush) {
            target_->CreateSolidColorBrush(c, &brush);
        } else {
            brush->SetColor(c);
        }
    }

    // The collection + brush are rebuilt only when the stops change; start/end move per call.
    ID2D1LinearGradientBrush* UseLinearGradient(LinearGradientSlot& slot,
                                                const D2D1_GRADIENT_STOP* stops, UINT32 count,
                                                D2D1_POINT_2F start, D2D1_POINT_2F end) {
        const uint64_t key = HashStops(stops, count);
        if (!slot.brush || slot.key != key) {
            slot.brush.Reset();
            ComPtr<ID2D1GradientStopCollection> coll;
            if (FAILED(target_->CreateGradientStopCollection(stops, count, D2D1_GAMMA_2_2,
                                                             D2D1_EXTEND_MODE_CLAMP, &coll)) || !coll) {
                return nullptr;
            }
            if (FAILED(target_->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(start, end), coll.Get(), &slot.brush)) ||
                !slot.brush) {
                slot.brush.Reset();
                return nullptr;
            }
            slot.key = key;
        }
        slot.brush->SetStartPoint(start);
        slot.brush->SetEndPoint(end);
        return slot.brush.Get();
    }

    ID2D1RadialGradientBrush* UseRadialGradient(RadialGradientSlot& slot,
                                                const D2D1_GRADIENT_STOP* stops, UINT32 count,
                                                D2D1_POINT_2F center, float rx, float ry) {
        const uint64_t key = HashStops(stops, count);
        if (!slot.brush || slot.key != key) {
            slot.brush.Reset();
            ComPtr<ID2D1GradientStopCollection> coll;
            if (FAILED(target_->CreateGradientStopCollection(stops, count, D2D1_GAMMA_2_2,
                                                             D2D1_EXTEND_MODE_CLAMP, &coll)) || !coll) {
                return nullptr;
            }
            if (FAILED(target_->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(0, 0), rx, ry),
                    coll.Get(), &slot.brush)) ||
                !slot.brush) {
                slot.brush.Reset();
                return nullptr;
            }
            slot.key = key;
        }
        slot.brush->SetCenter(center);
        slot.brush->SetGradientOriginOffset(D2D1::Point2F(0, 0));
        slot.brush->SetRadiusX(rx);
        slot.brush->SetRadiusY(ry);
        return slot.brush.Get();
    }

    // Rounded-rect or notch geometry, cached by rect + radius. Returns a ref-counted
    // pointer, so a caller holding it is safe even if the entry is evicted meanwhile.
    ComPtr<ID2D1Geometry> GetShapeGeometry(D2D1_RECT_F rect, float radius, bool notch) {
        const ShapeKey key{rect.left, rect.top, rect.right, rect.bottom, radius, notch};
        ShapeEntry* victim = &shapeCache_[0];
        for (ShapeEntry& e : shapeCache_) {
            if (e.geom && e.key == key) {
                e.lastUsed = ++shapeClock_;
                return e.geom;
            }
            if (e.lastUsed < victim->lastUsed) {
                victim = &e;  // empty entries have lastUsed == 0, so they go first
            }
        }

        ComPtr<ID2D1Geometry> geom;
        if (notch) {
            ComPtr<ID2D1PathGeometry> path = CreateNotchGeometry(rect, radius);
            if (path) path.As(&geom);
        }
        if (!geom) {  // not a notch, or the notch failed: same fallback as before
            ComPtr<ID2D1RoundedRectangleGeometry> rr;
            if (SUCCEEDED(d2dFactory_->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(rect, radius, radius), &rr)) && rr) {
                rr.As(&geom);
            }
        }
        if (!geom) return nullptr;

        victim->key = key;
        victim->geom = geom;
        victim->lastUsed = ++shapeClock_;
        return geom;
    }

    void ReleaseCachedResources() {
        // Bunbun: the mascot's per-thread brush and skin bitmaps belong to this target too.
        bb::mascot::ReleaseThreadResources();
        brushPool_.clear();
        layerPool_.clear();
        brushPoolUsed_ = 0;
        layerPoolUsed_ = 0;
        raisedBrush_.Reset();
        raisedStrongBrush_.Reset();
        depthGrad_ = LinearGradientSlot{};
        trackGrad_ = LinearGradientSlot{};
        bloomGrad_ = RadialGradientSlot{};
        swapGroupLayer_.Reset();
        for (ShapeEntry& e : shapeCache_) e = ShapeEntry{};
    }

    void BuildMaterialTokens(const Settings& settings) {
        MaterialTokens t;
        t.opacity = settingsOpacity_;
        t.base = pillBgColor_;

        // Light surfaces need dark scrims/edges, dark surfaces need light ones.
        const bool onDark = RelativeLuminance(t.base) < 0.45;
        t.onDark = onDark;

        const D2D1_COLOR_F white = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        const D2D1_COLOR_F black = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
        const D2D1_COLOR_F lift = onDark ? white : black;

        // Cards carry a slightly stronger fill than before. They used to be
        // outlined with a hairline ring, and losing that ring is what the fill
        // now has to compensate for so a card still reads as a distinct surface.
        t.raised = WithAlpha(lift, onDark ? 0.085f : 0.062f);
        t.raisedStrong = WithAlpha(lift, onDark ? 0.150f : 0.105f);
        t.hairline = WithAlpha(lift, onDark ? 0.100f : 0.085f);
        t.shadow = WithAlpha(black, 0.55f);

        t.stroke = settings.contourBorderColor;
        t.accent = currentAccent_;
        t.accentSoft = WithAlpha(currentAccent_, 0.18f);

        t.textPrimary = WithAlpha(settings.textPrimaryColor, 0.98f);
        t.textSecondary = WithAlpha(settings.textSecondaryColor, 0.88f);
        // Derived rather than configured, so a third tier always reads as
        // quieter than "secondary" whatever the user picked.
        t.textTertiary = WithAlpha(MixColor(settings.textSecondaryColor, t.base, 0.35f), 0.72f);

        material_ = t;
        SetPersistentBrush(raisedBrush_, t.raised);
        SetPersistentBrush(raisedStrongBrush_, t.raisedStrong);
    }

    // The island's shape for `rect`: shadow, surface, contour; also registers the
    // rect for the DWM backdrop. Returns the corner radius used.
    float DrawIslandSurface(D2D1_RECT_F rect, IslandKind kind, const Settings& settings) {
        float radius = settings.w11Style ? 8.0f * settings.sizeScale : (rect.bottom - rect.top) * 0.5f;
        if (settings.notchStyle) {
            radius = NotchBottomRadius((rect.bottom - rect.top) / settings.sizeScale) * settings.sizeScale;  // Bunbun
        } else if (!settings.w11Style) {
            radius = std::min(radius, 44.0f * settings.sizeScale);
        }
        if (pillCount_ < DwmBackdrop::kMaxPills) {
            pillRects_[pillCount_] = rect;
            pillRadii_[pillCount_] = radius;
            ++pillCount_;
        }
        DrawSoftShadow(rect, radius);
        DrawPillSurface(rect, radius, kind, settings);
        edgeRect_ = rect;  // Bunbun: the edge light follows this outline
        edgeRadius_ = radius;
        edgeValid_ = true;
        return radius;
    }

    // ownSurface = false: the activity is one section of a merged island whose
    // surface was already drawn (Bunbun: one dock, never two pills).
    void DrawPill(const SharedState& state, const Settings& settings, const Activity& activity,
                  D2D1_RECT_F rect, float scale, double now, bool ownSurface = true) {
        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float w = (rect.right - rect.left) * scale;
        const float h = (rect.bottom - rect.top) * scale;
        rect = D2D1::RectF(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);

        // Bunbun: tucked away, the island is only a thin status line.
        // Offscreen (another display's island) it's that island's own tuck.
        if (ownSurface && (offscreen_ ? restTuck_ : g_tucked.load(std::memory_order_relaxed))) {
            g_studioHits.clear();
            DrawTuckLine(state, rect, now);
            return;
        }
        if (ownSurface) g_studioHits.clear();  // Bunbun: the frame and pages re-publish them

        const float radius = ownSurface ? DrawIslandSurface(rect, activity.kind, settings) : 0.0f;

        if (ownSurface && activity.kind == IslandKind::Progress) {
            DrawProgressRing(rect, state.progress.percent);
        }

        if (ownSurface && activity.kind == IslandKind::BatteryLow) {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now * 2.0 * 3.14159265 * 2.1));
            redBrush_->SetOpacity(0.45f + 0.45f * pulse);
            DrawIslandShape(rect, radius, settings.w11Style, settings.notchStyle, redBrush_.Get(), 2.0f);
            redBrush_->SetOpacity(1.0f);
        }

        // No inner highlight ring here. A second bright hairline just inside the
        // contour read as a light rim tracing the whole island, which is exactly
        // the edge glow this design drops. The contour stroke alone defines the
        // silhouette now.

        D2D1_MATRIX_3X2_F oldTransform;
        target_->GetTransform(&oldTransform);
        D2D1_POINT_2F pillCenter = D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(settings.sizeScale, settings.sizeScale, pillCenter) * oldTransform);

        float invScale = 1.0f / settings.sizeScale;
        float unW = (rect.right - rect.left) * invScale;
        float unH = (rect.bottom - rect.top) * invScale;
        D2D1_RECT_F unscaledRect = D2D1::RectF(pillCenter.x - unW * 0.5f, pillCenter.y - unH * 0.5f, pillCenter.x + unW * 0.5f, pillCenter.y + unH * 0.5f);

        // Bunbun: compact pills draw their content left of the privacy dots (the
        // activity was widened by the same amount in ActivityForKind).
        D2D1_RECT_F contentRect = unscaledRect;
        if (unH <= 55.0f) contentRect.right -= PrivacyDotsReserve(state, settings);

        // Bunbun: the Studio frame takes a header and a tab bar off the open island.
        const D2D1_RECT_F pageRect = ownSurface ? BeginStudioFrame(state, settings, activity.kind, unscaledRect, now)
                                                : unscaledRect;

        switch (activity.kind) {
            case IslandKind::Media:
                DrawMedia(state, pageRect, settings, now);
                break;
            case IslandKind::Clipboard:
                DrawClipboard(state, contentRect);
                break;
            case IslandKind::Notification:
                DrawNotification(state, contentRect);
                break;
            case IslandKind::Volume:
                DrawVolume(state, contentRect);
                break;
            case IslandKind::Brightness:
                DrawBrightness(state, contentRect);
                break;
            case IslandKind::CapsLock:
                DrawCapsLock(state, contentRect);
                break;
            case IslandKind::Agent:
                DrawAgent(state, contentRect, settings, now);
                break;
            case IslandKind::Device:
                DrawDevice(state, contentRect);
                break;
            case IslandKind::Bluetooth:
                DrawBluetoothDevice(state, contentRect);
                break;
            case IslandKind::Timer:
                DrawTimer(state, contentRect);
                break;
            case IslandKind::BatteryLow:
                DrawBattery(state, contentRect);
                break;
            case IslandKind::Progress:
                DrawProgress(state, contentRect);
                break;
            case IslandKind::DoNotDisturb:
                DrawDoNotDisturb(state, contentRect);
                break;
            case IslandKind::Lookup:
                if (settings.studioLayout) DrawStudioChat(state, unscaledRect, settings, now);  // Bunbun
                else DrawLookup(state, contentRect, settings, now);
                break;
            case IslandKind::Reminder:
                DrawReminderPill(state, contentRect, now);
                break;
            case IslandKind::Privacy:
                DrawPrivacyPill(state, settings, contentRect);
                break;
            case IslandKind::Meeting:
                DrawMeetingPill(state, contentRect);
                break;
            case IslandKind::Download:
                DrawDownloadPill(state, contentRect, now);
                break;
            case IslandKind::Idle:
            default:
                DrawIdleDashboard(state, pageRect, settings, now);
                break;
        }
        if (ownSurface) EndStudioFrame(state, settings, now);  // Bunbun

        // ── Apple-style privacy indicator dots ───────────────────────────────
        // Green dot = camera in use, Orange dot = mic in use.
        // Drawn in top-right corner of pill, outside content area.
        DrawPrivacyDots(state, settings, unscaledRect, now);

        target_->SetTransform(oldTransform);
    }

    // A real soft shadow, approximated by stacking concentric rounded shapes
    // with a quadratic alpha falloff. There is no GPU device here (this renders
    // to a DC-bound target for UpdateLayeredWindow), so a Gaussian blur effect
    // is not available -- but the render padding around the pill leaves room to
    // fake it convincingly and cheaply.
    void DrawSoftShadow(D2D1_RECT_F rect, float radius) {
        // The island sits on top of the backdrop, so a shadow here would darken the blur.
        if (!g_settings.dropShadow || g_settings.backdropMaterial != BackdropMaterial::None) {
            return;
        }

        const float spread = Clamp(14.0f * g_settings.sizeScale, 6.0f, kRenderPadY - 4.0f);
        const float yOffset = spread * 0.35f;
        constexpr int kSteps = 7;

        ID2D1SolidColorBrush* brush = AcquireBrush(material_.shadow);
        if (!brush) {
            return;
        }

        for (int i = kSteps; i >= 1; --i) {
            const float t = static_cast<float>(i) / static_cast<float>(kSteps);
            const float grow = spread * t;
            // Quadratic falloff keeps the core dense and the outer edge feathered.
            const float alpha = material_.shadow.a * (1.0f - t) * (1.0f - t) * 0.55f * settingsOpacity_;
            if (alpha <= 0.002f) {
                continue;
            }
            brush->SetOpacity(alpha);

            const D2D1_RECT_F shadowRect = D2D1::RectF(
                rect.left - grow, rect.top - grow * 0.55f + yOffset,
                rect.right + grow, rect.bottom + grow + yOffset);
            FillIslandShape(shadowRect, radius + grow, g_settings.w11Style,
                            g_settings.notchStyle, brush);
        }
        brush->SetOpacity(1.0f);
    }

    // Vertical depth shading. This used to open with a white sheen across the
    // top, which on a dark island read as a lit strip along the upper edge --
    // the same edge-glow look the rest of this design removes. What is left is
    // a single downward shade that grounds the surface without lighting any
    // edge: neutral at the top, gradually deeper toward the bottom.
    void FillSurfaceDepth(D2D1_RECT_F rect, float radius, bool strong) {
        const float h = rect.bottom - rect.top;
        if (h <= 2.0f) {
            return;
        }

        // Light backgrounds need less of it: the same alpha over near-white
        // turns into a visible grey wash rather than subtle depth.
        const float botA = (strong ? 0.085f : 0.060f) *
                           (material_.onDark ? 1.0f : 0.55f) * settingsOpacity_;

        // Stops are constant; `botA` is applied as brush opacity (identical result,
        // since every stop alpha was just a multiple of botA). One brush forever.
        static const D2D1_GRADIENT_STOP kStops[3] = {
            {0.0f,  D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)},
            {0.45f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.25f)},
            {1.0f,  D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f)},
        };
        ID2D1LinearGradientBrush* brush = UseLinearGradient(
            depthGrad_, kStops, 3,
            D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.left, rect.bottom));
        if (!brush) {
            return;
        }
        brush->SetOpacity(Clamp(botA, 0.0f, 1.0f));
        FillIslandShape(rect, radius, g_settings.w11Style, g_settings.notchStyle, brush);
    }

    // A wide, very soft accent wash bled in from the top of the surface. Driven
    // by the album-art accent, this is what makes the media surface feel alive
    // without tinting the whole pill.
    void FillAccentBloom(D2D1_RECT_F rect, float radius, float strength) {
        if (strength <= 0.01f) {
            return;
        }

        const float w = rect.right - rect.left;
        const float h = rect.bottom - rect.top;
        if (w <= 2.0f || h <= 2.0f) {
            return;
        }

        // Only the accent colour is baked into the stops. Strength is brush opacity, so
        // the gradient is rebuilt only while the accent is actually changing colour.
        const D2D1_COLOR_F a = material_.accent;
        const D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, D2D1::ColorF(a.r, a.g, a.b, 1.0f)},
            {1.0f, D2D1::ColorF(a.r, a.g, a.b, 0.0f)},
        };
        const D2D1_POINT_2F center = D2D1::Point2F((rect.left + rect.right) * 0.5f, rect.top);
        ID2D1RadialGradientBrush* brush =
            UseRadialGradient(bloomGrad_, stops, 2, center, w * 0.75f, h * 1.15f);
        if (!brush) {
            return;
        }
        brush->SetOpacity(Clamp(strength * settingsOpacity_, 0.0f, 1.0f));
        FillIslandShape(rect, radius, g_settings.w11Style, g_settings.notchStyle, brush);
    }

    // Rounded inner card used by every dashboard, so panels across the media,
    // calendar, weather, hardware and file-tray surfaces match exactly.
    //
    // Fill only, no outline. Each card used to also get a hairline ring, and six
    // of those side by side in the hardware grid turned into a mesh of bright
    // edges competing with the content. The fill alpha in BuildMaterialTokens
    // was raised to carry the separation on its own.
    void DrawCard(D2D1_RECT_F rect, float radius, bool active = false) {
        ID2D1SolidColorBrush* fill = active ? raisedStrongBrush_.Get() : raisedBrush_.Get();
        if (fill) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), fill);
        }
    }

    // Accent-filled progress track shared by the scrubber, volume, battery and
    // timer, so "progress" looks the same everywhere.
    void DrawAccentTrack(D2D1_RECT_F track, float progress, float radius) {
        if (raisedStrongBrush_) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(track, radius, radius), raisedStrongBrush_.Get());
        }

        const float span = (track.right - track.left) * Clamp(progress, 0.0f, 1.0f);
        if (span <= 0.5f) {
            return;
        }

        const D2D1_RECT_F fillRect = D2D1::RectF(track.left, track.top, track.left + span, track.bottom);

        const D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, WithAlpha(MixColor(material_.accent, D2D1::ColorF(1, 1, 1, 1), 0.30f), 0.95f)},
            {1.0f, WithAlpha(material_.accent, 1.0f)},
        };
        if (ID2D1LinearGradientBrush* grad = UseLinearGradient(
                trackGrad_, stops, 2,
                D2D1::Point2F(fillRect.left, fillRect.top),
                D2D1::Point2F(fillRect.right, fillRect.top))) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(fillRect, radius, radius), grad);
        }
    }

    void DrawPrivacyDots(const SharedState& state, const Settings& settings, D2D1_RECT_F rect, double now) {
        UNREFERENCED_PARAMETER(now);
        const float height = rect.bottom - rect.top;
        if (height > 55.0f) return;

        const bool mic = state.system.micActive && settings.privacyDots && settings.privacyDotsMic;
        const bool cam = state.system.cameraActive && settings.privacyDots && settings.privacyDotsCam;
        const bool screen = state.system.screenActive && settings.privacyDots && settings.privacyDotsScreen;  // Bunbun
        if (!mic && !cam && !screen) return;

        const float dotR   = 4.0f;
        const float margin = 16.0f;
        const float dotY   = rect.top + (rect.bottom - rect.top) * 0.5f;

        float x = rect.right - margin - dotR;

        // Bunbun: one dot per device in use, outermost first: screen (blue), camera
        // (green), microphone (orange), so the dots say what is being used.
        auto dot = [&](D2D1_COLOR_F c) {
            c.a = settingsOpacity_;
            if (ID2D1SolidColorBrush* b = AcquireBrush(c)) {
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, dotY), dotR, dotR), b);
            }
            x -= dotR * 2.0f + 5.0f;
        };
        if (screen) dot(settings.privacyDotsScreenHex);
        if (cam) dot(settings.privacyDotsCamHex);
        if (mic) dot(settings.privacyDotsMicHex);
    }

    // Bunbun: `open` leaves out the top edge (the screen's edge) and starts at the
    // right ear's tip, for the edge light to run along.
    ComPtr<ID2D1PathGeometry> CreateNotchGeometry(D2D1_RECT_F rect, float radius, bool open = false) {
        ComPtr<ID2D1PathGeometry> geom;
        if (FAILED(d2dFactory_->CreatePathGeometry(&geom))) return nullptr;

        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geom->Open(&sink))) return nullptr;

        float r = std::min({radius, (rect.right - rect.left) * 0.5f, (rect.bottom - rect.top) * 0.5f});
        if (r < 0.0f) r = 0.0f;

        // Bunbun: concave "ears" where the notch meets the screen edge, so it
        // flows out of the bezel instead of starting with a hard corner (the look
        // of Vayli's notch, drawn our own way). Each is a quarter circle just
        // outside the body, from the top edge down onto its side: 16 px, within
        // the window's side padding.
        const float w = rect.right - rect.left, h = rect.bottom - rect.top;
        const float ear = std::max(0.0f, std::min({16.0f * g_settings.sizeScale, kRenderPadX - 4.0f, h * 0.42f, w / 8.0f}));
        auto earArc = [&](float x, float y) {
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(ear, ear), 0.0f,
                                          D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        };

        if (open) {
            sink->BeginFigure(D2D1::Point2F(rect.right + ear, rect.top), D2D1_FIGURE_BEGIN_HOLLOW);
        } else {
            sink->BeginFigure(D2D1::Point2F(rect.left - ear, rect.top), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(rect.right + ear, rect.top));
        }
        if (ear > 0.0f) earArc(rect.right, rect.top + ear);
        sink->AddLine(D2D1::Point2F(rect.right, rect.bottom - r));
        if (r > 0.0f) {
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(rect.right - r, rect.bottom),
                D2D1::SizeF(r, r), 0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(rect.left + r, rect.bottom));
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(rect.left, rect.bottom - r),
                D2D1::SizeF(r, r), 0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        } else {
            sink->AddLine(D2D1::Point2F(rect.left, rect.bottom));
        }
        if (ear > 0.0f) {  // Bunbun: the left ear, mirrored
            sink->AddLine(D2D1::Point2F(rect.left, rect.top + ear));
            earArc(rect.left - ear, rect.top);
        }
        sink->EndFigure(open ? D2D1_FIGURE_END_OPEN : D2D1_FIGURE_END_CLOSED);
        sink->Close();

        return geom;
    }

    ComPtr<ID2D1Geometry> CreateIslandMaskGeometry(D2D1_RECT_F rect, float radius, bool notchStyle) {
        return GetShapeGeometry(rect, radius, notchStyle);
    }

    void FillIslandShape(D2D1_RECT_F rect, float radius, bool w11Style, bool notchStyle, ID2D1Brush* brush) {
        if (!brush) return;
        if (notchStyle) {
            ComPtr<ID2D1Geometry> geom = GetShapeGeometry(rect, radius, true);
            if (geom) {
                target_->FillGeometry(geom.Get(), brush);
                return;
            }
        }
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
    }

    void DrawIslandShape(D2D1_RECT_F rect, float radius, bool w11Style, bool notchStyle, ID2D1Brush* brush, float strokeWidth) {
        if (!brush) return;
        if (notchStyle) {
            auto geom = CreateNotchGeometry(rect, radius);
            if (geom) {
                target_->DrawGeometry(geom.Get(), brush, strokeWidth);
                return;
            }
        }
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush, strokeWidth);
    }

    // The island's material, composed bottom-up:
    //   tint scrim -> base fill -> depth shading -> accent bloom -> contour
    // Each layer is individually subtle; together they give the pill depth
    // instead of the flat single-fill look it had before. Deliberately absent:
    // any bright rim, hairline or sheen tracing the island's edge.
    void DrawPillSurface(D2D1_RECT_F rect, float radius, IslandKind kind, const Settings& settings) {
        // The dark tint scrim exists to deepen an opaque background. With a real
        // backdrop enabled it would just mud up the blur, so it is skipped.
        if (tintBrush_ && settings.backdropMaterial == BackdropMaterial::None) {
            FillIslandShape(rect, radius, settings.w11Style, settings.notchStyle, tintBrush_.Get());
        }

        // Blur / acrylic and tint come from the DWM window below (see DwmBackdrop);
        // only the fill is drawn here.

        // User-defined pill background color. The authored alpha is combined
        // with the global pill transparency, so a plain 6-digit hex behaves
        // exactly as before (alpha 1.0) while #RRGGBBAA stays translucent.
        D2D1_COLOR_F bg = pillBgColor_;
        bg.a = Clamp(bg.a * settingsOpacity_, 0.0f, 1.0f);
        if (settings.backdropMaterial != BackdropMaterial::None) {
            // An opaque fill would hide the backdrop, so cap it.
            bg.a = std::min(bg.a, settings.backdropFillAlpha);
        }
        if (ID2D1SolidColorBrush* fillBrush = AcquireBrush(bg)) {
            FillIslandShape(rect, radius, settings.w11Style, settings.notchStyle, fillBrush);
        }

        if (settings.materialDepth) {
            FillSurfaceDepth(rect, radius, kind == IslandKind::Media || kind == IslandKind::Idle);

            // Media leans on the album-art accent; other surfaces get a whisper
            // of it so the whole UI still feels connected to what is playing.
            const float bloom = (kind == IslandKind::Media) ? 0.115f
                                : (kind == IslandKind::Idle) ? 0.055f
                                                             : 0.075f;
            FillAccentBloom(rect, radius, bloom * settings.accentBloom);
        }

        if (settings.contourBorderMode != ContourBorderMode::Borderless && settings.contourBorderEnabled) {
            D2D1_COLOR_F borderColor = settings.contourBorderColor;
            float strokeWidth = settings.w11Style ? 1.0f : 0.8f;

            if (settings.contourBorderMode == ContourBorderMode::Auto) {
                if (currentAccent_.a > 0.0f) {
                    borderColor = currentAccent_;
                    borderColor.a = std::min(1.0f, (kind == IslandKind::Idle ? 0.35f : 0.60f) * settingsOpacity_);
                    strokeWidth = 1.0f;
                } else {
                    borderColor.a = std::min(1.0f, borderColor.a * settingsOpacity_);
                }
            } else {
                borderColor.a = std::min(1.0f, borderColor.a * settingsOpacity_);
            }

            if (ID2D1SolidColorBrush* border = AcquireBrush(borderColor)) {
                D2D1_RECT_F borderRect = D2D1::RectF(rect.left + 0.5f, rect.top + 0.5f,
                                                     rect.right - 0.5f, rect.bottom - 0.5f);
                DrawIslandShape(borderRect, radius, settings.w11Style, settings.notchStyle, border, strokeWidth);
            }
        }
    }


    static void GetWeatherIconAndText(int code, std::wstring& icon, std::wstring& text) {
        switch (code) {
            case 113: icon = L"☀️"; break;
            case 116: icon = L"⛅"; break;
            case 119: case 122: icon = L"☁️"; break;
            case 143: case 248: case 260: icon = L"🌫️"; break;
            case 200: case 386: case 389: case 392: case 395: icon = L"⛈️"; break;
            case 176: case 263: case 266: case 281: case 284: case 293: case 296: case 299: case 302: case 305: case 308: case 311: case 314: case 353: case 356: case 359: icon = L"🌧️"; break;
            case 179: case 182: case 185: case 227: case 230: case 317: case 320: case 323: case 326: case 329: case 332: case 335: case 338: case 350: case 362: case 365: case 368: case 371: icon = L"❄️"; break;
            default: icon = L"🌡️"; break;
        }
    }

    static int GetDaysInMonth(int year, int month) {
        if (month == 2) {
            bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            return leap ? 29 : 28;
        }
        if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
        return 31;
    }

    static int GetDayOfWeek(int year, int month, int day) {
        if (month < 3) { month += 12; year -= 1; }
        int k = year % 100;
        int j = year / 100;
        int h = (day + 13 * (month + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
        return (h + 6) % 7;
    }



    void DrawCalendarDashboard(const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now, float scale, SYSTEMTIME& local) {
        (void)state;
        (void)now;

        // ── Hero column ──────────────────────────────────────────────────────
        // Today's date as an editorial block: month over a large day figure over
        // the weekday. Uses the shared DrawCard so it matches every other panel
        // instead of being a one-off translucent slab.
        const D2D1_RECT_F hero = D2D1::RectF(rect.left + 22.0f * scale, rect.top + 16.0f * scale,
                                             rect.left + 115.0f * scale, rect.bottom - 20.0f * scale);
        // Bunbun: with a meeting coming up, "Up next" takes the hero's place. The
        // hero code still runs (it fills the caches the grid uses) but draws into
        // an empty clip.
        const bool upNext = state.meeting.valid;
        // Bunbun: the week view (wide pages) takes the whole width, so the card stands aside.
        const bool weekFull = (rect.right - rect.left) >= 440.0f * scale && g_calView == 1;
        if (upNext || weekFull) target_->PushAxisAlignedClip(D2D1::RectF(0, 0, 0, 0), D2D1_ANTIALIAS_MODE_ALIASED);
        DrawCard(hero, 12.0f * scale);

        // The accent is the album-art / system accent like everywhere else. This
        // used to fall back to a hardcoded red (#D94A38) whenever the mode wasn't
        // "System", which was the one colour in the whole island that answered to
        // nothing -- it clashed with the accent on every other surface.
        D2D1_COLOR_F accentColor = (settings.calendarAccent == CalendarAccentMode::System)
            ? GetSystemAccentColor()
            : material_.accent;
        accentColor.a = 0.95f * settingsOpacity_;

        ComPtr<ID2D1SolidColorBrush> accent;
        target_->CreateSolidColorBrush(accentColor, &accent);

        if (calendarCachedDate_.wYear != local.wYear || calendarCachedDate_.wMonth != local.wMonth ||
            calendarCachedDate_.wDay != local.wDay ||
            calendarCachedFirstDaySetting_ != settings.firstDayOfWeek) {
            wchar_t monthNameBuf[32] = {};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"MMMM", monthNameBuf, ARRAYSIZE(monthNameBuf), nullptr);
            // No longer uppercased. towupper is per-character and locale-blind:
            // it turns Turkish "i" into "I" rather than "İ", and does nothing at
            // all for CJK month names, so the effect was inconsistent by locale.
            calendarCachedMonthName_ = monthNameBuf;

            wchar_t weekdayNameBuf[32] = {};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"dddd", weekdayNameBuf, ARRAYSIZE(weekdayNameBuf), nullptr);
            calendarCachedWeekdayName_ = weekdayNameBuf;

            calendarCachedFirstDay_ = settings.firstDayOfWeek;
            if (calendarCachedFirstDay_ < 0) {
                DWORD localeFirstDay = 0;  // locale counts 0 = Monday
                calendarCachedFirstDay_ =
                    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER,
                                    reinterpret_cast<LPWSTR>(&localeFirstDay),
                                    sizeof(localeFirstDay) / sizeof(wchar_t)) > 0
                        ? static_cast<int>((localeFirstDay + 1) % 7)
                        : 0;
            }

            // Same locale as the month and weekday names above, so the grid
            // headers can't end up in a different language from the hero.
            static const wchar_t* const kFallbackInitials[] = {L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
            for (int weekday = 0; weekday < 7; ++weekday) {
                wchar_t initialBuf[16] = {};
                // CAL_SSHORTESTDAYNAME1 is Monday; weekday 0 here is Sunday.
                const CALTYPE calType = CAL_SSHORTESTDAYNAME1 + static_cast<CALTYPE>((weekday + 6) % 7);
                calendarCachedInitials_[weekday] =
                    GetCalendarInfoEx(LOCALE_NAME_USER_DEFAULT, CAL_GREGORIAN, nullptr, calType,
                                      initialBuf, ARRAYSIZE(initialBuf), nullptr) > 0
                        ? initialBuf
                        : kFallbackInitials[weekday];
            }
            calendarCachedFirstDaySetting_ = settings.firstDayOfWeek;

            calendarCachedDate_ = local;
        }

        // Month and year stay on separate lines. Putting them on one ("September
        // 2026") needs about 95px at 12px bold and the hero column only offers
        // 85, so any long month name clipped -- and month names are exactly the
        // strings that get long once localized.
        if (boldTextFormat_) {
            target_->DrawTextW(calendarCachedMonthName_.c_str(),
                               static_cast<UINT32>(calendarCachedMonthName_.size()),
                               boldTextFormat_.Get(),
                               D2D1::RectF(hero.left + 3.0f * scale, hero.top + 6.0f * scale,
                                           hero.right - 3.0f * scale, hero.top + 22.0f * scale),
                               accent.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        wchar_t yearStr[16] = {};
        swprintf_s(yearStr, L"%d", local.wYear);
        mutedBrush_->SetOpacity(0.62f);
        if (smallTextFormat_) {
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextW(yearStr, static_cast<UINT32>(wcslen(yearStr)), smallTextFormat_.Get(),
                               D2D1::RectF(hero.left, hero.top + 21.0f * scale,
                                           hero.right, hero.top + 36.0f * scale),
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        wchar_t dayStr[16] = {};
        swprintf_s(dayStr, L"%d", local.wDay);
        textBrush_->SetOpacity(0.97f);
        target_->DrawTextW(dayStr, static_cast<UINT32>(wcslen(dayStr)),
                           calDayLargeFormat_ ? calDayLargeFormat_.Get() : hugeTextFormat_.Get(),
                           D2D1::RectF(hero.left, hero.top + 34.0f * scale,
                                       hero.right, hero.bottom - 26.0f * scale),
                           textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);

        mutedBrush_->SetOpacity(0.70f);
        if (boldTextFormat_) {
            target_->DrawTextW(calendarCachedWeekdayName_.c_str(),
                               static_cast<UINT32>(calendarCachedWeekdayName_.size()),
                               boldTextFormat_.Get(),
                               D2D1::RectF(hero.left + 4.0f * scale, hero.bottom - 24.0f * scale,
                                           hero.right - 4.0f * scale, hero.bottom - 6.0f * scale),
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        if (upNext || weekFull) {
            target_->PopAxisAlignedClip();
            if (!weekFull) DrawCalendarUpNext(state, hero, scale);
        }

        // ── Month grid and week (Bunbun: both views, moved with ‹ ›) ─────────
        // On a wide page (the Studio layout), a roomier grid and an agenda column on
        // the right, and a Week view; a narrow one keeps the month. A row on top
        // names the month or week shown, with ‹ › and (wide) Month | Week.
        const bool wideCalendar = (rect.right - rect.left) >= 440.0f * scale;
        const bool weekView = wideCalendar && g_calView == 1;
        // The week view has the whole width: the date card stands aside (see the hero).
        const float gridStart = weekView ? rect.left + 22.0f * scale : rect.left + (wideCalendar ? 128.0f : 134.0f) * scale;
        const float navTop = rect.top + 12.0f * scale, navH = 20.0f * scale;
        const float gridTop = navTop + navH + 4.0f * scale;
        const float colW = (wideCalendar ? 30.0f : 28.0f) * scale;
        const float headerH = 18.0f * scale;
        const float areaRight = rect.right - 4.0f * scale;
        const int firstDay = calendarCachedFirstDay_;
        IDWriteTextFormat* gridFmt = calGridFormat_ ? calGridFormat_.Get() : boldTextFormat_.Get();

        // Days since 1970 in local time, so events and to-dos land on the right day.
        auto dayOf = [](double epoch) -> int64_t {
            const time_t t = static_cast<time_t>(epoch);
            tm lt{};
            localtime_s(&lt, &t);
            return bb::ics::DaysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        };
        const int64_t today = bb::ics::DaysFromCivil(local.wYear, local.wMonth, local.wDay);
        const int64_t todayWeek = today - (GetDayOfWeek(local.wYear, local.wMonth, local.wDay) - firstDay + 7) % 7;
        // Busy days: meetings from the calendar link, and to-dos due that day.
        std::map<int64_t, std::pair<int, int>> busy;
        if (state.calendar)
            for (const CalendarItem& e : *state.calendar) ++busy[dayOf(e.startsAt)].first;
        for (const TodoItem& t : state.todos)
            if (!t.done && t.due > 0.0) ++busy[dayOf(t.due)].second;
        ComPtr<ID2D1SolidColorBrush> green;
        target_->CreateSolidColorBrush(D2D1::ColorF(0.19f, 0.82f, 0.35f, 0.95f * settingsOpacity_), &green);

        // The month shown: today's moved by g_calMonthOffset.
        int showY = local.wYear, showM = local.wMonth + (weekView ? 0 : g_calMonthOffset);
        while (showM < 1) { showM += 12; --showY; }
        while (showM > 12) { showM -= 12; ++showY; }
        const int64_t weekStart = todayWeek + 7LL * g_calWeekOffset;

        // ── The row on top: ‹ title ›, then Month | Week ──
        const float navRight = wideCalendar ? areaRight : gridStart + 7.0f * colW;
        auto navButton = [&](D2D1_RECT_F b, const wchar_t* label, StudioAction action, int arg, bool on) {
            const float hover = SHover(action, arg);
            if (on || hover > 0.01f)
                SFillRound(b, (b.bottom - b.top) * 0.5f, D2D1::ColorF(1, 1, 1, on ? 0.16f : 0.10f * hover));
            textBrush_->SetOpacity(on ? 1.0f : 0.8f);
            DrawAgentText(label, b, smallTextFormat_.Get(), textBrush_.Get(), true);
            SHit(b, action, arg);
        };
        float chipsLeft = navRight;
        if (wideCalendar) {
            const float cw = 48.0f * scale;
            chipsLeft = navRight - cw * 2.0f;
            navButton(D2D1::RectF(chipsLeft, navTop, chipsLeft + cw, navTop + navH), L"Month", StudioAction::CalView, 0, !weekView);
            navButton(D2D1::RectF(chipsLeft + cw, navTop, navRight, navTop + navH), L"Week", StudioAction::CalView, 1, weekView);
        }
        std::wstring title;
        {
            wchar_t buf[64] = {};
            if (!weekView) {
                SYSTEMTIME st{};
                st.wYear = static_cast<WORD>(showY);
                st.wMonth = static_cast<WORD>(showM);
                st.wDay = 1;
                GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMMM yyyy", buf, ARRAYSIZE(buf), nullptr);
                title = buf;
            } else {
                auto label = [&](int64_t days, const wchar_t* fmt) {
                    int y = 0, m = 0, d = 0;
                    bb::ics::CivilFromDays(days, &y, &m, &d);
                    SYSTEMTIME st{};
                    st.wYear = static_cast<WORD>(y);
                    st.wMonth = static_cast<WORD>(m);
                    st.wDay = static_cast<WORD>(d);
                    wchar_t b[32] = {};
                    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, fmt, b, ARRAYSIZE(b), nullptr);
                    return std::wstring(b);
                };
                title = label(weekStart, L"d MMM") + L" \x2013 " + label(weekStart + 6, L"d MMM");
            }
        }
        const float arrowW = 22.0f * scale;
        // The title spans the grid (the week view: the whole width before the chips).
        const float titleRight = (weekView ? chipsLeft - 8.0f * scale : gridStart + 7.0f * colW) - arrowW;
        navButton(D2D1::RectF(gridStart, navTop, gridStart + arrowW, navTop + navH), L"\x2039", StudioAction::CalPrev, 0, false);
        navButton(D2D1::RectF(titleRight, navTop, titleRight + arrowW, navTop + navH), L"\x203A", StudioAction::CalNext, 0, false);
        const bool moved = weekView ? g_calWeekOffset != 0 : g_calMonthOffset != 0;
        const D2D1_RECT_F titleRect = D2D1::RectF(gridStart + arrowW, navTop, titleRight, navTop + navH);
        textBrush_->SetOpacity(0.95f);
        DrawAgentText(title, titleRect, boldTextFormat_.Get(), textBrush_.Get(), true);
        if (moved) SHit(titleRect, StudioAction::CalToday);  // a click on the title goes back to today

        if (weekView) {
            DrawCalendarWeek(state, D2D1::RectF(gridStart, gridTop, areaRight, rect.bottom - 10.0f * scale), weekStart, today,
                             firstDay, scale, accent.Get(), green.Get(), dayOf);
            textBrush_->SetOpacity(0.96f);
            mutedBrush_->SetOpacity(0.75f);
            return;
        }

        const int startDayIdx = (GetDayOfWeek(showY, showM, 1) - firstDay + 7) % 7;
        const int monthDays = GetDaysInMonth(showY, showM);
        const int rowCount = (startDayIdx + monthDays + 6) / 7;  // 5 or 6

        // Row height is derived from the space actually available rather than
        // fixed at 26px. At 26 a six-row month ran to y=197 inside a 184px
        // island, so the last row was silently clipped by the island mask --
        // visible every month that starts late in the week.
        const float datesTop = gridTop + headerH + 7.0f * scale;
        const float gridBottom = rect.bottom - 10.0f * scale;
        const float rowH = (gridBottom - datesTop) / static_cast<float>(rowCount > 0 ? rowCount : 1);

        // Weekday initials are uniformly quiet. They used to paint S and S in the
        // accent, which put three competing accent marks in the grid (both
        // weekend headers plus today) and made the headers look selected.
        mutedBrush_->SetOpacity(0.55f);
        for (int i = 0; i < 7; ++i) {
            const D2D1_RECT_F cell = D2D1::RectF(gridStart + i * colW, gridTop,
                                                 gridStart + (i + 1) * colW, gridTop + headerH);
            const std::wstring& initial = calendarCachedInitials_[(firstDay + i) % 7];
            target_->DrawTextW(initial.c_str(), static_cast<UINT32>(initial.size()), gridFmt, cell,
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        // Hairline under the weekday headers.
        const float hrY = gridTop + headerH + 3.0f * scale;
        ComPtr<ID2D1SolidColorBrush> hrBrush;
        if (SUCCEEDED(target_->CreateSolidColorBrush(
                WithAlpha(material_.hairline, material_.hairline.a * settingsOpacity_), &hrBrush)) && hrBrush) {
            target_->DrawLine(D2D1::Point2F(gridStart + 2.0f * scale, hrY),
                              D2D1::Point2F(gridStart + 7.0f * colW - 2.0f * scale, hrY),
                              hrBrush.Get(), 1.0f * scale);
        }

        // Today's marker is sized from the cell it has to live in, so it stays a
        // circle around the figure instead of a fixed 12px disc that swallowed
        // the digits once the rows got shorter.
        const float markerR = std::min(colW, rowH) * 0.5f - 2.5f * scale;

        int row = 0;
        int col = startDayIdx;
        for (int d = 1; d <= monthDays; ++d) {
            const D2D1_RECT_F cell = D2D1::RectF(gridStart + col * colW, datesTop + row * rowH,
                                                 gridStart + (col + 1) * colW, datesTop + (row + 1) * rowH);
            // The figure sits a little high, leaving room for the busy dots under it.
            const D2D1_RECT_F figure = D2D1::RectF(cell.left, cell.top - 2.0f * scale, cell.right, cell.bottom - 2.0f * scale);
            const std::wstring dayText = std::to_wstring(d);
            const int weekday = (firstDay + col) % 7;
            const int64_t days = bb::ics::DaysFromCivil(showY, showM, d);

            if (days == today) {
                target_->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(cell.left + colW * 0.5f, cell.top + rowH * 0.5f - 2.0f * scale),
                                  markerR, markerR),
                    accent.Get());
                // Today's figure is drawn against the accent fill, so it needs the
                // primary text colour at full strength rather than the 0.85 the
                // other days use.
                textBrush_->SetOpacity(1.0f);
                target_->DrawTextW(dayText.c_str(), static_cast<UINT32>(dayText.size()), gridFmt,
                                   figure, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            } else if (weekday == 0 || weekday == 6) {
                // Weekends recede instead of taking the accent, leaving today as
                // the only accented thing in the grid.
                mutedBrush_->SetOpacity(0.62f);
                target_->DrawTextW(dayText.c_str(), static_cast<UINT32>(dayText.size()), gridFmt,
                                   figure, mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            } else {
                textBrush_->SetOpacity(0.88f);
                target_->DrawTextW(dayText.c_str(), static_cast<UINT32>(dayText.size()), gridFmt,
                                   figure, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
            // Bunbun: busy days: a dot per meeting (accent) and per to-do due (green), up to 3.
            const auto it = busy.find(days);
            if (it != busy.end()) {
                const int meetings = std::min(it->second.first, 3), todos = std::min(it->second.second, 3 - meetings);
                const int n = meetings + todos;
                const float r = 1.7f * scale, gap = 4.6f * scale;
                const float y = cell.bottom - 3.0f * scale;
                float x = (cell.left + cell.right) * 0.5f - (n - 1) * gap * 0.5f;
                for (int k = 0; k < n; ++k, x += gap)
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), r, r), k < meetings ? accent.Get() : green.Get());
            }
            // A day opens its week (wide pages, which have the week view).
            if (wideCalendar)
                SHit(cell, StudioAction::CalOpenWeek, static_cast<int>((days - todayWeek >= 0 ? days - todayWeek : days - todayWeek - 6) / 7));

            if (++col > 6) { col = 0; ++row; }
        }

        textBrush_->SetOpacity(0.96f);
        mutedBrush_->SetOpacity(0.75f);
        if (wideCalendar) {  // Bunbun
            const float agendaLeft = gridStart + 7.0f * colW + 14.0f * scale;
            DrawCalendarAgenda(state, D2D1::RectF(agendaLeft, gridTop, areaRight, rect.bottom - 14.0f * scale), now);
        }
    }

    // Bunbun: the Calendar page's week: seven columns from `weekStart` (days since
    // 1970), each with its day, then that day's events (time and title) and the
    // to-dos due, as many as fit, and "+N more".
    template <typename DayOf>
    void DrawCalendarWeek(const SharedState& state, D2D1_RECT_F area, int64_t weekStart, int64_t today, int firstDay,
                          float scale, ID2D1Brush* accent, ID2D1Brush* green, const DayOf& dayOf) {
        const float colW = (area.right - area.left) / 7.0f;
        const float headH = 34.0f * scale, rowH = 30.0f * scale;  // two lines: the time, then the title
        const int rowsFit = std::max(1, static_cast<int>((area.bottom - area.top - headH - 2.0f * scale) / (rowH + 2.0f * scale)));
        for (int i = 0; i < 7; ++i) {
            const int64_t days = weekStart + i;
            int y = 0, m = 0, d = 0;
            bb::ics::CivilFromDays(days, &y, &m, &d);
            const float x0 = area.left + i * colW, x1 = x0 + colW - 3.0f * scale;
            const bool isToday = days == today;
            if (isToday) SFillRound(D2D1::RectF(x0, area.top, x1, area.bottom), 10.0f * scale, D2D1::ColorF(1, 1, 1, 0.05f));
            mutedBrush_->SetOpacity(0.6f);
            DrawAgentText(calendarCachedInitials_[(firstDay + i) % 7], D2D1::RectF(x0, area.top + 2.0f * scale, x1, area.top + 15.0f * scale),
                          smallTextFormat_.Get(), mutedBrush_.Get(), true);
            const D2D1_POINT_2F c = D2D1::Point2F((x0 + x1) * 0.5f, area.top + 25.0f * scale);
            if (isToday) target_->FillEllipse(D2D1::Ellipse(c, 10.0f * scale, 10.0f * scale), accent);
            textBrush_->SetOpacity(isToday ? 1.0f : 0.9f);
            DrawAgentText(std::to_wstring(d), D2D1::RectF(x0, c.y - 9.0f * scale, x1, c.y + 9.0f * scale), smallTextFormat_.Get(),
                          textBrush_.Get(), true);

            // The day's items: events first (by start), then the to-dos due.
            struct Row { std::wstring when, text; bool todo; };
            std::vector<Row> rows;
            auto clock = [](double epoch) {
                const time_t t = static_cast<time_t>(epoch);
                tm lt{};
                localtime_s(&lt, &t);
                wchar_t hm[8];
                swprintf_s(hm, L"%02d:%02d", lt.tm_hour, lt.tm_min);
                return std::wstring(hm);
            };
            static const std::vector<CalendarItem> kNone;
            for (const CalendarItem& e : state.calendar ? *state.calendar : kNone)
                if (dayOf(e.startsAt) == days)
                    rows.push_back({e.endsAt - e.startsAt >= 23 * 3600.0 ? std::wstring(L"All day") : clock(e.startsAt), e.title, false});
            for (const TodoItem& t : state.todos)
                if (!t.done && t.due > 0.0 && dayOf(t.due) == days) rows.push_back({clock(t.due), t.text, true});
            float ry = area.top + headH;
            const int shown = static_cast<int>(rows.size()) > rowsFit ? rowsFit - 1 : static_cast<int>(rows.size());
            for (int k = 0; k < shown; ++k) {
                const D2D1_RECT_F chip = D2D1::RectF(x0 + 2.0f * scale, ry, x1 - 2.0f * scale, ry + rowH);
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(chip.left, chip.top + 3.0f * scale, chip.left + 2.5f * scale,
                                                                            chip.bottom - 3.0f * scale),
                                                                1.2f * scale, 1.2f * scale),
                                              rows[static_cast<size_t>(k)].todo ? green : accent);
                const Row& row = rows[static_cast<size_t>(k)];
                mutedBrush_->SetOpacity(0.6f);
                DrawEllipsizedText(row.when, D2D1::RectF(chip.left + 6.0f * scale, chip.top + 1.0f * scale, chip.right, chip.top + 14.0f * scale),
                                   smallTextFormat_.Get(), mutedBrush_.Get());
                textBrush_->SetOpacity(0.92f);
                DrawEllipsizedText(row.text, D2D1::RectF(chip.left + 6.0f * scale, chip.top + 14.0f * scale, chip.right, chip.bottom),
                                   smallTextFormat_.Get(), textBrush_.Get());
                ry += rowH + 2.0f * scale;
            }
            if (shown < static_cast<int>(rows.size())) {
                mutedBrush_->SetOpacity(0.6f);
                DrawEllipsizedText(L"+" + std::to_wstring(rows.size() - shown) + L" more",
                                   D2D1::RectF(x0 + 8.0f * scale, ry + 1.0f * scale, x1, ry + rowH), smallTextFormat_.Get(), mutedBrush_.Get());
            }
        }
    }

    // Wind direction is rendered as flow arrows, not bearing arrows -- a wind
    // *from* the north-east is drawn as an arrow pointing south-west, matching
    // the convention weather apps use.
    std::wstring WindDirToArrow(const std::wstring& dir) {
        if (dir == L"N") return L"\x2193";
        if (dir == L"NNE" || dir == L"NE" || dir == L"ENE") return L"\x2199";
        if (dir == L"E") return L"\x2190";
        if (dir == L"ESE" || dir == L"SE" || dir == L"SSE") return L"\x2196";
        if (dir == L"S") return L"\x2191";
        if (dir == L"SSW" || dir == L"SW" || dir == L"WSW") return L"\x2197";
        if (dir == L"W") return L"\x2192";
        if (dir == L"WNW" || dir == L"NW" || dir == L"NNW") return L"\x2198";
        return dir;
    }

