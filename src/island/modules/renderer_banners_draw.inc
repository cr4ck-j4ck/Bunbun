// Bunbun: Renderer: brightness, volume, waveform, notification and device banners.
// Split out of island_core.cpp; included inside class Renderer at the
// original position, so declaration order is unchanged. Not a standalone file.

    // ── Brightness banner (shares the Volume banner's look) ──────────────────
    void DrawLevelBanner(D2D1_RECT_F rect, const wchar_t* glyph, const std::wstring& label,
                         const wchar_t* value, float pct, bool drained) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 140.0f) return;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14, cy - badgeSz * 0.5f,
                                        rect.left + 14 + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), raisedStrongBrush_.Get());

        textBrush_->SetOpacity(0.95f);

        if (iconFormat_) {
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            target_->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), iconFormat_.Get(), badge,
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        const float tx = badge.right + 14;
        D2D1_RECT_F labelRect = D2D1::RectF(tx, cy - 13.0f, rect.right - 58, cy + 3.0f);
        mutedBrush_->SetOpacity(0.50f);
        target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                           smallTextFormat_.Get(), labelRect, mutedBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        D2D1_RECT_F valueRect = D2D1::RectF(rect.right - 58, cy - 13.0f, rect.right - 14, cy + 3.0f);
        target_->DrawTextW(value, static_cast<UINT32>(wcslen(value)), smallTextFormat_.Get(),
                           valueRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        textBrush_->SetOpacity(0.90f);

        D2D1_RECT_F track = D2D1::RectF(tx, cy + 7.0f, rect.right - 14, cy + 12.0f);
        if (drained) {
            ComPtr<ID2D1SolidColorBrush> trackBrush;
            if (SUCCEEDED(target_->CreateSolidColorBrush(material_.raisedStrong, &trackBrush)) && trackBrush) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(track, 2.5f, 2.5f), trackBrush.Get());
            }
            D2D1_RECT_F fill = D2D1::RectF(track.left, track.top,
                                           track.left + (track.right - track.left) * pct,
                                           track.bottom);
            ComPtr<ID2D1SolidColorBrush> dim;
            if (SUCCEEDED(target_->CreateSolidColorBrush(
                    WithAlpha(material_.textSecondary, 0.35f), &dim)) && dim) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(fill, 2.5f, 2.5f), dim.Get());
            }
        } else {
            DrawAccentTrack(track, pct, 2.5f);
        }
        accentBrush_->SetOpacity(1.0f);
        mutedBrush_->SetOpacity(0.58f);
    }

    void DrawBrightness(const SharedState& state, D2D1_RECT_F rect) {
        wchar_t value[32] = {};
        swprintf_s(value, L"%d%%", state.brightness.percent);
        DrawLevelBanner(rect, L"\uE706", Loc(L"Brightness"), value,
                        Clamp(state.brightness.percent / 100.0f, 0.0f, 1.0f), false);
    }

    // Bunbun: how much of Bun shows over the cover (0 = cover, 1 = Bun). A 7 s loop:
    // 3 s cover, 0.5 s cross-fade, 3 s Bun, 0.5 s back, shared with the collapsed pill.
    static float MascotCoverMix(double now) {
        constexpr double kCycle = 7.0;
        const double phase = std::fmod(now, kCycle);
        if (phase < 3.0) return 0.0f;
        if (phase < 3.5) return SmoothStep01(static_cast<float>((phase - 3.0) / 0.5));
        if (phase < 6.5) return 1.0f;
        return 1.0f - SmoothStep01(static_cast<float>((phase - 6.5) / 0.5));
    }

    // Bunbun: the cover takes its own shape (a video's 16:9 thumbnail isn't squashed
    // into a square). `growLeft`: keep the slot's height and widen leftwards (the Home
    // Music tile has room there); otherwise fit inside the slot, centred.
    static D2D1_RECT_F ArtShape(const MediaSnapshot& media, D2D1_RECT_F rect, bool growLeft) {
        if (!media.art || media.art->width <= 0 || media.art->height <= 0) return rect;
        const float aspect = Clamp(static_cast<float>(media.art->width) / media.art->height, 0.75f, 1.78f);
        const float w = rect.right - rect.left, h = rect.bottom - rect.top;
        if (w <= 1.0f || h <= 1.0f || std::fabs(aspect - w / h) < 0.06f) return rect;
        if (growLeft) return D2D1::RectF(rect.right - h * aspect, rect.top, rect.right, rect.bottom);
        const float fw = std::min(w, h * aspect), fh = fw / aspect;
        const float cx = (rect.left + rect.right) * 0.5f, cy = (rect.top + rect.bottom) * 0.5f;
        return D2D1::RectF(cx - fw * 0.5f, cy - fh * 0.5f, cx + fw * 0.5f, cy + fh * 0.5f);
    }

    void DrawAlbumArt(const MediaSnapshot& media, D2D1_RECT_F rect, double now, float radius = 9.0f, bool drawBadge = true,
                      bool growLeft = false) {
        rect = ArtShape(media, rect, growLeft);  // Bunbun
        radius = std::min(radius, std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.18f);  // a short wide cover stays a card
        ComPtr<ID2D1Geometry> mask = GetShapeGeometry(rect, radius, false);
        ID2D1Layer* layer = AcquireLayer();
        const bool roundedClip = mask && layer;
        if (roundedClip) {
            target_->PushLayer(D2D1::LayerParameters(rect, mask.Get()), layer);
        } else {
            target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }

        if (media.art) {
            EnsureArtBitmap(media);

            D2D1_RECT_F dst = D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
            target_->DrawBitmap(artBitmap_.Get(), dst, 1.0f,
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            accentBrush_->SetOpacity(0.24f);
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), accentBrush_.Get());
            accentBrush_->SetOpacity(1.0f);
            if (media.sourceIcon) {
                D2D1_RECT_F iconRect = D2D1::RectF(rect.left + 11, rect.top + 11,
                                                  rect.right - 11, rect.bottom - 11);
                DrawBitmapPixels(*media.sourceIcon, iconRect, mediaSourceIconBitmap_,
                                 mediaSourceIconGeneration_, 0.95f);
            } else {
                target_->DrawTextW(media.sourceBadge.empty() ? L"\u25b6" : media.sourceBadge.c_str(),
                                   static_cast<UINT32>(media.sourceBadge.empty() ? 1 : media.sourceBadge.size()),
                                   textFormat_.Get(), rect, textBrush_.Get());
            }
        }

        // Bunbun: in the expanded player the cover and Bun (headphones on, vibing)
        // take turns while music plays, like the collapsed pill. Bun is drawn inside
        // the cover's rounded clip, and the app badge still goes on top of both.
        if (drawBadge && media.playing && g_settings.bunbunMascot && g_settings.bunbunMascotOnIsland) {
            const float mix = MascotCoverMix(now);
            if (mix > 0.01f) {
                ID2D1SolidColorBrush* plate = raisedStrongBrush_ ? raisedStrongBrush_.Get() : accentBrush_.Get();
                const float oldOpacity = plate->GetOpacity();
                plate->SetOpacity(mix);
                target_->FillRectangle(rect, plate);
                plate->SetOpacity(oldOpacity);
                ID2D1Layer* fade = mix < 0.99f ? AcquireLayer() : nullptr;
                if (fade) {
                    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                             D2D1::IdentityMatrix(), mix, nullptr, D2D1_LAYER_OPTIONS_NONE),
                                       fade);
                }
                // Bunbun: outline fit inside the slot, like the clock-side mascot.
                const float side = std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.86f;
                const D2D1_POINT_2F mid = D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f + 2.0f);
                D2D1_POINT_2F c{};
                float size = 0.0f;
                bb::mascot::FitToBox(D2D1::RectF(mid.x - side * 0.5f, mid.y - side * 0.5f, mid.x + side * 0.5f, mid.y + side * 0.5f), &c, &size);
                double moodSince = 0.0;  // Bunbun: a logged glass plays here too
                const bb::mascot::Mood coverMood = WithWaterMood(MediaMascotMood(media, g_settings), now, &moodSince);
                bb::mascot::Draw(target_.Get(), c, size, coverMood, now, moodSince, MascotLook(c));
                if (fade) target_->PopLayer();
            }
        }

        if (drawBadge && artBadgeAlpha_ > 0.01f) {
            if (media.sourceIcon) {
                D2D1_RECT_F badge = D2D1::RectF(rect.right - 24, rect.bottom - 22,
                                               rect.right - 3, rect.bottom - 3);
                DrawCircularBitmapPixels(*media.sourceIcon,
                                         D2D1::Point2F((badge.left + badge.right) * 0.5f,
                                                       (badge.top + badge.bottom) * 0.5f),
                                         9.5f, mediaSourceIconBitmap_,
                                         mediaSourceIconGeneration_, 0.98f * artBadgeAlpha_);
            }

            // Bunbun: sleek subscript badge (e.g. "Spotify") at the bottom of the cover art
            if (!media.sourceName.empty() && smallTextFormat_ && textBrush_) {
                const float textW = std::min(rect.right - rect.left - 8.0f, 68.0f);
                const float pillH = 14.0f;
                D2D1_RECT_F subPill = D2D1::RectF(rect.left + 4.0f, rect.bottom - pillH - 4.0f,
                                                 rect.left + 4.0f + textW, rect.bottom - 4.0f);
                if (scratchColorBrush_) {
                    // 0x0C0E12 as one RGB value; ColorF(0x0C, 0x0E, 0x12, a) reads those as
                    // 0-1 floats (12, 14, 18) and clamps to white, hiding the white text.
                    scratchColorBrush_->SetColor(D2D1::ColorF(0x0C0E12, 0.72f * artBadgeAlpha_));
                    target_->FillRoundedRectangle(D2D1::RoundedRect(subPill, 3.5f, 3.5f), scratchColorBrush_.Get());
                }
                const float oldTextAlpha = textBrush_->GetOpacity();
                textBrush_->SetOpacity(0.88f * artBadgeAlpha_);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                target_->DrawTextW(media.sourceName.c_str(), static_cast<UINT32>(media.sourceName.size()),
                                   smallTextFormat_.Get(), subPill, textBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                textBrush_->SetOpacity(oldTextAlpha);
            }
        }

        if (roundedClip) {
            target_->PopLayer();
        } else {
            target_->PopAxisAlignedClip();
        }
    }

    void DrawBitmapPixels(const BitmapPixels& pixels, D2D1_RECT_F rect,
                          ComPtr<ID2D1Bitmap>& cache, uint64_t& cachedGeneration,
                          float opacity = 1.0f) {
        if (pixels.bgra.empty()) {
            return;
        }

        if (cachedGeneration != pixels.generation || !cache) {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            target_->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height),
                                  pixels.bgra.data(), pixels.width * 4,
                                  &props, &cache);
            cachedGeneration = pixels.generation;
        }

        if (cache) {
            target_->DrawBitmap(cache.Get(), rect, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }

    // Draws a bitmap filling a rounded-rect badge with a small inset for polish.
    void DrawRoundedBitmapPixels(const BitmapPixels& pixels, D2D1_RECT_F badge,
                                 float cornerRadius,
                                 ComPtr<ID2D1Bitmap>& cache, uint64_t& cachedGeneration,
                                 float opacity = 1.0f) {
        if (pixels.bgra.empty()) return;

        // 2px inset so the icon has clean edges inside the badge.
        const float pad = 2.0f;
        D2D1_RECT_F iconRect = D2D1::RectF(badge.left + pad, badge.top + pad,
                                           badge.right - pad, badge.bottom - pad);
        const float innerR = std::max(0.0f, cornerRadius - pad);

        ComPtr<ID2D1RoundedRectangleGeometry> mask;
        d2dFactory_->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(iconRect, innerR, innerR), &mask);
        ComPtr<ID2D1Layer> layer;
        target_->CreateLayer(nullptr, &layer);

        if (mask && layer) {
            target_->PushLayer(
                D2D1::LayerParameters(iconRect, mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                layer.Get());
            DrawBitmapPixels(pixels, iconRect, cache, cachedGeneration, opacity);
            target_->PopLayer();
        } else {
            DrawBitmapPixels(pixels, iconRect, cache, cachedGeneration, opacity);
        }
    }

    // Like DrawRoundedBitmapPixels, but center-crops (cover-fit) instead of
    // stretching, so a non-square clipboard image isn't squashed into the
    // square badge.
    void DrawCoverFitBitmapPixels(const BitmapPixels& pixels, D2D1_RECT_F badge,
                                  float cornerRadius,
                                  ComPtr<ID2D1Bitmap>& cache, uint64_t& cachedGeneration,
                                  float opacity = 1.0f) {
        if (pixels.bgra.empty() || !pixels.width || !pixels.height) return;

        const float pad = 2.0f;
        D2D1_RECT_F iconRect = D2D1::RectF(badge.left + pad, badge.top + pad,
                                           badge.right - pad, badge.bottom - pad);
        const float innerR = std::max(0.0f, cornerRadius - pad);
        const float boxW = iconRect.right - iconRect.left;
        const float boxH = iconRect.bottom - iconRect.top;
        if (boxW <= 0.0f || boxH <= 0.0f) return;

        const float srcAspect = static_cast<float>(pixels.width) / static_cast<float>(pixels.height);
        const float boxAspect = boxW / boxH;
        float drawW = boxW;
        float drawH = boxH;
        if (srcAspect > boxAspect) {
            drawH = boxH;
            drawW = boxH * srcAspect;
        } else {
            drawW = boxW;
            drawH = boxW / srcAspect;
        }
        const float cx = (iconRect.left + iconRect.right) * 0.5f;
        const float cy = (iconRect.top + iconRect.bottom) * 0.5f;
        D2D1_RECT_F drawRect = D2D1::RectF(cx - drawW * 0.5f, cy - drawH * 0.5f,
                                           cx + drawW * 0.5f, cy + drawH * 0.5f);

        ComPtr<ID2D1RoundedRectangleGeometry> mask;
        d2dFactory_->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(iconRect, innerR, innerR), &mask);
        ComPtr<ID2D1Layer> layer;
        target_->CreateLayer(nullptr, &layer);

        if (mask && layer) {
            target_->PushLayer(
                D2D1::LayerParameters(iconRect, mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                layer.Get());
            DrawBitmapPixels(pixels, drawRect, cache, cachedGeneration, opacity);
            target_->PopLayer();
        } else {
            DrawBitmapPixels(pixels, drawRect, cache, cachedGeneration, opacity);
        }
    }

    void DrawCircularBitmapPixels(const BitmapPixels& pixels, D2D1_POINT_2F center, float radius,
                                  ComPtr<ID2D1Bitmap>& cache, uint64_t& cachedGeneration,
                                  float opacity = 1.0f) {
        if (pixels.bgra.empty()) {
            return;
        }

        D2D1_RECT_F rect = D2D1::RectF(center.x - radius, center.y - radius,
                                      center.x + radius, center.y + radius);
        ComPtr<ID2D1EllipseGeometry> ellipse;
        d2dFactory_->CreateEllipseGeometry(D2D1::Ellipse(center, radius, radius), &ellipse);
        ComPtr<ID2D1Layer> layer;
        target_->CreateLayer(nullptr, &layer);

        if (ellipse && layer) {
            target_->PushLayer(D2D1::LayerParameters(
                                  rect, ellipse.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                              layer.Get());
            DrawBitmapPixels(pixels, rect, cache, cachedGeneration, opacity);
            target_->PopLayer();
        } else {
            DrawBitmapPixels(pixels, rect, cache, cachedGeneration, opacity);
        }

        ComPtr<ID2D1SolidColorBrush> border;
        target_->CreateSolidColorBrush(material_.hairline, &border);
        if (border) {
            target_->DrawEllipse(D2D1::Ellipse(center, radius, radius), border.Get(), 1.0f);
        }
    }

    void DrawMarqueeText(const std::wstring& text, D2D1_RECT_F rect, IDWriteTextFormat* format,
                         ID2D1Brush* brush, double now, float speed, MarqueeLayoutCache& cache) {
        if (!format || !brush || text.empty()) {
            return;
        }

        const float wrapHeight = rect.bottom - rect.top;
        // Only rebuild the layout when text/format/height actually changed.
        // Scroll offset is applied via the translated draw origin below, so
        // it never invalidates the cache — this is what lets the marquee
        // scroll every frame without calling CreateTextLayout every frame.
        if (cache.text != text || cache.format != format ||
            std::fabs(cache.wrapWidth - wrapHeight) > 0.01f || !cache.layout) {
            cache.layout.Reset();
            dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                             format, 2000.0f, wrapHeight, &cache.layout);
            if (cache.text != text) cache.startedAt = now;  // Bunbun: new text starts from its beginning
            cache.text = text;
            cache.format = format;
            cache.wrapWidth = wrapHeight;
            cache.metrics = {};
            if (cache.layout) {
                cache.layout->GetMetrics(&cache.metrics);
            }
        }

        if (!cache.layout) {
            return;
        }

        const float available = rect.right - rect.left;

        D2D1_RECT_F clipRect = rect;
        clipRect.top -= 10.0f;
        clipRect.bottom += 10.0f;
        target_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (cache.metrics.widthIncludingTrailingWhitespace <= available) {
            target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), cache.layout.Get(), brush,
                                    D2D1_DRAW_TEXT_OPTIONS_NONE);
        } else {
            const float cycle = cache.metrics.widthIncludingTrailingWhitespace + 38.0f;
            // Bunbun: start at the first word and rest there 1.5 s each loop. The offset
            // used to come from the clock alone, so a new line often appeared mid-word.
            constexpr double kHold = 1.5;
            if (cache.startedAt < 0.0) cache.startedAt = now;
            const double period = kHold + cycle / std::max(1.0f, speed);
            const double phase = std::fmod(now - cache.startedAt, period);
            const float offset = phase < kHold ? 0.0f : static_cast<float>((phase - kHold) * speed);
            target_->DrawTextLayout(D2D1::Point2F(rect.left - offset, rect.top), cache.layout.Get(),
                                    brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
            target_->DrawTextLayout(D2D1::Point2F(rect.left - offset + cycle, rect.top),
                                    cache.layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
        target_->PopAxisAlignedClip();
    }

    // Bunbun: the default music bars, refined. A few fat, fully rounded bars (40 %
    // of each slot is gap) driven by the analyzer's log-spaced bands, low to high,
    // so they follow the music rather than raw samples. Each bar rises quickly and
    // falls slowly, glows softly in the album colour, and settles to a flat row when
    // nothing is playing. The skip ripple is unchanged.
    float waveLevel_[16] = {};

    void DrawWaveform(const SharedState& state, D2D1_RECT_F rect, float dt) {
        const float availableW = rect.right - rect.left;
        const float height = rect.bottom - rect.top;
        if (availableW <= 2.0f || height <= 2.0f) return;
        // About one bar per 7 px, between 4 and 16 bars.
        const size_t count = static_cast<size_t>(Clamp(std::floor(availableW / 7.0f), 4.0f, 16.0f));
        const float slot = availableW / static_cast<float>(count);
        const float barWidth = std::max(2.0f, slot * 0.6f);
        const float centerY = (rect.top + rect.bottom) * 0.5f;
        const float maxH = height * 0.92f;

        // Per-frame rates of 50 % up and 16 % down at 60 fps, made frame-rate independent.
        const float up = 1.0f - std::pow(0.50f, dt * 60.0f);
        const float down = 1.0f - std::pow(0.84f, dt * 60.0f);
        const bool playing = state.media.playing;

        const float t = specT_;
        for (size_t i = 0; i < count; ++i) {
            // Bands are log-spaced already; skip the very top ones, which are mostly empty.
            const float from = static_cast<float>(i) * (kSpectrumBands * 0.85f) / static_cast<float>(count);
            const float to = static_cast<float>(i + 1) * (kSpectrumBands * 0.85f) / static_cast<float>(count);
            // Higher bands carry less energy; lift them so the row isn't lopsided.
            const float tilt = 1.0f + 0.9f * static_cast<float>(i) / static_cast<float>(count);
            const float target = playing ? Clamp(SpecBandRange(state, from, to) * 1.6f * tilt, 0.0f, 1.0f) : 0.0f;
            float& level = waveLevel_[i];
            level += (target - level) * (target > level ? up : down);
            if (std::fabs(target - level) > 0.004f) g_layoutDirty = true;

            float h = std::max(barWidth, (0.10f + 0.90f * level) * maxH);
            float glow = 0.0f;

            // Track change: domino ripple. Each bar collapses to a dot, then springs
            // back up with overshoot; the ripple runs with the skip direction.
            if (t >= 0.0f && count > 1) {
                float f = static_cast<float>(i) / static_cast<float>(count - 1);
                if (specDir_ < 0.0f) f = 1.0f - f;
                const float u = (t - 0.34f * f) / 0.66f;
                float scale = 1.0f;
                if (u < 0.0f) {
                    scale = 1.0f;
                } else if (u < 0.22f) {
                    scale = 1.0f - SkipSmooth(u / 0.22f);
                } else {
                    const float v = Clamp((u - 0.22f) / 0.78f, 0.0f, 1.0f);
                    scale = 1.0f - std::exp(-5.5f * v) * std::cos(9.5f * v);
                    glow = std::exp(-6.0f * v);
                }
                h = std::max(barWidth * 0.8f, h * Clamp(scale, 0.0f, 1.5f));
            }

            const float x = rect.left + slot * static_cast<float>(i) + (slot - barWidth) * 0.5f;
            const D2D1_RECT_F bar = D2D1::RectF(x, centerY - h * 0.5f, x + barWidth, centerY + h * 0.5f);
            const float r = barWidth * 0.5f;
            // Glow: a slightly larger, faint copy behind the bar.
            const float halo = std::max(1.2f, barWidth * 0.45f);
            accentBrush_->SetOpacity(Clamp(0.10f + 0.22f * level + 0.25f * glow, 0.0f, 0.5f));
            target_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(bar.left - halo, bar.top - halo, bar.right + halo, bar.bottom + halo),
                                  r + halo, r + halo),
                accentBrush_.Get());
            accentBrush_->SetOpacity(Clamp((playing ? 0.62f : 0.40f) + 0.38f * level + 0.3f * glow, 0.0f, 1.0f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(bar, r, r), accentBrush_.Get());
        }
        accentBrush_->SetOpacity(1.0f);
    }

    void DrawCountdownProgress(float left, float right, float bottom, float progress) {
        if (!g_settings.statusCountdownProgress) return;
        const float h = 2.5f;
        D2D1_RECT_F track = D2D1::RectF(left, bottom - h, right, bottom);
        ComPtr<ID2D1SolidColorBrush> trackBrush;
        target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.08f), &trackBrush);
        target_->FillRoundedRectangle(D2D1::RoundedRect(track, 1.25f, 1.25f), trackBrush.Get());
        D2D1_RECT_F fill = D2D1::RectF(track.left, track.top,
                                       track.left + (track.right - track.left) * Clamp(progress, 0.0f, 1.0f),
                                       track.bottom);
        accentBrush_->SetOpacity(0.70f);
        target_->FillRoundedRectangle(D2D1::RoundedRect(fill, 1.25f, 1.25f), accentBrush_.Get());
        accentBrush_->SetOpacity(1.0f);
    }

    void DrawClipboard(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 40.0f || rect.right - rect.left < 100.0f) return;
        const double now = NowSeconds();
        const float ttl = 2.5f;
        const float remaining = Clamp(static_cast<float>(state.clipboard.expiresAt - now), 0.0f, ttl);
        const float progress = remaining / ttl;

        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14.0f, cy - badgeSz * 0.5f,
                                        rect.left + 14.0f + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        if (state.clipboard.image && state.clipboard.imagePreview) {
            DrawCoverFitBitmapPixels(*state.clipboard.imagePreview,
                                     badge, br,
                                     clipboardImageBitmap_,
                                     clipboardImageGeneration_, 1.0f);
        } else if (state.clipboard.appIcon) {
            DrawRoundedBitmapPixels(*state.clipboard.appIcon,
                                    badge, br,
                                    clipboardIconBitmap_,
                                    clipboardIconGeneration_, 0.96f);
        } else {
            const wchar_t* glyph = state.clipboard.image
                ? (usingFluentIcons_ ? L"\uE91B" : L"\uE114")
                : (usingFluentIcons_ ? L"\uF0E3" : L"\uE8C8");
            textBrush_->SetOpacity(0.95f);

            if (iconFormat_) {
                iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                target_->DrawTextW(glyph,
                                   static_cast<UINT32>(wcslen(glyph)), iconFormat_.Get(), badge,
                                   textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }

            textBrush_->SetOpacity(0.90f);
        }

        const float tx = badge.right + 14.0f;
        const bool showBar = g_settings.statusCountdownProgress;
        D2D1_RECT_F titleRect = showBar
            ? D2D1::RectF(tx, cy - 18.0f, rect.right - 14.0f, cy - 2.0f)
            : D2D1::RectF(tx, cy - 16.0f, rect.right - 14.0f, cy - 1.0f);
        mutedBrush_->SetOpacity(0.48f);
        const std::wstring clipTitle =
            state.clipboard.appName.empty()
                ? (state.clipboard.image ? std::wstring(L"Image copied") : std::wstring(L"Clipboard"))
                : state.clipboard.appName + L"  \u00b7  Clipboard";
        target_->DrawTextW(clipTitle.c_str(), static_cast<UINT32>(clipTitle.size()),
                           smallTextFormat_.Get(), titleRect, mutedBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        D2D1_RECT_F textRect = showBar
            ? D2D1::RectF(tx, cy - 2.0f, rect.right - 14.0f, cy + 15.0f)
            : D2D1::RectF(tx, cy - 1.0f, rect.right - 14.0f, cy + 16.0f);
        DrawMarqueeText(state.clipboard.text.empty() ? std::wstring(Loc(L"Copied")) : state.clipboard.text,
                        textRect, textFormat_.Get(), textBrush_.Get(), now, 34.0f, marqueeClipboardCache_);

        DrawCountdownProgress(tx, rect.right - 14.0f, rect.bottom - 6.0f, progress);
        mutedBrush_->SetOpacity(0.58f);
    }

