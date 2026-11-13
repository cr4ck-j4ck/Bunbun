// Bunbun: Renderer: time dashboard, tray buttons and game overlay.
// Split out of island_core.cpp; included inside class Renderer at the
// original position, so declaration order is unchanged. Not a standalone file.

        // Karaoke effect: a soft wave rides the wipe boundary across the active line.
    // The layout and per-cluster rects are cached (karaokeCache_) and only rebuilt
    // when the line changes.
    void DrawKaraokeLetterPop(const wchar_t* text, UINT32 textLength, IDWriteTextFormat* format,
                              D2D1_RECT_F lineRect, float wipeX, float baseOpacity,
                              float unsungOpacity, bool glowEnabled, float glowStrength = 1.0f) {
        if (!text || textLength == 0 || !format || baseOpacity <= 0.01f) {
            return;
        }

        const float layoutWidth = lineRect.right - lineRect.left;
        const float layoutHeight = lineRect.bottom - lineRect.top;
        if (layoutWidth <= 0.0f || layoutHeight <= 0.0f) {
            return;
        }

        const std::wstring textView(text, textLength);
        if (karaokeCache_.text != textView || karaokeCache_.format != format ||
            std::fabs(karaokeCache_.layoutWidth - layoutWidth) > 0.5f ||
            std::fabs(karaokeCache_.layoutHeight - layoutHeight) > 0.5f ||
            !karaokeCache_.layout) {
            karaokeCache_.layout.Reset();
            karaokeCache_.clusters.clear();
            karaokeCache_.valid = false;
            karaokeCache_.text = textView;
            karaokeCache_.format = format;
            karaokeCache_.layoutWidth = layoutWidth;
            karaokeCache_.layoutHeight = layoutHeight;

            dwriteFactory_->CreateTextLayout(text, textLength, format, layoutWidth, layoutHeight,
                                             &karaokeCache_.layout);
            if (karaokeCache_.layout) {
                // Walk shaped glyph clusters (not raw UTF-16 units) so scripts like
                // Devanagari, where one letter is several code units, stay intact.
                UINT32 clusterCount = 0;
                karaokeCache_.layout->GetClusterMetrics(nullptr, 0, &clusterCount);
                if (clusterCount > 0) {
                    std::vector<DWRITE_CLUSTER_METRICS> rawClusters(clusterCount);
                    if (SUCCEEDED(karaokeCache_.layout->GetClusterMetrics(rawClusters.data(), clusterCount,
                                                                          &clusterCount))) {
                        karaokeCache_.clusters.reserve(clusterCount);
                        UINT32 clusterStart = 0;
                        for (UINT32 c = 0; c < clusterCount; ++c) {
                            const UINT32 clusterLength = rawClusters[c].length;
                            const UINT32 i = clusterStart;
                            clusterStart += clusterLength;

                            KaraokeLayoutCache::ClusterInfo info;
                            info.textStart = i;
                            info.textLength = clusterLength;
                            info.isWhitespace = rawClusters[c].isWhitespace || clusterLength == 0;

                            if (!info.isWhitespace) {
                                DWRITE_HIT_TEST_METRICS hit{};
                                UINT32 hitCount = 0;
                                if (SUCCEEDED(karaokeCache_.layout->HitTestTextRange(
                                        i, clusterLength, 0.0f, 0.0f, &hit, 1, &hitCount)) &&
                                    hitCount > 0 && hit.width > 0.0f) {
                                    info.rect = D2D1::RectF(hit.left, hit.top, hit.left + hit.width,
                                                            hit.top + hit.height);
                                } else {
                                    info.isWhitespace = true;
                                }
                            }

                            karaokeCache_.clusters.push_back(info);
                        }
                        karaokeCache_.valid = true;
                    }
                }
            }
        }

        if (!karaokeCache_.valid) {
            // Plain two-pass wipe if the layout could not be built.
            mutedBrush_->SetOpacity(baseOpacity * unsungOpacity);
            target_->DrawTextW(text, textLength, format, lineRect, mutedBrush_.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (wipeX > lineRect.left) {
                D2D1_RECT_F sungClip = D2D1::RectF(lineRect.left, lineRect.top,
                                                   std::min(wipeX, lineRect.right), lineRect.bottom);
                target_->PushAxisAlignedClip(sungClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                textBrush_->SetOpacity(baseOpacity);
                target_->DrawTextW(text, textLength, format, lineRect, textBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
                target_->PopAxisAlignedClip();
            }
            return;
        }

        constexpr float kWaveTrailWidth = 150.0f;
        constexpr float kWaveLeadWidth = 46.0f;
        constexpr int kGlowStampCount = 4;
        constexpr float kGlowRadius = 0.85f;
        constexpr float kGlowAlpha = 0.075f;

        for (const auto& cluster : karaokeCache_.clusters) {
            if (cluster.isWhitespace) {
                continue;
            }

            const D2D1_RECT_F hitRect = D2D1::RectF(
                cluster.rect.left + lineRect.left, cluster.rect.top + lineRect.top,
                cluster.rect.right + lineRect.left, cluster.rect.bottom + lineRect.top);

            const float charCenterX = (hitRect.left + hitRect.right) * 0.5f;
            const float distFromWipe = charCenterX - wipeX;  // <= 0 means already sung

            float pop;
            if (distFromWipe <= 0.0f) {
                const float t = Clamp(-distFromWipe / kWaveTrailWidth, 0.0f, 1.0f);
                pop = 0.5f * (1.0f + std::cos(t * 3.14159265f));
            } else {
                const float t = Clamp(distFromWipe / kWaveLeadWidth, 0.0f, 1.0f);
                pop = 0.5f * (1.0f + std::cos(t * 3.14159265f));
            }

            const bool sung = distFromWipe <= 0.0f;
            D2D1_RECT_F charRect = D2D1::RectF(hitRect.left, lineRect.top, lineRect.right, lineRect.bottom);
            const wchar_t* clusterText = text + cluster.textStart;

            if (glowEnabled && glowStrength > 0.01f && pop > 0.02f) {
                const float alpha = kGlowAlpha * pop * baseOpacity * glowStrength;
                if (alpha > 0.003f) {
                    textBrush_->SetOpacity(alpha);
                    for (int s = 0; s < kGlowStampCount; ++s) {
                        const float a = (2.0f * 3.14159265f * s) / kGlowStampCount;
                        const float ox = std::cos(a) * kGlowRadius;
                        const float oy = std::sin(a) * kGlowRadius;
                        D2D1_RECT_F stampRect = D2D1::RectF(
                            charRect.left + ox, charRect.top + oy,
                            charRect.right + ox, charRect.bottom + oy);
                        target_->DrawTextW(clusterText, cluster.textLength, format, stampRect,
                                           textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                    textBrush_->SetOpacity(baseOpacity);
                }
            }

            if (sung) {
                textBrush_->SetOpacity(baseOpacity);
                target_->DrawTextW(clusterText, cluster.textLength, format, charRect, textBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
            } else {
                mutedBrush_->SetOpacity(baseOpacity * unsungOpacity);
                target_->DrawTextW(clusterText, cluster.textLength, format, charRect, mutedBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    }

    void DrawLyricsDashboard(const SharedState& state, D2D1_RECT_F rect, const Settings& settings,
                             float scale) {
        const double now = NowSeconds();

        // If the tab wasn't on screen a moment ago (island was collapsed), its scroll
        // position, highlight and visualizer fade are stale. Snap them to the current
        // state instead of animating them while the island is still expanding.
        {
            const bool lyricsFresh = lyricsLastDrawn_ >= 0.0 && (now - lyricsLastDrawn_) < 0.25;
            lyricsLastDrawn_ = now;
            g_lastLyricsViewTime.store(now, std::memory_order_relaxed);
            if (!lyricsFresh) {
                lyricsScrollAnimStartTime_ = -1.0;  // triggers the snap branch below
                lyricsWaveformFadeTime_ = -1.0;     // snaps the visualizer alpha
            }
        }

        // Lyric effects ramp: 0 while the island is still resizing, then eases to 1 over 0.3s.
        // A fresh open of the tab on an already-settled island goes straight to 1 (old behaviour).
        {
            const bool fxRecent = lyricsFxLast_ >= 0.0 && (now - lyricsFxLast_) < 0.25;
            const float fxDt = fxRecent ? Clamp(static_cast<float>(now - lyricsFxLast_), 0.001f, 0.05f) : 0.0f;
            lyricsFxLast_ = now;
            if (!islandSettled_) {
                lyricsFxMix_ = 0.0f;
            } else if (!fxRecent) {
                lyricsFxMix_ = 1.0f;
            } else {
                lyricsFxMix_ = std::min(1.0f, lyricsFxMix_ + fxDt / 0.30f);
            }
            if (!islandSettled_ || lyricsFxMix_ < 1.0f) {
                g_layoutDirty = true;  // keep frames coming until the effects are fully in
            }
        }

        // ── Header: title on the left, tiny live visualizer on the right ─────
        textBrush_->SetOpacity(0.96f);
        if (boldTextFormat_) {
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            const wchar_t* title = Loc(L"Lyrics");
            target_->DrawTextW(title, static_cast<UINT32>(wcslen(title)), boldTextFormat_.Get(),
                               D2D1::RectF(rect.left + 26.0f * scale, rect.top + 14.0f * scale,
                                           rect.right - 96.0f * scale, rect.top + 36.0f * scale),
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        {
            const float waveformTarget = (state.media.playing && settings.lyricsVisualizer) ? 1.0f : 0.0f;
            if (lyricsWaveformFadeTime_ < 0.0) {
                lyricsWaveformAlpha_ = waveformTarget;
            } else {
                const float dt = Clamp(static_cast<float>(now - lyricsWaveformFadeTime_), 0.0f, 0.25f);
                const float t = 1.0f - std::exp(-8.0f * dt);
                lyricsWaveformAlpha_ += (waveformTarget - lyricsWaveformAlpha_) * t;
            }
            lyricsWaveformFadeTime_ = now;

            if (lyricsWaveformAlpha_ > 0.01f) {
                const D2D1_RECT_F waveRect = D2D1::RectF(rect.right - 24.0f * scale - 56.0f * scale,
                                                         rect.top + 17.0f * scale,
                                                         rect.right - 24.0f * scale,
                                                         rect.top + 35.0f * scale);
                ID2D1Layer* waveFadeLayer = AcquireLayer();
                if (waveFadeLayer) {
                    target_->PushLayer(D2D1::LayerParameters(waveRect, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                              D2D1::IdentityMatrix(), lyricsWaveformAlpha_, nullptr,
                                                              D2D1_LAYER_OPTIONS_NONE),
                                       waveFadeLayer);
                    DrawSpectrum(state, waveRect, settings, now);
                    target_->PopLayer();
                }
            }
        }

        const D2D1_RECT_F bodyRect = D2D1::RectF(rect.left + 24.0f * scale, rect.top + 44.0f * scale,
                                                 rect.right - 24.0f * scale, rect.bottom - 14.0f * scale);

        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        // Bunbun: the previous song's lyrics, while the new song's are still on their way
        // (the fetch waits ~1.5 s for the track to settle), read as "fetching" rather
        // than being shown against the new song's position.
        const bool otherSong = state.lyrics.hasData && !state.lyrics.matchedTitle.empty() &&
                               (state.lyrics.matchedTitle != state.media.title ||
                                state.lyrics.matchedArtist != state.media.artist);
        if (state.lyrics.fetching || otherSong) {
            const float pulse = 0.55f + 0.25f * std::sin(static_cast<float>(now * 2.0 * 3.14159265 * 0.6));
            mutedBrush_->SetOpacity(pulse);
            const wchar_t* msg = Loc(L"Fetching lyrics...");
            target_->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), textFormat_.Get(), bodyRect,
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            mutedBrush_->SetOpacity(0.75f);
            return;
        }

        if (!state.lyrics.hasData || state.lyrics.lines.empty()) {
            mutedBrush_->SetOpacity(0.55f);
            // Bunbun: instrumentals say so instead of "no lyrics".
            const wchar_t* msg = state.lyrics.instrumental ? Loc(L"Instrumental")
                                                           : Loc(L"No lyrics found for this track.");
            target_->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), textFormat_.Get(), bodyRect,
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            mutedBrush_->SetOpacity(0.75f);
            return;
        }

        if (state.lyrics.synced) {
            double currentPositionSec = state.media.positionTicks / 10000000.0;
            if (state.media.playing && state.media.lastUpdatedTicks > 0) {
                currentPositionSec += (GetTickCount64() - state.media.lastUpdatedTicks) / 1000.0;
            }

            // The reported position lags what you hear by a bit; nudge it forward.
            // Raise this if lyrics feel late, lower it if they feel early.
            const double kLyricsLookaheadSec = LyricsLookaheadSec();  // 0.49 baseline + speaker/Bluetooth offset
            // Bunbun: paused too (it was only while playing), or pausing pulled the
            // position back by the lookahead and the line could drop back one.
            currentPositionSec += kLyricsLookaheadSec;
            const int64_t posMs = std::max<int64_t>(0, static_cast<int64_t>(currentPositionSec * 1000.0));

            int currentIndex = 0;
            for (size_t i = 0; i < state.lyrics.lines.size(); ++i) {
                if (state.lyrics.lines[i].timeMs <= posMs) {
                    currentIndex = static_cast<int>(i);
                } else {
                    break;
                }
            }

            // Bunbun: in a long instrumental break the active line fades to the note glyph.
            const float gapNoteMix = LyricsGapNoteMix(state.lyrics.lines, currentIndex, posMs);

            // Fixed-duration ease-in-out scroll toward the active line.
            constexpr float kScrollAnimDurationSec = 0.55f;
            constexpr float kHighlightFadeDurationSec = 0.65f;
            const std::wstring trackKey = state.lyrics.matchedArtist + L"|" + state.lyrics.matchedTitle;
            // While the island is resizing, keep scroll/highlight snapped to the current line
            // (same snap as a fresh open). Animations resume once it settles, and the glow
            // decay starts from that moment.
            if (trackKey != lyricsScrollTrackKey_ || lyricsScrollAnimStartTime_ < 0.0 ||
                lyricsFxMix_ <= 0.0f) {
                lyricsScrollPos_ = static_cast<float>(currentIndex);
                lyricsScrollAnimStartPos_ = lyricsScrollPos_;
                lyricsScrollAnimTargetPos_ = lyricsScrollPos_;
                lyricsScrollAnimStartTime_ = now;
                lyricsScrollTrackKey_ = trackKey;
                lyricsHighlightIndex_ = currentIndex;
                lyricsHighlightPrevIndex_ = currentIndex;
                lyricsHighlightFadeStartTime_ = now - kHighlightFadeDurationSec;
            } else {
                if (static_cast<float>(currentIndex) != lyricsScrollAnimTargetPos_) {
                    lyricsScrollAnimStartPos_ = lyricsScrollPos_;
                    lyricsScrollAnimTargetPos_ = static_cast<float>(currentIndex);
                    lyricsScrollAnimStartTime_ = now;

                    lyricsHighlightPrevIndex_ = lyricsHighlightIndex_;
                    lyricsHighlightIndex_ = currentIndex;
                    lyricsHighlightFadeStartTime_ = now;
                }

                const float elapsed = static_cast<float>(now - lyricsScrollAnimStartTime_);
                const float t = Clamp(elapsed / kScrollAnimDurationSec, 0.0f, 1.0f);
                lyricsScrollPos_ = lyricsScrollAnimStartPos_ +
                    (lyricsScrollAnimTargetPos_ - lyricsScrollAnimStartPos_) * SmoothStep01(t);
            }

            constexpr int kContextLines = 2;
            const float lineHeight = 42.0f * scale;
            const float centerY = (bodyRect.top + bodyRect.bottom) * 0.5f;
            const int baseIndex = static_cast<int>(std::floor(lyricsScrollPos_));

            // Larger, heavier type for the focused line. Honors the Font family
            // and Text size settings.
            constexpr float kLyricsActiveFontSize = 19.5f;
            const float activeFontSize = kLyricsActiveFontSize * scale * settings.textScale;
            const wchar_t* activeFamily =
                settings.fontFamily.empty() ? L"Segoe UI Variable Display" : settings.fontFamily.c_str();

            auto makeActiveFormat = [&](float size, ComPtr<IDWriteTextFormat>& out) {
                out.Reset();
                if (!dwriteFactory_) {
                    return;
                }
                HRESULT hr = dwriteFactory_->CreateTextFormat(
                    activeFamily, nullptr, DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, size, L"", &out);
                if (FAILED(hr) || !out) {
                    out.Reset();
                    dwriteFactory_->CreateTextFormat(
                        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL, size, L"", &out);
                }
                if (out) {
                    out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
                karaokeCache_.layout.Reset();  // cache is keyed by format pointer
            };

            if (std::fabs(lyricsActiveTextFormatSize_ - activeFontSize) > 0.01f || !lyricsActiveTextFormat_) {
                makeActiveFormat(activeFontSize, lyricsActiveTextFormat_);
                lyricsActiveTextFormatSize_ = activeFontSize;
            }
            IDWriteTextFormat* activeFormat =
                lyricsActiveTextFormat_ ? lyricsActiveTextFormat_.Get() : boldTextFormat_.Get();

            const float kMaxNeighborOpacity = settings.lyricsNeighborLinesVisible ? 0.55f : 0.22f;
            const bool instantHighlightSwitch = settings.lyricsNeighborLinesVisible;
            constexpr float kKaraokeUnsungOpacity = 0.6f;
            constexpr float kKaraokeGlowTrailWidth = 46.0f;
            constexpr float kKaraokeGlowLeadWidth = 6.0f;
            constexpr float kKaraokeGlowPeakAlpha = 0.30f;

            constexpr float kFocusFalloffLines = 2.2f;
            auto EdgeFade = [&](float distanceFromCenter) -> float {
                const float t = 1.0f - Clamp(std::fabs(distanceFromCenter) / kFocusFalloffLines, 0.0f, 1.0f);
                return SmoothStep01(t);
            };

            for (int offset = -kContextLines; offset <= kContextLines; ++offset) {
                const int idx = baseIndex + offset;
                if (idx < 0 || idx >= static_cast<int>(state.lyrics.lines.size())) continue;

                const float distance = static_cast<float>(idx) - lyricsScrollPos_;
                const float y = centerY + distance * lineHeight;

                float edgeFade = EdgeFade(distance);
                if (edgeFade <= 0.01f) continue;

                D2D1_RECT_F lineRect = D2D1::RectF(
                    bodyRect.left, y - lineHeight * 0.5f,
                    bodyRect.right, y + lineHeight * 0.5f);

                // Bunbun: instrumental break. Cross-fade the line into a note glyph,
                // drawn where the active line sits.
                if (idx == currentIndex && gapNoteMix > 0.0f) {
                    if (activeFormat) {
                        const D2D1_RECT_F noteRect = D2D1::RectF(
                            lineRect.left, y - lineHeight * 0.66f, lineRect.right, y + lineHeight * 0.66f);
                        textBrush_->SetOpacity(0.9f * gapNoteMix * edgeFade);
                        target_->DrawTextW(L"♪", 1, activeFormat, noteRect, textBrush_.Get(),
                                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                    edgeFade *= 1.0f - gapNoteMix;
                    if (edgeFade <= 0.01f) continue;
                }

                const std::wstring& line = state.lyrics.lines[idx].text;
                const wchar_t* toDraw = line.empty() ? L"\u266A" : line.c_str();
                const UINT32 toDrawLen = static_cast<UINT32>(line.empty() ? 1 : line.size());

                float activeBlend = 0.0f;
                float glowPulse = 0.0f;
                float karaokeProgress = -1.0f;  // -1 = not the karaoke-wiped line right now
                if (idx == lyricsHighlightIndex_) {
                    const double sinceBecameActive = now - lyricsHighlightFadeStartTime_;
                    const float fadeT = Clamp(static_cast<float>(sinceBecameActive) / kHighlightFadeDurationSec, 0.0f, 1.0f);
                    activeBlend = instantHighlightSwitch ? 1.0f : SmoothStep01(fadeT);

                    constexpr float kGlowHoldSec = 0.15f;
                    constexpr float kGlowDecaySec = 0.85f;
                    if (sinceBecameActive < kHighlightFadeDurationSec) {
                        glowPulse = fadeT;
                    } else {
                        const double afterHold = sinceBecameActive - kHighlightFadeDurationSec - kGlowHoldSec;
                        if (afterHold < 0.0) {
                            glowPulse = 1.0f;
                        } else {
                            glowPulse = 1.0f - SmoothStep01(Clamp(static_cast<float>(afterHold / kGlowDecaySec), 0.0f, 1.0f));
                        }
                    }

                    if (settings.karaokeLyrics) {
                        const int64_t lineStartMs = state.lyrics.lines[idx].timeMs;
                        int64_t lineEndMs = lineStartMs + 4000;  // fallback for the last line
                        if (idx + 1 < static_cast<int>(state.lyrics.lines.size())) {
                            lineEndMs = state.lyrics.lines[idx + 1].timeMs;
                        } else if (state.media.endTicks > 0) {
                            lineEndMs = state.media.endTicks / 10000;
                        }
                        // Bunbun: before a long break, finish the wipe before the line fades to a note.
                        const int64_t gapHoldMs = LyricsGapHoldMs(state.lyrics.lines, idx);
                        if (gapHoldMs > 0) lineEndMs = std::min(lineEndMs, lineStartMs + gapHoldMs);
                        const int64_t span = lineEndMs - lineStartMs;
                        karaokeProgress = span > 0
                            ? Clamp(static_cast<float>(posMs - lineStartMs) / static_cast<float>(span), 0.0f, 1.0f)
                            : 1.0f;
                    }
                } else if (idx == lyricsHighlightPrevIndex_) {
                    if (instantHighlightSwitch) {
                        activeBlend = 0.0f;
                        glowPulse = 0.0f;
                    } else {
                        const float fadeT = Clamp(static_cast<float>(now - lyricsHighlightFadeStartTime_) / kHighlightFadeDurationSec, 0.0f, 1.0f);
                        activeBlend = std::pow(1.0f - SmoothStep01(fadeT), 1.4f);
                        glowPulse = 1.0f - SmoothStep01(fadeT);
                    }
                }

                // ── Bright / active pass ─────────────────────────────────────
                if (activeBlend > 0.01f) {
                    D2D1_RECT_F activeLineRect = D2D1::RectF(
                        lineRect.left, y - lineHeight * 0.66f,
                        lineRect.right, y + lineHeight * 0.66f);

                    const float availableWidth = activeLineRect.right - activeLineRect.left;
                    IDWriteTextFormat* drawFormat = activeFormat;

                    ComPtr<IDWriteTextLayout> measureLayout;
                    dwriteFactory_->CreateTextLayout(toDraw, toDrawLen, activeFormat,
                                                     availableWidth, lineHeight, &measureLayout);
                    float textWidth = availableWidth;
                    if (measureLayout) {
                        DWRITE_TEXT_METRICS metrics{};
                        measureLayout->GetMetrics(&metrics);
                        textWidth = metrics.widthIncludingTrailingWhitespace;
                    }

                    // Too wide for the active size: shrink just enough to fit.
                    if (textWidth > availableWidth && textWidth > 0.0f) {
                        const float fitFontSize = std::max(
                            9.0f, kLyricsActiveFontSize * scale * settings.textScale *
                                      (availableWidth / textWidth) * 0.96f);
                        if (std::fabs(lyricsActiveFitTextFormatSize_ - fitFontSize) > 0.1f ||
                            !lyricsActiveFitTextFormat_) {
                            makeActiveFormat(fitFontSize, lyricsActiveFitTextFormat_);
                            lyricsActiveFitTextFormatSize_ = fitFontSize;
                        }
                        if (lyricsActiveFitTextFormat_) {
                            drawFormat = lyricsActiveFitTextFormat_.Get();
                        }
                    }

                    if (karaokeProgress >= 0.0f) {
                        const float renderedTextWidth =
                            (drawFormat == lyricsActiveFitTextFormat_.Get())
                                ? availableWidth * 0.96f
                                : std::min(textWidth, availableWidth);
                        const float wipeX = activeLineRect.left + karaokeProgress * renderedTextWidth;

                        if (settings.karaokeLetterGlow) {
                            DrawKaraokeLetterPop(toDraw, toDrawLen, drawFormat, activeLineRect,
                                                 wipeX, activeBlend * edgeFade, kKaraokeUnsungOpacity,
                                                 settings.karaokeLetterGlow, lyricsFxMix_);
                        } else {
                            mutedBrush_->SetOpacity(activeBlend * edgeFade * kKaraokeUnsungOpacity);
                            target_->DrawTextW(toDraw, toDrawLen, drawFormat, activeLineRect,
                                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

                            if (wipeX > activeLineRect.left) {
                                D2D1_RECT_F sungClip = D2D1::RectF(
                                    activeLineRect.left, activeLineRect.top,
                                    std::min(wipeX, activeLineRect.right), activeLineRect.bottom);
                                target_->PushAxisAlignedClip(sungClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                                textBrush_->SetOpacity(activeBlend * edgeFade);
                                target_->DrawTextW(toDraw, toDrawLen, drawFormat, activeLineRect,
                                                   textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                                target_->PopAxisAlignedClip();
                            }
                        }

                        // Moving glow riding the wipe edge, drawn as text so it
                        // only lights actual glyph pixels.
                        if (karaokeProgress < 1.0f && lyricsFxMix_ > 0.01f) {
                            const float trailWidth = kKaraokeGlowTrailWidth * scale;
                            const float leadWidth = kKaraokeGlowLeadWidth * scale;
                            const float peakAlpha = kKaraokeGlowPeakAlpha * activeBlend * edgeFade * lyricsFxMix_;
                            const float peakOffset = trailWidth / (trailWidth + leadWidth);
                            D2D1_GRADIENT_STOP glowStops[3] = {
                                {0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f)},
                                {peakOffset, D2D1::ColorF(1.0f, 1.0f, 1.0f, peakAlpha)},
                                {1.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f)},
                            };
                            ComPtr<ID2D1GradientStopCollection> glowStopCollection;
                            target_->CreateGradientStopCollection(glowStops, 3, &glowStopCollection);
                            if (glowStopCollection) {
                                ComPtr<ID2D1LinearGradientBrush> glowBrush;
                                target_->CreateLinearGradientBrush(
                                    D2D1::LinearGradientBrushProperties(
                                        D2D1::Point2F(wipeX - trailWidth, y),
                                        D2D1::Point2F(wipeX + leadWidth, y)),
                                    glowStopCollection.Get(), &glowBrush);
                                if (glowBrush) {
                                    target_->DrawTextW(toDraw, toDrawLen, drawFormat, activeLineRect,
                                                       glowBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                                }
                            }
                        }
                    } else {
                        // Whole-line highlight (karaoke off, or the line fading out).
                        const float glowStrength = glowPulse * edgeFade * lyricsFxMix_;
                        if (glowStrength > 0.02f) {
                            constexpr int kGlowStampCount = 8;
                            constexpr float kGlowRadiusInner = 0.9f;
                            constexpr float kGlowRadiusOuter = 2.0f;
                            constexpr float kGlowRadiusBloom = 4.2f;
                            const float innerAlpha = 0.05f * glowStrength;
                            const float outerAlpha = 0.028f * glowStrength;
                            const float bloomAlpha = 0.016f * glowStrength;

                            for (int ring = 0; ring < 3; ++ring) {
                                const float radius = (ring == 0 ? kGlowRadiusInner
                                                       : ring == 1 ? kGlowRadiusOuter
                                                                   : kGlowRadiusBloom) * scale;
                                const float alpha = (ring == 0 ? innerAlpha
                                                      : ring == 1 ? outerAlpha
                                                                  : bloomAlpha);
                                if (alpha <= 0.002f) continue;

                                textBrush_->SetOpacity(alpha);
                                for (int i = 0; i < kGlowStampCount; ++i) {
                                    const float a = (2.0f * 3.14159265f * i) / kGlowStampCount;
                                    const float ox = std::cos(a) * radius;
                                    const float oy = std::sin(a) * radius;
                                    D2D1_RECT_F stampRect = D2D1::RectF(
                                        activeLineRect.left + ox, activeLineRect.top + oy,
                                        activeLineRect.right + ox, activeLineRect.bottom + oy);
                                    target_->DrawTextW(toDraw, toDrawLen, drawFormat, stampRect,
                                                       textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                                }
                            }
                        }

                        textBrush_->SetOpacity(activeBlend * edgeFade);
                        target_->DrawTextW(toDraw, toDrawLen, drawFormat, activeLineRect,
                                           textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                }

                // ── Dim / neighbor pass ──────────────────────────────────────
                const float dimOpacity = kMaxNeighborOpacity * (1.0f - activeBlend) * edgeFade;
                if (dimOpacity > 0.01f) {
                    mutedBrush_->SetOpacity(dimOpacity);
                    target_->DrawTextW(toDraw, toDrawLen, textFormat_.Get(), lineRect,
                                       mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }

            textBrush_->SetOpacity(0.96f);
            mutedBrush_->SetOpacity(0.75f);
        } else {
            // Unsynced (plain) lyrics: a simple static list.
            const float lineHeight = 19.0f * scale;
            const size_t maxLines = std::min<size_t>(
                state.lyrics.lines.size(),
                static_cast<size_t>((bodyRect.bottom - bodyRect.top) / lineHeight));

            textBrush_->SetOpacity(0.82f);
            for (size_t i = 0; i < maxLines; ++i) {
                D2D1_RECT_F lineRect = D2D1::RectF(
                    bodyRect.left, bodyRect.top + i * lineHeight,
                    bodyRect.right, bodyRect.top + (i + 1) * lineHeight);
                const std::wstring& line = state.lyrics.lines[i].text;
                target_->DrawTextW(line.c_str(), static_cast<UINT32>(line.size()), textFormat_.Get(),
                                   lineRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            textBrush_->SetOpacity(0.96f);
        }
    }

    void DrawTimeDashboard(const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now, float scale, SYSTEMTIME& local) {
        (void)now;

        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;

        if (settings.clockAccentGlow) {
            D2D1_COLOR_F accentColor = accentBrush_ ? accentBrush_->GetColor() : D2D1::ColorF(0x4cc9f0);
            D2D1_GRADIENT_STOP stops[2] = {
                {0.0f, D2D1::ColorF(accentColor.r, accentColor.g, accentColor.b, 0.24f)},
                {1.0f, D2D1::ColorF(accentColor.r, accentColor.g, accentColor.b, 0.0f)},
            };
            ComPtr<ID2D1GradientStopCollection> glowStops;
            target_->CreateGradientStopCollection(stops, 2, &glowStops);

            if (glowStops) {
                const D2D1_POINT_2F glowCenter = D2D1::Point2F(cx, cy - 8.0f * scale);
                ComPtr<ID2D1RadialGradientBrush> glowBrush;
                target_->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(glowCenter, D2D1::Point2F(0, 0),
                                                         150.0f * scale, 70.0f * scale),
                    glowStops.Get(), &glowBrush);
                if (glowBrush) {
                    target_->FillEllipse(D2D1::Ellipse(glowCenter, 150.0f * scale, 70.0f * scale), glowBrush.Get());
                }
            }
        }

        // Time and date as a single typographic block. `dateFirst` promotes the
        // date to the headline for people who care about it more than the clock
        // (#61); either way the pair stays optically centred as a unit.
        const std::wstring timeText = FormatIslandTime(local, settings.clockFollowSystem,
                                                       settings.use24HourClock, settings.showSeconds);
        const std::wstring dateText = FormatIslandDate(local, settings.dateFormat, L"dddd, MMMM d");

        IDWriteTextFormat* bigFmt = timeDashboardFormat_ ? timeDashboardFormat_.Get() : hugeTextFormat_.Get();
        IDWriteTextFormat* smallFmt = dateDashboardFormat_ ? dateDashboardFormat_.Get() : boldTextFormat_.Get();

        const std::wstring& headline = settings.dateFirst ? dateText : timeText;
        const std::wstring& subline = settings.dateFirst ? timeText : dateText;
        IDWriteTextFormat* headlineFmt = settings.dateFirst ? smallFmt : bigFmt;
        IDWriteTextFormat* sublineFmt = settings.dateFirst ? bigFmt : smallFmt;

        if (settings.dateFirst) {
            // Date on top reads as a label, so give the big clock the lower slot.
            const D2D1_RECT_F headRect = D2D1::RectF(rect.left, cy - 46.0f * scale, rect.right, cy - 22.0f * scale);
            textBrush_->SetOpacity(0.92f);
            target_->DrawTextW(headline.c_str(), static_cast<UINT32>(headline.size()), headlineFmt,
                               headRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);

            const D2D1_RECT_F subRect = D2D1::RectF(rect.left, cy - 20.0f * scale, rect.right, cy + 44.0f * scale);
            textBrush_->SetOpacity(0.98f);
            target_->DrawTextW(subline.c_str(), static_cast<UINT32>(subline.size()), sublineFmt,
                               subRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        } else {
            const D2D1_RECT_F headRect = D2D1::RectF(rect.left, cy - 44.0f * scale, rect.right, cy + 20.0f * scale);
            textBrush_->SetOpacity(0.98f);
            target_->DrawTextW(headline.c_str(), static_cast<UINT32>(headline.size()), headlineFmt,
                               headRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);

            const D2D1_RECT_F subRect = D2D1::RectF(rect.left, cy + 22.0f * scale, rect.right, cy + 46.0f * scale);
            mutedBrush_->SetOpacity(0.85f);
            target_->DrawTextW(subline.c_str(), static_cast<UINT32>(subline.size()), sublineFmt,
                               subRect, mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
        textBrush_->SetOpacity(0.98f);

        const bool micActive = state.system.micActive && settings.privacyDots && settings.privacyDotsMic;
        const bool camActive = state.system.cameraActive && settings.privacyDots && settings.privacyDotsCam;
        const bool scrActive = state.system.screenActive && settings.privacyDots && settings.privacyDotsScreen;  // Bunbun

        if (camActive || micActive || scrActive) {
            std::wstring label;
            D2D1_COLOR_F dotColor;

            if (scrActive && (camActive || micActive)) {  // Bunbun: the screen share was missing here
                dotColor = settings.privacyDotsScreenHex;
                label = std::wstring(camActive && micActive ? L"Camera, microphone & screen" : camActive ? L"Camera & screen" : L"Microphone & screen") +
                        L" in use";
            } else if (scrActive) {
                dotColor = settings.privacyDotsScreenHex;
                label = state.system.screenApp.empty() ? std::wstring(L"Screen is being shared")
                                                       : state.system.screenApp + L" is sharing your screen";
            } else if (camActive && micActive) {
                dotColor = settings.privacyDotsCamHex;
                if (!state.system.cameraApp.empty() && !state.system.micApp.empty() && state.system.cameraApp == state.system.micApp) {
                    label = state.system.cameraApp + L" is using camera & microphone";
                } else if (!state.system.cameraApp.empty() && !state.system.micApp.empty()) {
                    label = state.system.cameraApp + L" & " + state.system.micApp + L" using camera & mic";
                } else if (!state.system.cameraApp.empty()) {
                    label = state.system.cameraApp + L" is using camera & microphone";
                } else if (!state.system.micApp.empty()) {
                    label = state.system.micApp + L" is using camera & microphone";
                } else {
                    label = L"Camera & microphone in use";
                }
            } else if (camActive) {
                dotColor = settings.privacyDotsCamHex;
                if (!state.system.cameraApp.empty()) {
                    label = state.system.cameraApp + L" is using your camera";
                } else {
                    label = L"Camera in use";
                }
            } else {
                dotColor = settings.privacyDotsMicHex;
                if (!state.system.micApp.empty()) {
                    label = state.system.micApp + L" is using your microphone";
                } else {
                    label = L"Microphone in use";
                }
            }

            IDWriteTextFormat* fmt = smallTextFormat_ ? smallTextFormat_.Get() : textFormat_.Get();
            if (fmt && dwriteFactory_) {
                ComPtr<IDWriteTextLayout> textLayout;
                HRESULT hr = dwriteFactory_->CreateTextLayout(
                    label.c_str(), static_cast<UINT32>(label.size()),
                    fmt, 500.0f, 30.0f, &textLayout);

                if (SUCCEEDED(hr) && textLayout) {
                    DWRITE_TEXT_METRICS tm = {};
                    textLayout->GetMetrics(&tm);

                    const float pillH = 22.0f * scale;
                    const float pillW = tm.width + 28.0f * scale;
                    const float pillX = cx - pillW * 0.5f;
                    const float pillY = rect.bottom - 18.0f * scale - pillH;

                    ComPtr<ID2D1SolidColorBrush> badgeBg;
                    target_->CreateSolidColorBrush(WithAlpha(material_.raised, material_.raised.a * settingsOpacity_), &badgeBg);
                    if (badgeBg) {
                        target_->FillRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH), pillH * 0.5f, pillH * 0.5f),
                            badgeBg.Get());
                    }

                    ComPtr<ID2D1SolidColorBrush> dotBrush;
                    dotColor.a = settingsOpacity_;
                    target_->CreateSolidColorBrush(dotColor, &dotBrush);
                    if (dotBrush) {
                        const float badgeDotR = 3.5f * scale;
                        target_->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F(pillX + 10.0f * scale, pillY + pillH * 0.5f), badgeDotR, badgeDotR),
                            dotBrush.Get());
                    }

                    mutedBrush_->SetOpacity(0.90f);
                    target_->DrawTextLayout(
                        D2D1::Point2F(pillX + 18.0f * scale, pillY + (pillH - tm.height) * 0.5f),
                        textLayout.Get(), mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
                    mutedBrush_->SetOpacity(0.75f);
                }
            }
        }

        textBrush_->SetOpacity(0.96f);
        mutedBrush_->SetOpacity(0.75f);
    }

    void DrawIdleDashboard(const SharedState& state, D2D1_RECT_F rect, const Settings& settings,
                           double now) {
        if (GameOverlayShown(settings.gameOverlay)) {
            DrawGameOverlay(state, rect, 1.0f);
            return;
        }
        if (!clockFormat_) return;

        // Clip to the island's real silhouette (pill / notch / w11 rounded
        // rect), not its bounding box — a plain rect clip leaves the corners
        // outside the rounded shape unclipped, which is what was showing up
        // as a faint square "border" around the round island while collapsing.
        // Publish the same content-space geometry the media surface does, so the
        // File Tray's row hit test works while the island is idle too.
        PublishContentGeometry(rect);

        const float dashHeight = rect.bottom - rect.top;
        const float dashRadius = ContentIslandRadius(dashHeight);
        // Bunbun: in Studio `rect` is the page inside the frame; the island's big radius
        // there rounded the bottom cards' corners (and cut letters). A plain clip then.
        ComPtr<ID2D1Geometry> dashMask = studio_.on ? nullptr : CreateIslandMaskGeometry(rect, dashRadius, g_settings.notchStyle);
        ID2D1Layer* dashLayer = AcquireLayer();
        const bool haveDashMask = dashMask && dashLayer;
        if (haveDashMask) {
            target_->PushLayer(D2D1::LayerParameters(rect, dashMask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), dashLayer);
        } else {
            target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }

        SYSTEMTIME local = {};
        GetLocalTime(&local);
        const std::wstring collapsedTime = !settings.idleName.empty() ? settings.idleName  // Bunbun: your text
                                         : FormatIslandTime(local, settings.clockFollowSystem,
                                                            settings.use24HourClock,
                                                            settings.showSeconds);
        const wchar_t* timeBuf = collapsedTime.c_str();

        const float scale = 1.0f;
        const float width = rect.right - rect.left;

        bool hasWeather = state.weather.hasData && (now - state.weather.lastUpdated < 3600.0);
        std::wstring wIcon = L"🌡️";
        std::wstring wText = Loc(L"Loading...");
        if (hasWeather) {
            wText = state.weather.weatherDesc;
            GetWeatherIconAndText(state.weather.weatherCode, wIcon, wText);
        }

        // Cross-fade the collapsed status-bar view and the expanded tab view
        // across a small width band around the switchover point instead of
        // a hard cut. Previously this was a plain if/else on width, so the
        // clock's glow (and everything else in the expanded view) just got
        // clipped smaller as the pill shrank and then vanished outright the
        // instant width crossed the threshold — a pop, not a fade. Blending
        // both views by opacity (with an eased curve) makes it a dissolve.
        (void)width;
        auto SmoothFade = [](float t) {
            t = Clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        // Height-driven, like the media surface: collapsed content is gone by
        // ~70px, expanded content starts at ~56px and is solid by ~120px.
        const float collapsedAlpha = SmoothFade((70.0f - dashHeight) / 30.0f);
        const float expandedAlpha = SmoothFade((dashHeight - 56.0f) / 64.0f);

        ID2D1Layer* dashFadeLayer = AcquireLayer();

        if (collapsedAlpha > 0.01f) {
            if (dashFadeLayer) {
                target_->PushLayer(D2D1::LayerParameters(rect, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                          D2D1::IdentityMatrix(), collapsedAlpha, nullptr,
                                                          D2D1_LAYER_OPTIONS_NONE),
                                   dashFadeLayer);
            }

            // Collapsed Mode (Apple Dynamic Island status bar).
            //
            // Slots are placed from the same measurements that sized the pill, so
            // the clock gets exactly the room it needs and the divider sits between
            // the two strings instead of at an arbitrary geometric centre. The old
            // version split the pill 50/50 at centerX and padded both ends by 6px,
            // which produced dead air on short strings.
            const IdleStripMetrics idleMetrics = MeasureIdleStrip(state, settings, now);
            IDWriteTextFormat* idleFmt =
                idleTextFormat_ ? idleTextFormat_.Get() : smallTextFormat_.Get();

            // The dot is anchored to the right edge by DrawPrivacyDots, so reserve
            // its lane rather than shrinking the text box out from under the clock.
            const float privacyReserve =
                idleMetrics.privacyReserve * scale;  // Bunbun: as many slots as dots
            const float innerLeft = rect.left + IdleStripLayout::kPadX * scale;
            const float innerRight =
                rect.right - IdleStripLayout::kPadX * scale - privacyReserve;

            float blockWidth = idleMetrics.clockWidth + idleMetrics.ringWidth * scale;  // Bunbun: + the status ring
            const bool idleDivider = idleMetrics.hasWeather && idleMetrics.clockWidth > 0.0f;  // Bunbun
            if (idleMetrics.hasWeather) {
                blockWidth += (idleDivider ? (IdleStripLayout::kSlotGap * 2.0f +
                                              IdleStripLayout::kDividerWidth) * scale : 0.0f) +
                              idleMetrics.weatherWidth;
            }

            // Centre the block in whatever room the pill currently has. Mid-spring
            // the rect is wider or narrower than the natural width, and clamping
            // the slack at zero stops the slots from inverting when it is narrower.
            const float slack = std::max(0.0f, (innerRight - innerLeft) - blockWidth);
            float slotX = innerLeft + slack * 0.5f;
            // Bunbun: the face in the middle: the time and weather centred on its left.
            if (faceGapOn_) {
                const float leftBlock = blockWidth - idleMetrics.ringWidth * scale;
                slotX = innerLeft + std::max(0.0f, (faceGapL_ - innerLeft) - leftBlock) * 0.5f;
            }

            // Each slot is exactly its measured width, and the measurement used the
            // widest-digit form, so the live string is centred inside a box it is
            // guaranteed to fit rather than drifting as the digits change.
            textBrush_->SetOpacity(0.96f);
            if (idleMetrics.clockWidth > 0.0f) {  // Bunbun: the time can be left out
                const D2D1_RECT_F timeRect =
                    D2D1::RectF(slotX, rect.top, slotX + idleMetrics.clockWidth, rect.bottom);
                target_->DrawTextW(timeBuf, static_cast<UINT32>(wcslen(timeBuf)), idleFmt,
                                   timeRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
            slotX += idleMetrics.clockWidth;
            // Bunbun: the status ring, right after the clock.
            if (idleMetrics.ringWidth > 0.0f) {
                float ringFrac = 0.0f;
                D2D1_COLOR_F ringCol{};
                if (StatusRing(state, settings, now, &ringFrac, &ringCol)) {
                    EnsureRoundStroke();
                    const float d = kStatusRingSize * scale;
                    D2D1_POINT_2F c = D2D1::Point2F(slotX + (idleMetrics.clockWidth > 0.0f ? kStatusRingGap * scale : 0.0f) + d * 0.5f, (rect.top + rect.bottom) * 0.5f);
                    if (faceGapOn_) c.x = (faceGapR_ + innerRight) * 0.5f;  // Bunbun: right of the face
                    bb::studio::Ring(SPen(), c, d * 0.5f - 1.2f * scale, 2.4f * scale, ringFrac, ringCol, settingsOpacity_);
                }
                if (!faceGapOn_) slotX += idleMetrics.ringWidth * scale;
            }

            if (idleMetrics.hasWeather && !idleDivider) {
                wchar_t weatherOnly[32] = {};
                if (hasWeather) swprintf_s(weatherOnly, L"%s %.0f\x00B0", wIcon.c_str(), state.weather.temperature);
                else wcscpy_s(weatherOnly, ARRAYSIZE(weatherOnly), L"🌡️ --\x00B0");
                target_->DrawTextW(weatherOnly, static_cast<UINT32>(wcslen(weatherOnly)), idleFmt,
                                   D2D1::RectF(slotX, rect.top, slotX + idleMetrics.weatherWidth, rect.bottom),
                                   textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            } else if (idleMetrics.hasWeather) {
                slotX += IdleStripLayout::kSlotGap * scale;

                ComPtr<ID2D1SolidColorBrush> divider;
                target_->CreateSolidColorBrush(
                    WithAlpha(material_.hairline, material_.hairline.a * settingsOpacity_),
                    &divider);
                if (divider) {
                    const float divW = IdleStripLayout::kDividerWidth * scale;
                    const float divTop = rect.top + IdleStripLayout::kDividerInsetY * scale;
                    const float divBottom = rect.bottom - IdleStripLayout::kDividerInsetY * scale;
                    target_->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(slotX, divTop, slotX + divW, divBottom),
                                          divW * 0.5f, divW * 0.5f), divider.Get());
                }
                slotX += (IdleStripLayout::kDividerWidth + IdleStripLayout::kSlotGap) * scale;

                wchar_t weatherLabel[32] = {};
                if (hasWeather) swprintf_s(weatherLabel, L"%s %.0f\x00B0", wIcon.c_str(), state.weather.temperature);
                else wcscpy_s(weatherLabel, ARRAYSIZE(weatherLabel), L"\U0001F321\uFE0F --\x00B0");

                const D2D1_RECT_F wRect =
                    D2D1::RectF(slotX, rect.top, slotX + idleMetrics.weatherWidth, rect.bottom);
                target_->DrawTextW(weatherLabel, static_cast<UINT32>(wcslen(weatherLabel)), idleFmt,
                                   wRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
            textBrush_->SetOpacity(1.0f);

            if (dashFadeLayer) {
                target_->PopLayer();
            }
        }

        if (expandedAlpha > 0.01f) {
            if (dashFadeLayer) {
                target_->PushLayer(D2D1::LayerParameters(rect, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                          D2D1::IdentityMatrix(), expandedAlpha, nullptr,
                                                          D2D1_LAYER_OPTIONS_NONE),
                                   dashFadeLayer);
            }

            // Expanded Mode. Order must match ActiveTabCount()/FileTrayTabIndex().
            // Bunbun: one list for both views (DashboardTabIds), plus the Studio pages.
            const std::vector<int> activeTabs = DashboardTabIds(1, settings);
            g_dashboardContext.store(1, std::memory_order_relaxed);

            int maxTabs = static_cast<int>(activeTabs.size());
            int tabIdx = NormalizedTabIndex(settings);
            if (tabIdx >= maxTabs) tabIdx = maxTabs - 1;

            // Lay the expanded view out at its final size, centred horizontally and
            // pinned to the top, so growing the pill only reveals it (same as media)
            // instead of re-centring and rescaling the content every frame.
            const float fullW = std::max(MediaLayout::kExpandedWidth, rect.right - rect.left);
            const float fullH = std::max(MediaLayout::kExpandedHeight, rect.bottom - rect.top);
            const float fullCx = (rect.left + rect.right) * 0.5f;
            const D2D1_RECT_F fullRect = D2D1::RectF(fullCx - fullW * 0.5f, rect.top,
                                                     fullCx + fullW * 0.5f, rect.top + fullH);

            DrawTabTransition(1, tabIdx, activeTabs, fullRect, now, [&](int activeTabId) {
                if (DrawStudioExtraPage(activeTabId, state, fullRect, settings, now)) return;  // Bunbun
                if (activeTabId == 3 && settings.studioLayout) DrawStudioHome(state, fullRect, settings, now);  // Bunbun
                else if (activeTabId == 3) DrawTimeDashboard(state, fullRect, settings, now, scale, local);
                else if (activeTabId == 0) DrawCalendarDashboard(state, fullRect, settings, now, scale, local);
                else if (activeTabId == 1) DrawWeatherDashboard(state, fullRect, settings, now, scale, hasWeather, wIcon, wText);
                else if (activeTabId == 2) DrawHardwareMonitorDashboard(state, fullRect, settings, scale);
                else if (activeTabId == 5) DrawConnectivityDashboard(state, fullRect, scale);  // Bunbun
                else if (activeTabId == 4) DrawFileTrayDashboard(state, fullRect, settings, scale);
                else if (activeTabId == 7) DrawTodoDashboard(state, fullRect, scale);  // Bunbun
            });

            if (!settings.studioLayout) DrawTabPagination(fullRect, maxTabs);  // Bunbun: Studio has a tab bar

            if (dashFadeLayer) {
                target_->PopLayer();
            }
        }

        if (expandedAlpha <= 0.01f && !g_trayDragOver.load(std::memory_order_relaxed) && !offscreen_) {  // Bunbun: not for another display's island
            g_idleTab = 0;
            ResetTabTransition();
        }

        if (haveDashMask) target_->PopLayer(); else target_->PopAxisAlignedClip();
    }

    void DrawGameOverlay(const SharedState& state, D2D1_RECT_F rect, float unused_scale) {
        (void)unused_scale;
        const float scale = 1.0f;
        const bool compact = g_settings.gameOverlayCompact;
        const GameOverlayLayout::Metrics m = GameOverlayLayout::For(compact);

        const float cardTop = rect.top + m.padY;
        const float cardBottom = rect.bottom - m.padY;
        float cursorX = rect.left + m.padX;

        // Bunbun: the mascot on the left, controller in hand, eyes on the game.
        if (g_settings.bunbunMascot && g_settings.bunbunMascotOnIsland) {
            const float side = GameOverlayLayout::MascotW(compact);
            const D2D1_POINT_2F mid = D2D1::Point2F(cursorX + side * 0.5f, (cardTop + cardBottom) * 0.5f);
            D2D1_POINT_2F c{};
            float size = 0.0f;
            bb::mascot::FitToBox(D2D1::RectF(mid.x - side * 0.5f, mid.y - side * 0.5f, mid.x + side * 0.5f, mid.y + side * 0.5f), &c, &size);
            bb::mascot::Draw(target_.Get(), c, size, bb::mascot::Mood::Gaming, NowSeconds(), 0.0, MascotLook(c));
            cursorX += side + m.fpsGap;  // its frames: a 12 fps bucket in the render loop
        }

        // ── FPS, given hero treatment ────────────────────────────────────────
        // Frame rate is the number a player actually watches, so it gets a wider
        // card and a larger figure than the percentages beside it. It has no
        // 0-100 ceiling, so it gets no load bar and keeps the plain accent.
        if (g_settings.gameOverlayShowFps) {
            const D2D1_RECT_F fpsCard = D2D1::RectF(cursorX, cardTop, cursorX + m.fpsW, cardBottom);
            DrawCard(fpsCard, m.radius);

            ComPtr<ID2D1SolidColorBrush> fpsIcon;
            if (SUCCEEDED(target_->CreateSolidColorBrush(
                    WithAlpha(material_.accent, 0.90f * settingsOpacity_), &fpsIcon)) && fpsIcon) {
                DrawMetricGlyph(D2D1::Point2F(fpsCard.left + 15.0f * scale, fpsCard.top + 15.0f * scale),
                                15.0f * scale, 9, fpsIcon.Get());
            }

            const float textLeft = fpsCard.left + 28.0f * scale;
            const float textRight = fpsCard.right - 9.0f * scale;

            if (g_settings.showMetricText && smallTextFormat_) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                mutedBrush_->SetOpacity(0.58f);
                target_->DrawTextW(L"FPS", 3, smallTextFormat_.Get(),
                                   D2D1::RectF(textLeft, fpsCard.top + 2.0f * scale,
                                               textRight, fpsCard.top + 16.0f * scale),
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }

            wchar_t fpsValue[16] = {};
            swprintf_s(fpsValue, L"%d", state.system.renderFps);
            IDWriteTextFormat* fpsFmt = clockFormat_ ? clockFormat_.Get() : textFormat_.Get();
            if (fpsFmt) {
                // clockFormat_ is centre-aligned by default; the strip reads as a
                // left-aligned column, so override for this draw and restore.
                fpsFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                textBrush_->SetOpacity(0.97f);
                // Shares the label's left margin so the glyph sits in its own
                // column and the text column has one straight edge, rather than
                // the value hanging out to the left of the label above it.
                const float top = g_settings.showMetricText ? fpsCard.top + 16.0f * scale : cardTop;
                target_->DrawTextW(fpsValue, static_cast<UINT32>(wcslen(fpsValue)), fpsFmt,
                                   D2D1::RectF(textLeft, top,
                                               textRight, cardBottom - 3.0f * scale),
                                   textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                fpsFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            }

            cursorX = fpsCard.right + m.fpsGap;
        }

        // ── Load cards ───────────────────────────────────────────────────────
        // Laid out from whichever metrics are enabled (#25), so turning some off
        // closes the gap instead of leaving a hole. Icons come from the shared
        // DrawMetricGlyph family, so CPU here is the same symbol as CPU on the
        // hardware dashboard.
        struct GameCard {
            const wchar_t* label;
            int percent;
            int glyph;
            bool enabled;
        };
        const GameCard cards[] = {
            {L"CPU", state.system.cpuPercent,              3, g_settings.gameOverlayShowCpu},
            {L"RAM", state.system.memoryPercent,           4, g_settings.gameOverlayShowRam},
            {L"GPU", state.system.gpuPercent,              5, g_settings.gameOverlayShowGpu},
            {L"DISK", 100 - state.system.diskFreePercent,  8, g_settings.gameOverlayShowDisk},
        };

        for (const GameCard& card : cards) {
            if (!card.enabled) {
                continue;
            }
            if (cursorX + m.cardW > rect.right - m.padX + 0.5f) {
                break;  // ran out of room
            }
            DrawGameMetricCard(D2D1::RectF(cursorX, cardTop, cursorX + m.cardW, cardBottom),
                               card.label, card.percent, card.glyph, m.radius);
            cursorX += m.cardW + m.gap;
        }

        textBrush_->SetOpacity(0.90f);
        mutedBrush_->SetOpacity(0.58f);
    }

    void DrawGameMetricCard(D2D1_RECT_F rect, const wchar_t* label, int percent, int glyph, float radius) {
        const float scale = 1.0f;

        // Same semantic load colour the hardware dashboard uses: accent while a
        // component is comfortable, amber under pressure, red when saturated.
        // This replaces a per-metric rainbow (cyan CPU, magenta RAM, green GPU,
        // orange disk) that encoded nothing and fought the album-art accent.
        const bool known = percent >= 0;
        const float pct = known ? Clamp(percent / 100.0f, 0.0f, 1.0f) : 0.0f;
        const D2D1_COLOR_F tint = known ? LoadStateColor(pct) : material_.accent;

        // Fill only. This card used to carry a hairline border *and* a
        // metric-coloured ring on top of it -- two bright outlines per card,
        // four cards across, which is the edge lighting this design drops.
        DrawCard(rect, radius);

        ComPtr<ID2D1SolidColorBrush> glyphBrush;
        if (SUCCEEDED(target_->CreateSolidColorBrush(
                WithAlpha(tint, 0.85f * settingsOpacity_), &glyphBrush)) && glyphBrush) {
            DrawMetricGlyph(D2D1::Point2F(rect.left + 15.0f * scale, rect.top + 15.0f * scale),
                            14.0f * scale, glyph, glyphBrush.Get());
        }

        const float textLeft = rect.left + 28.0f * scale;
        const float textRight = rect.right - 8.0f * scale;

        // Label above value above bar, matching the hardware dashboard exactly.
        if (g_settings.showMetricText && smallTextFormat_) {
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            mutedBrush_->SetOpacity(0.58f);
            target_->DrawTextW(label, static_cast<UINT32>(wcslen(label)), smallTextFormat_.Get(),
                               D2D1::RectF(textLeft, rect.top + 2.0f * scale,
                                           textRight, rect.top + 16.0f * scale),
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        wchar_t value[16] = {};
        if (known) {
            swprintf_s(value, L"%d%%", percent);
        } else {
            wcscpy_s(value, ARRAYSIZE(value), L"--");
        }

        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            textBrush_->SetOpacity(0.95f);
            // Aligned with the label above it, so the glyph owns the left column
            // and label/value share one straight edge.
            const float valueTop = g_settings.showMetricText ? rect.top + 17.0f * scale
                                                             : rect.top + 4.0f * scale;
            target_->DrawTextW(value, static_cast<UINT32>(wcslen(value)), textFormat_.Get(),
                               D2D1::RectF(textLeft, valueTop,
                                           textRight, rect.bottom - 8.0f * scale),
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        // The bar spans the card rather than just the text column: at this size it
        // reads as the card's own meter, and a 38px stub under the value did not.
        const D2D1_RECT_F track = D2D1::RectF(rect.left + 13.0f * scale, rect.bottom - 7.0f * scale,
                                              rect.right - 13.0f * scale, rect.bottom - 4.5f * scale);
        ComPtr<ID2D1SolidColorBrush> trackBrush;
        if (SUCCEEDED(target_->CreateSolidColorBrush(material_.raisedStrong, &trackBrush)) && trackBrush) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(track, 1.25f * scale, 1.25f * scale), trackBrush.Get());
        }
        const float span = (track.right - track.left) * pct;
        if (span > 0.5f) {
            ComPtr<ID2D1SolidColorBrush> fillBrush;
            if (SUCCEEDED(target_->CreateSolidColorBrush(WithAlpha(tint, 0.95f), &fillBrush)) && fillBrush) {
                target_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(track.left, track.top, track.left + span, track.bottom),
                                      1.25f * scale, 1.25f * scale),
                    fillBrush.Get());
            }
        }

        textBrush_->SetOpacity(0.90f);
        mutedBrush_->SetOpacity(0.58f);
    }

