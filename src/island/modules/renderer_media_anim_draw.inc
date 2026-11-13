// Bunbun: Renderer: source switch and dock, skip / play morphs, title, progress and album-art animations.
// Split out of island_core.cpp; included inside class Renderer at the
// original position, so declaration order is unchanged. Not a standalone file.

    // ── Source switch animation + source dock ────────────────────────────────
    enum { kSwapNone = 0, kSwapOut, kSwapHold, kSwapIn };

    // Out: old content slides away and fades. Hold: nothing drawn while the media
    // thread switches sessions. In: new content slides in. The request is only sent
    // once Out has finished, so old and new content are never visible together.
    void UpdateSourceSwap(const SharedState& state, double now, float* alpha, float* dx) {
        *alpha = 1.0f;
        *dx = 0.0f;

        // Media tab was off screen for a while: don't resume mid-animation.
        if (swapPhase_ != kSwapNone && swapLastUpdate_ >= 0.0 && now - swapLastUpdate_ > 0.5) {
            if (swapPhase_ == kSwapOut) RequestMediaSource(swapTarget_);
            swapPhase_ = kSwapNone;
        }
        swapLastUpdate_ = now;

        if (!expandedAnim_ && swapPhase_ != kSwapNone) {  // animation switched off mid-swap
            if (swapPhase_ == kSwapOut) RequestMediaSource(swapTarget_);
            swapPhase_ = kSwapNone;
        }

        // New request. One that arrives during slide-in waits for it to finish.
        const unsigned seq = g_switchSeq.load();
        if (seq != swapSeenSeq_ && swapPhase_ != kSwapIn) {
            swapSeenSeq_ = seq;
            {
                std::lock_guard lock(g_mediaSourceMutex);
                swapTarget_ = g_switchTarget;
            }
            swapDir_ = g_switchDir.load() >= 0 ? 1.0f : -1.0f;
            if (!expandedAnim_) {
                RequestMediaSource(swapTarget_);
            } else if (swapPhase_ == kSwapNone) {
                swapPhase_ = kSwapOut;
                swapStart_ = now;
            } else if (swapPhase_ == kSwapHold) {
                RequestMediaSource(swapTarget_);  // retarget while nothing is visible
                swapStart_ = now;
            }                                     // kSwapOut: just retargeted
        }

        switch (swapPhase_) {
            case kSwapOut: {
                const float t = Clamp(static_cast<float>((now - swapStart_) / MediaLayout::kSwitchOutSec), 0.0f, 1.0f);
                const float e = t * t;  // ease-in
                *alpha = 1.0f - e;
                *dx = -swapDir_ * MediaLayout::kSwitchShift * e;  // opposite to the switch direction
                if (t >= 1.0f) {
                    RequestMediaSource(swapTarget_);
                    swapPhase_ = kSwapHold;
                    swapStart_ = now;
                    *alpha = 0.0f;
                }
                break;
            }
            case kSwapHold:
                *alpha = 0.0f;
                *dx = -swapDir_ * MediaLayout::kSwitchShift;
                if (state.media.sourceAppUserModelId == swapTarget_ ||
                    now - swapStart_ > MediaLayout::kSwitchHoldTimeoutSec) {
                    CancelMediaChangeAnims();
                    swapPhase_ = kSwapIn;
                    swapStart_ = now;
                }
                break;
            case kSwapIn: {
                const float t = Clamp(static_cast<float>((now - swapStart_) / MediaLayout::kSwitchInSec), 0.0f, 1.0f);
                const float inv = 1.0f - t;
                const float e = 1.0f - inv * inv * inv;  // ease-out cubic
                *alpha = e;
                *dx = swapDir_ * MediaLayout::kSwitchShift * (1.0f - e);
                if (t >= 1.0f) {
                    swapPhase_ = kSwapNone;
                    *alpha = 1.0f;
                    *dx = 0.0f;
                }
                break;
            }
            default:
                break;
        }
        if (swapPhase_ != kSwapNone) g_layoutDirty = true;
    }

    // The title smoke, art flip, artist/album swap and spectrum ripple all key off "was
    // this drawn a moment ago". Forget that so the new source simply appears.
    void CancelMediaChangeAnims() {
        titleAnimActive_ = false;
        titleOld_.clear();
        titleNew_.clear();
        titleLastDrawn_ = -1.0;
        artFlipStart_ = -1.0;
        artFlipOld_.Reset();
        artFlipLastDrawn_ = -1.0;
        swapArtist_.start = -1.0;
        swapArtist_.lastDrawn = -1.0;
        swapAlbum_.start = -1.0;
        swapAlbum_.lastDrawn = -1.0;
        specStart_ = -1.0;
        specLastDrawn_ = -1.0;
    }

    // Fades (and optionally shifts) everything drawn until EndSwapGroup.
    bool BeginSwapGroup(float alpha, float dx) {
        if (alpha >= 0.999f && std::fabs(dx) < 0.01f) return false;
        if (!swapGroupLayer_ &&
            (FAILED(target_->CreateLayer(&swapGroupLayer_)) || !swapGroupLayer_)) return false;
        target_->GetTransform(&swapGroupOldTransform_);
        target_->SetTransform(D2D1::Matrix3x2F::Translation(dx, 0.0f) * swapGroupOldTransform_);
        target_->PushLayer(
            D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                  D2D1::IdentityMatrix(), Clamp(alpha, 0.0f, 1.0f), nullptr,
                                  D2D1_LAYER_OPTIONS_NONE),
            swapGroupLayer_.Get());
        return true;
    }

    void EndSwapGroup(bool began) {
        if (!began) return;
        target_->PopLayer();
        target_->SetTransform(swapGroupOldTransform_);
        swapGroupLayer_.Reset();
    }

    // "Playing from Brave" / "Paused in Spotify", 11px, tertiary text.
    void DrawSourceLine(const MediaSnapshot& media, D2D1_RECT_F area, double now) {
        if (media.sourceName.empty() || !smallTextFormat_) return;
        const std::wstring text =
            FormatWithName(Loc(media.playing ? L"Playing from %s" : L"Paused in %s"), media.sourceName);
        if (!scratchColorBrush_) {
            target_->CreateSolidColorBrush(material_.textTertiary, &scratchColorBrush_);
        } else {
            scratchColorBrush_->SetColor(material_.textTertiary);
        }
        if (!scratchColorBrush_) return;
        DrawMarqueeText(text, area, smallTextFormat_.Get(), scratchColorBrush_.Get(), now, 26.0f,
                        marqueeSourceCache_);
    }

    // One rounded pill, one circular slot per source. Shown only with 2+ sources.
    void DrawSourceDock(const SharedState& state, D2D1_RECT_F rect, double now) {
        const int sourceCount = static_cast<int>(state.mediaSources.size());
        if (sourceCount < 2) {
            dockInit_ = false;
            return;
        }

        const int liveIdx = ActiveMediaSourceIndex(state);
        const DockSlots slots = ComputeDockSlots(sourceCount, liveIdx);  // same inputs as the hit test
        if (slots.count < 2) return;

        // During a switch the ring already points at the source being switched to.
        int shownIdx = liveIdx;
        if (swapPhase_ != kSwapNone && !swapTarget_.empty()) {
            for (int i = 0; i < sourceCount; ++i) {
                if (state.mediaSources[i].aumid == swapTarget_) {
                    shownIdx = i;
                    break;
                }
            }
        }
        auto slotOf = [&](int sourceIdx) {
            for (int s = 0; s < slots.count; ++s) {
                if (slots.sourceIndex[s] == sourceIdx) return s;
            }
            return -1;
        };
        int targetSlot = slotOf(shownIdx);
        if (targetSlot < 0) targetSlot = slotOf(liveIdx);
        if (targetSlot < 0) targetSlot = 0;

        // ── Animation state ──────────────────────────────────────────────────
        const bool fresh = dockInit_ && dockLastDrawn_ >= 0.0 && (now - dockLastDrawn_) < 0.25 &&
                           dockSlotCount_ == slots.count;
        const float dt = fresh ? Clamp(static_cast<float>(now - dockLastDrawn_), 0.001f, 0.05f) : 0.016f;
        dockLastDrawn_ = now;

        if (!fresh) {
            dockInit_ = true;
            dockSlotCount_ = slots.count;
            dockIndPos_ = dockIndFrom_ = dockIndTarget_ = static_cast<float>(targetSlot);
            dockIndStart_ = -1.0;
            for (int i = 0; i < MediaLayout::kDockMaxSlots; ++i) {
                dockHover_[i] = 0.0f;
                dockPress_[i] = 0.0f;
            }
        } else if (static_cast<float>(targetSlot) != dockIndTarget_) {
            dockIndFrom_ = dockIndPos_;
            dockIndTarget_ = static_cast<float>(targetSlot);
            dockIndStart_ = now;
        }
        if (dockIndStart_ >= 0.0) {
            const float p = Clamp(static_cast<float>((now - dockIndStart_) / MediaLayout::kDockSlideSec), 0.0f, 1.0f);
            dockIndPos_ = dockIndFrom_ + (dockIndTarget_ - dockIndFrom_) * SkipEaseInOut(p);
            if (p >= 1.0f) {
                dockIndPos_ = dockIndTarget_;
                dockIndStart_ = -1.0;
            }
            g_layoutDirty = true;
        }

        const int hovered = g_hoveredSourceSlot.load(std::memory_order_relaxed);
        const int pressed = g_pressedSourceSlot.load(std::memory_order_relaxed);
        bool moving = false;
        for (int i = 0; i < slots.count; ++i) {
            const float hoverTarget = (i == hovered) ? 1.0f : 0.0f;
            dockHover_[i] += (hoverTarget - dockHover_[i]) * (1.0f - std::exp(-dt / 0.09f));
            if (std::fabs(hoverTarget - dockHover_[i]) < 0.004f) dockHover_[i] = hoverTarget; else moving = true;

            const float pressTarget = (i == pressed) ? 1.0f : 0.0f;
            const float tau = pressTarget > dockPress_[i] ? 0.025f : 0.075f;  // same feel as mediaBtnPress_
            dockPress_[i] += (pressTarget - dockPress_[i]) * (1.0f - std::exp(-dt / tau));
            if (std::fabs(pressTarget - dockPress_[i]) < 0.004f) dockPress_[i] = pressTarget; else moving = true;
        }
        if (moving) g_layoutDirty = true;

        // ── Drawing ──────────────────────────────────────────────────────────
        const float cy = rect.top + MediaLayout::kControlsY;
        const float x0 = rect.left + MediaLayout::kDockLeft;
        const D2D1_RECT_F pill = D2D1::RectF(x0, cy - MediaLayout::kDockHeight * 0.5f,
                                             x0 + MediaLayout::DockWidth(slots.count),
                                             cy + MediaLayout::kDockHeight * 0.5f);
        DrawCard(pill, MediaLayout::kDockHeight * 0.5f);

        // The pill's real colour (raised over the island base), used for the dot's ring.
        const D2D1_COLOR_F pillBg = MixColor(WithAlpha(material_.base, 1.0f),
                                             WithAlpha(material_.raised, 1.0f), material_.raised.a);

        // Active indicator: slides between slots.
        const float stride = MediaLayout::kDockSlot + MediaLayout::kDockGap;
        const float indCx = rect.left + MediaLayout::DockSlotLeft(0) + dockIndPos_ * stride +
                            MediaLayout::kDockSlot * 0.5f;
        const float indR = MediaLayout::kDockSlot * 0.5f;
        ComPtr<ID2D1SolidColorBrush> indFill;
        if (SUCCEEDED(target_->CreateSolidColorBrush(material_.raisedStrong, &indFill)) && indFill) {
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(indCx, cy), indR, indR), indFill.Get());
        }
        accentBrush_->SetOpacity(1.0f);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(indCx, cy), indR - 0.75f, indR - 0.75f),
                             accentBrush_.Get(), 1.5f);

        for (int i = 0; i < slots.count; ++i) {
            const D2D1_POINT_2F c = D2D1::Point2F(
                rect.left + MediaLayout::DockSlotLeft(i) + MediaLayout::kDockSlot * 0.5f, cy);
            const float active = Clamp(1.0f - std::fabs(dockIndPos_ - static_cast<float>(i)), 0.0f, 1.0f);
            const float lit = std::max(active, dockHover_[i]);
            const float opacity = 0.5f + 0.5f * lit;  // 50% idle, 100% active / hovered
            const float s = 1.0f - 0.10f * Clamp(dockPress_[i], 0.0f, 1.0f);
            const D2D1_RECT_F slotRect = D2D1::RectF(c.x - 13.0f, c.y - 13.0f, c.x + 13.0f, c.y + 13.0f);

            D2D1_MATRIX_3X2_F oldTransform;
            target_->GetTransform(&oldTransform);
            target_->SetTransform(D2D1::Matrix3x2F::Scale(s, s, c) * oldTransform);

            const int srcIdx = slots.sourceIndex[i];
            if (srcIdx < 0) {
                wchar_t more[16] = {};
                swprintf_s(more, L"+%d", slots.overflow);
                if (boldTextFormat_) {
                    mutedBrush_->SetOpacity(0.55f + 0.45f * lit);
                    target_->DrawTextW(more, static_cast<UINT32>(wcslen(more)), boldTextFormat_.Get(),
                                       slotRect, mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    mutedBrush_->SetOpacity(0.75f);
                }
            } else {
                const MediaSourceInfo& src = state.mediaSources[srcIdx];
                if (src.icon) {
                    DrawCircularBitmapPixels(*src.icon, c, MediaLayout::kDockIcon * 0.5f,
                                             dockIconBitmap_[i], dockIconGen_[i], opacity);
                } else if (boldTextFormat_ && !src.badge.empty()) {
                    textBrush_->SetOpacity(opacity);
                    target_->DrawTextW(src.badge.c_str(), static_cast<UINT32>(src.badge.size()),
                                       boldTextFormat_.Get(), slotRect, textBrush_.Get(),
                                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    textBrush_->SetOpacity(0.96f);
                }

                if (src.playing) {  // 7px green dot, bottom-right, 1.5px ring in the pill colour
                    const D2D1_POINT_2F d = D2D1::Point2F(c.x + 9.5f, c.y + 9.5f);
                    ComPtr<ID2D1SolidColorBrush> ring, dot;
                    if (SUCCEEDED(target_->CreateSolidColorBrush(pillBg, &ring)) && ring) {
                        target_->FillEllipse(D2D1::Ellipse(d, 5.0f, 5.0f), ring.Get());
                    }
                    if (SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.19f, 0.83f, 0.38f, 1.0f), &dot)) && dot) {
                        target_->FillEllipse(D2D1::Ellipse(d, 3.5f, 3.5f), dot.Get());
                    }
                }
            }
            target_->SetTransform(oldTransform);
        }
    }

    void UpdateMediaButtonAnimations(double now, bool playing) {
        float dt = 0.016f;
        if (lastMediaBtnTime_ > 0.0) {
            dt = static_cast<float>(std::max(0.001, std::min(now - lastMediaBtnTime_, 0.05)));
        }
        lastMediaBtnTime_ = now;

        const int pressedCmd = g_pressedMediaButton.load();
        bool isStillAnimating = false;

        for (int i = 0; i < 3; ++i) {
            const float target = (pressedCmd == i) ? 1.0f : 0.0f;
            const float tau = (target > mediaBtnPress_[i]) ? 0.025f : 0.075f;
            const float k = 1.0f - std::exp(-dt / tau);
            mediaBtnPress_[i] += (target - mediaBtnPress_[i]) * k;

            if (std::abs(mediaBtnPress_[i] - target) < 0.002f) {
                mediaBtnPress_[i] = target;
            } else {
                isStillAnimating = true;
            }
        }

        // Play/pause morph. Animates only if the button was on screen a moment
        // ago; otherwise (island was collapsed, first draw) it snaps to the state.
        {
            constexpr double kMorphDuration = 0.34;
            const bool recentlyDrawn = playMorphLastDrawn_ >= 0.0 && (now - playMorphLastDrawn_) < 0.25;
            playMorphLastDrawn_ = now;
            const float target = playing ? 0.0f : 1.0f;
            if (!playMorphInit_ || !recentlyDrawn || !expandedAnim_) {
                playMorphInit_ = true;
                playMorph_ = playMorphFrom_ = playMorphTarget_ = target;
                playMorphStart_ = -1.0;
            } else if (target != playMorphTarget_) {
                playMorphFrom_ = playMorph_;
                playMorphTarget_ = target;
                playMorphStart_ = now;
            }
            if (playMorphStart_ >= 0.0) {
                const float p = static_cast<float>((now - playMorphStart_) / kMorphDuration);
                if (p >= 1.0f) {
                    playMorph_ = playMorphTarget_;
                    playMorphStart_ = -1.0;
                } else {
                    playMorph_ = playMorphFrom_ + (playMorphTarget_ - playMorphFrom_) * SkipEaseInOut(p);
                    isStillAnimating = true;
                }
            }
        }

        // Depth-swap: start on a new click, then advance with wall-clock time.
        constexpr double kSkipDuration = 0.46;
        const unsigned trig[2] = {g_skipTriggerPrev.load(), g_skipTriggerNext.load()};
        for (int i = 0; i < 2; ++i) {
            if (trig[i] != skipSeenTrigger_[i]) {
                skipSeenTrigger_[i] = trig[i];
                lastSkipClickAt_ = now;
                lastSkipDir_ = (i == 0) ? -1.0f : 1.0f;
                // Ignore a re-click while the swap is still in its first half so
                // the motion never snaps back; the command is sent regardless.
                if (expandedAnim_ && (skipAnimStart_[i] < 0.0 || (now - skipAnimStart_[i]) / kSkipDuration >= 0.5)) {
                    skipAnimStart_[i] = now;
                }
            }
            if (skipAnimStart_[i] >= 0.0) {
                const double p = (now - skipAnimStart_[i]) / kSkipDuration;
                if (p >= 1.0) {
                    skipAnimStart_[i] = -1.0;
                    skipProgress_[i] = -1.0f;
                } else {
                    skipProgress_[i] = static_cast<float>(p);
                    isStillAnimating = true;
                }
            } else {
                skipProgress_[i] = -1.0f;
            }
        }

        if (isStillAnimating) {
            g_layoutDirty = true;
        }
    }

    // ── Skip icon: two rounded triangles + "depth swap" animation ────────────
    static constexpr float kSkipTriW = 0.626f;   // triangle width / height
    static constexpr float kSkipPitch = 0.451f;  // front-left minus back-left, / height

    HRESULT BuildRoundedTriangle(float x0, float w, float H, ComPtr<ID2D1PathGeometry>& out) {
        const D2D1_POINT_2F v[3] = {
            D2D1::Point2F(x0, -0.5f * H), D2D1::Point2F(x0, 0.5f * H), D2D1::Point2F(x0 + w, 0.0f)};
        const float d[3] = {0.11f * H, 0.11f * H, 0.14f * H};
        D2D1_POINT_2F a[3], b[3];
        for (int i = 0; i < 3; ++i) {
            const D2D1_POINT_2F p = v[(i + 2) % 3], n = v[(i + 1) % 3];
            float px = p.x - v[i].x, py = p.y - v[i].y;
            float nx = n.x - v[i].x, ny = n.y - v[i].y;
            const float pl = std::sqrt(px * px + py * py), nl = std::sqrt(nx * nx + ny * ny);
            a[i] = D2D1::Point2F(v[i].x + px / pl * d[i], v[i].y + py / pl * d[i]);
            b[i] = D2D1::Point2F(v[i].x + nx / nl * d[i], v[i].y + ny / nl * d[i]);
        }
        ComPtr<ID2D1PathGeometry> geom;
        HRESULT hr = d2dFactory_->CreatePathGeometry(&geom);
        if (FAILED(hr) || !geom) return FAILED(hr) ? hr : E_FAIL;
        ComPtr<ID2D1GeometrySink> sink;
        hr = geom->Open(&sink);
        if (FAILED(hr) || !sink) return FAILED(hr) ? hr : E_FAIL;
        sink->BeginFigure(b[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(a[1]);
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(v[1], b[1]));
        sink->AddLine(a[2]);
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(v[2], b[2]));
        sink->AddLine(a[0]);
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(v[0], b[0]));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        hr = sink->Close();
        if (FAILED(hr)) return hr;
        out = geom;
        return S_OK;
    }

    bool EnsureSkipGeometry() {
        if (skipTriFull_ && skipTriNotch_) return true;
        if (!d2dFactory_) return false;
        const float H = MediaLayout::kNavButtonRadius * 0.78f;
        const float w = kSkipTriW * H, pitch = kSkipPitch * H;

        ComPtr<ID2D1PathGeometry> full, back, front;
        if (FAILED(BuildRoundedTriangle(0.0f, w, H, full))) return false;
        if (FAILED(BuildRoundedTriangle(0.0f, w, H, back))) return false;
        if (FAILED(BuildRoundedTriangle(pitch, w, H, front))) return false;

        // The back triangle is cut by a slightly enlarged copy of the front one,
        // leaving a clean gap that works on translucent backgrounds too.
        ComPtr<ID2D1TransformedGeometry> grown;
        if (FAILED(d2dFactory_->CreateTransformedGeometry(
                front.Get(), D2D1::Matrix3x2F::Scale(1.16f, 1.16f, D2D1::Point2F(pitch + w / 3.0f, 0.0f)),
                &grown)) || !grown) return false;

        ComPtr<ID2D1PathGeometry> notch;
        if (FAILED(d2dFactory_->CreatePathGeometry(&notch)) || !notch) return false;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(notch->Open(&sink)) || !sink) return false;
        if (FAILED(back->CombineWithGeometry(grown.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, 0.05f, sink.Get()))) {
            return false;
        }
        if (FAILED(sink->Close())) return false;

        skipTriFull_ = full;
        skipTriNotch_ = notch;
        return true;
    }

    static float SkipSmooth(float x) {            // smoothstep 0..1
        x = Clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }
    static float SkipEaseInOut(float x) {         // easeInOutCubic
        x = Clamp(x, 0.0f, 1.0f);
        return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f;
    }

    // forward = true -> "next" (>>), false -> "previous" (<<, mirrored).
    // p < 0 = resting state, otherwise animation progress 0..1.
    void DrawSkipIcon(D2D1_POINT_2F center, bool forward, float p, float opacity, float press) {
        if (!EnsureSkipGeometry()) return;

        const float H = MediaLayout::kNavButtonRadius * 0.78f;
        const float w = kSkipTriW * H, pitch = kSkipPitch * H;
        const float pairLeft = -0.5f * (pitch + w);

        D2D1_MATRIX_3X2_F oldTransform;
        target_->GetTransform(&oldTransform);
        const float sf = 1.0f - 0.13f * press;
        const D2D1_MATRIX_3X2_F base =
            D2D1::Matrix3x2F::Scale(forward ? 1.0f : -1.0f, 1.0f) *
            D2D1::Matrix3x2F::Translation(center.x, center.y) *
            D2D1::Matrix3x2F::Scale(sf, sf, center) * oldTransform;

        auto fill = [&](ID2D1Geometry* g, float x, float scale, float alpha) {
            if (alpha <= 0.003f) return;
            const D2D1_MATRIX_3X2_F m =
                D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(w / 3.0f, 0.0f)) *
                D2D1::Matrix3x2F::Translation(x, 0.0f) * base;
            target_->SetTransform(m);
            accentBrush_->SetOpacity(Clamp(opacity * alpha, 0.0f, 1.0f));
            target_->FillGeometry(g, accentBrush_.Get());
        };

        if (p < 0.0f) {
            fill(skipTriNotch_.Get(), pairLeft, 1.0f, 1.0f);
            fill(skipTriFull_.Get(), pairLeft + pitch, 1.0f, 1.0f);
        } else {
            const float e = SkipEaseInOut(p);

            // B (front): pushed forward, recedes and fades out.
            const float bFade = 1.0f - SkipSmooth(p / 0.75f);
            fill(skipTriFull_.Get(), pairLeft + pitch + pitch * 1.15f * e, 1.0f - 0.2f * e, bFade);

            // A (back): glides into B's slot; its notch dissolves as it becomes the front.
            const float s = SkipSmooth((p - 0.1f) / 0.8f);
            fill(skipTriNotch_.Get(), pairLeft + pitch * e, 1.0f, 1.0f - s);
            fill(skipTriFull_.Get(), pairLeft + pitch * e, 1.0f, s);

            // A' (new back): grows in behind, where A used to be.
            const float cIn = SkipSmooth((p - 0.3f) / 0.7f);
            fill(skipTriNotch_.Get(), pairLeft - pitch * 0.45f * (1.0f - cIn), 0.7f + 0.3f * cIn, cIn);
        }

        target_->SetTransform(oldTransform);
        accentBrush_->SetOpacity(1.0f);
    }

    void DrawMediaControls(bool playing, D2D1_POINT_2F prev, D2D1_POINT_2F play, D2D1_POINT_2F next, double now) {
        UpdateMediaButtonAnimations(now, playing);
        DrawMediaButton(prev, MediaLayout::kNavButtonRadius, 0, false);
        DrawMediaButton(play, MediaLayout::kPlayButtonRadius, playing ? 1 : 2, true);
        DrawMediaButton(next, MediaLayout::kNavButtonRadius, 3, false);
    }

    void DrawMediaButton(D2D1_POINT_2F center, float radius, int kind, bool primary) {
        int buttonCmd = (kind == 0) ? 0 : ((kind == 1 || kind == 2) ? 1 : 2);
        bool isHovered = (g_hoveredMediaButton.load() == buttonCmd);
        const float press = (buttonCmd >= 0 && buttonCmd < 3) ? mediaBtnPress_[buttonCmd] : 0.0f;

        const float r = radius * (1.0f - 0.13f * press);

        const D2D1_COLOR_F restingBg = D2D1::ColorF(
            1.0f, 1.0f, 1.0f,
            primary ? (isHovered ? 0.16f : 0.080f) : (isHovered ? 0.09f : 0.040f)
        );
        const D2D1_COLOR_F pressedBg = D2D1::ColorF(
            currentAccent_.r, currentAccent_.g, currentAccent_.b,
            primary ? 0.28f : 0.18f
        );

        const D2D1_COLOR_F currentBg = D2D1::ColorF(
            restingBg.r + (pressedBg.r - restingBg.r) * press,
            restingBg.g + (pressedBg.g - restingBg.g) * press,
            restingBg.b + (pressedBg.b - restingBg.b) * press,
            restingBg.a + (pressedBg.a - restingBg.a) * press
        );

        if (ID2D1SolidColorBrush* bg = AcquireBrush(currentBg)) {
            target_->FillEllipse(D2D1::Ellipse(center, r, r), bg);
        }

        const float baseOpacity = primary ? (isHovered ? 1.0f : 0.88f) : (isHovered ? 0.92f : 0.62f);
        const float iconOpacity = Clamp(baseOpacity + (1.0f - baseOpacity) * press, 0.0f, 1.0f);
        accentBrush_->SetOpacity(iconOpacity);

        if (kind == 0 || kind == 3) {
            DrawSkipIcon(center, kind == 3, skipProgress_[kind == 3 ? 1 : 0], iconOpacity, press);
            accentBrush_->SetOpacity(1.0f);
            return;
        }

        if (kind == 1 || kind == 2) {
            DrawPlayPauseIcon(center, iconOpacity, press);
            accentBrush_->SetOpacity(1.0f);
            return;
        }

        const wchar_t* glyph = nullptr;
        IDWriteTextFormat* fmt = nullptr;
        if (usingFluentIcons_) {
            if (kind == 0) {
                glyph = L"\uF8AC";
                fmt = mediaNavIconFormat_.Get();
            } else if (kind == 1) {
                glyph = L"\uF8AE";
                fmt = mediaPlayIconFormat_.Get();
            } else if (kind == 2) {
                glyph = L"\uF5B0";
                fmt = mediaPlayIconFormat_.Get();
            } else if (kind == 3) {
                glyph = L"\uF8AD";
                fmt = mediaNavIconFormat_.Get();
            }
        } else {
            if (kind == 0) {
                glyph = L"\uE100";
                fmt = mediaNavIconFormat_.Get();
            } else if (kind == 1) {
                glyph = L"\uE103";
                fmt = mediaPlayIconFormat_.Get();
            } else if (kind == 2) {
                glyph = L"\uE102";
                fmt = mediaPlayIconFormat_.Get();
            } else if (kind == 3) {
                glyph = L"\uE101";
                fmt = mediaNavIconFormat_.Get();
            }
        }

        if (glyph && fmt) {
            const float offsetX = (kind == 2) ? 1.0f : 0.0f;
            D2D1_RECT_F glyphRect = D2D1::RectF(
                center.x - radius + offsetX,
                center.y - radius,
                center.x + radius + offsetX,
                center.y + radius
            );

            if (press > 0.001f) {
                D2D1_MATRIX_3X2_F oldTransform;
                target_->GetTransform(&oldTransform);
                const float scaleFactor = 1.0f - 0.13f * press;
                target_->SetTransform(D2D1::Matrix3x2F::Scale(scaleFactor, scaleFactor, center) * oldTransform);
                target_->DrawTextW(glyph, 1, fmt, glyphRect, accentBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
                target_->SetTransform(oldTransform);
            } else {
                target_->DrawTextW(glyph, 1, fmt, glyphRect, accentBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        }

        accentBrush_->SetOpacity(1.0f);
    }

    // ── Play/pause morph ─────────────────────────────────────────────────────
    // Both icons are two quads with matching vertices: the pause bars become the
    // two halves of the play triangle (the right half collapses into the tip).
    static constexpr float kPlayPauseCorner = 1.5f;

    struct PlayPauseShapes {
        D2D1_POINT_2F pause[8];
        D2D1_POINT_2F play[8];
    };

    static const PlayPauseShapes& GetPlayPauseShapes() {
        static const PlayPauseShapes shapes = [] {
            PlayPauseShapes s = {};
            const float rr = kPlayPauseCorner;
            const float bx0 = 2.1f, bx1 = 6.5f, by = 7.5f;
            // Left bar, then right bar: TL, TR, BR, BL.
            s.pause[0] = D2D1::Point2F(-bx1 + rr, -by + rr);
            s.pause[1] = D2D1::Point2F(-bx0 - rr, -by + rr);
            s.pause[2] = D2D1::Point2F(-bx0 - rr,  by - rr);
            s.pause[3] = D2D1::Point2F(-bx1 + rr,  by - rr);
            s.pause[4] = D2D1::Point2F( bx0 + rr, -by + rr);
            s.pause[5] = D2D1::Point2F( bx1 - rr, -by + rr);
            s.pause[6] = D2D1::Point2F( bx1 - rr,  by - rr);
            s.pause[7] = D2D1::Point2F( bx0 + rr,  by - rr);

            // Triangle (optically centred), split by a shared edge T-U.
            const D2D1_POINT_2F A = D2D1::Point2F(-5.5f, -7.5f);
            const D2D1_POINT_2F B = D2D1::Point2F(-5.5f,  7.5f);
            const D2D1_POINT_2F C = D2D1::Point2F( 7.5f,  0.0f);
            const D2D1_POINT_2F T = D2D1::Point2F( 1.0f, -3.75f);
            const D2D1_POINT_2F U = D2D1::Point2F( 1.0f,  3.75f);
            const float la = std::hypot(B.x - C.x, B.y - C.y);
            const float lb = std::hypot(C.x - A.x, C.y - A.y);
            const float lc = std::hypot(A.x - B.x, A.y - B.y);
            const float per = la + lb + lc;
            const float ix = (la * A.x + lb * B.x + lc * C.x) / per;
            const float iy = (la * A.y + lb * B.y + lc * C.y) / per;
            const float area = 0.5f * std::fabs((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x));
            const float inr = 2.0f * area / per;
            const float k = (inr - rr) / inr;
            auto shrink = [&](D2D1_POINT_2F p) {
                return D2D1::Point2F(ix + k * (p.x - ix), iy + k * (p.y - iy));
            };
            s.play[0] = shrink(A); s.play[1] = shrink(T); s.play[2] = shrink(U); s.play[3] = shrink(B);
            s.play[4] = shrink(T); s.play[5] = shrink(C); s.play[6] = shrink(C); s.play[7] = shrink(U);
            return s;
        }();
        return shapes;
    }

    bool EnsureRoundJoinStyle() {
        if (roundJoinStyle_) return true;
        if (!d2dFactory_) return false;
        return SUCCEEDED(d2dFactory_->CreateStrokeStyle(
                   D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                               D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.0f,
                                               D2D1_DASH_STYLE_SOLID, 0.0f),
                   nullptr, 0, &roundJoinStyle_)) && roundJoinStyle_;
    }

    void DrawPlayPauseIcon(D2D1_POINT_2F center, float opacity, float press) {
        if (!d2dFactory_) return;
        EnsureRoundJoinStyle();
        const PlayPauseShapes& sh = GetPlayPauseShapes();
        const float m = Clamp(playMorph_, 0.0f, 1.0f);

        D2D1_POINT_2F p[8];
        for (int i = 0; i < 8; ++i) {
            p[i] = D2D1::Point2F(sh.pause[i].x + (sh.play[i].x - sh.pause[i].x) * m,
                                 sh.pause[i].y + (sh.play[i].y - sh.pause[i].y) * m);
        }

        ComPtr<ID2D1PathGeometry> quad[2];
        for (int q = 0; q < 2; ++q) {
            if (FAILED(d2dFactory_->CreatePathGeometry(&quad[q])) || !quad[q]) return;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(quad[q]->Open(&sink)) || !sink) return;
            sink->BeginFigure(p[q * 4], D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(p[q * 4 + 1]);
            sink->AddLine(p[q * 4 + 2]);
            sink->AddLine(p[q * 4 + 3]);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if (FAILED(sink->Close())) return;
        }

        D2D1_MATRIX_3X2_F oldTransform;
        target_->GetTransform(&oldTransform);

        // Everything is drawn opaque inside a layer that carries the icon opacity,
        // so the overlap between fill, stroke and the two halves never double-blends.
        ComPtr<ID2D1Layer> layer;
        const bool layered = SUCCEEDED(target_->CreateLayer(nullptr, &layer)) && layer;
        if (layered) {
            target_->PushLayer(
                D2D1::LayerParameters(D2D1::RectF(center.x - 18.0f, center.y - 18.0f,
                                                  center.x + 18.0f, center.y + 18.0f),
                                      nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                      D2D1::IdentityMatrix(), opacity, nullptr, D2D1_LAYER_OPTIONS_NONE),
                layer.Get());
        }
        accentBrush_->SetOpacity(layered ? 1.0f : opacity);

        const float sf = 1.0f - 0.13f * press;
        target_->SetTransform(D2D1::Matrix3x2F::Translation(center.x, center.y) *
                              D2D1::Matrix3x2F::Scale(sf, sf, center) * oldTransform);
        for (int q = 0; q < 2; ++q) {
            target_->FillGeometry(quad[q].Get(), accentBrush_.Get());
            target_->DrawGeometry(quad[q].Get(), accentBrush_.Get(), kPlayPauseCorner * 2.0f,
                                  roundJoinStyle_.Get());
        }
        target_->SetTransform(oldTransform);
        if (layered) target_->PopLayer();
        accentBrush_->SetOpacity(1.0f);
    }

    // ── Song-title change animation ──────────────────────────────────────────
    // Scripts whose characters shape/reorder together can't be animated one
    // glyph at a time, so those titles simply fall back to the plain redraw.
    static bool TitleNeedsShaping(wchar_t c) {
        return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0590 && c <= 0x0DFF) ||
               (c >= 0x0E00 && c <= 0x0EFF) || c == 0x200D;
    }

    // Bunbun: a new title starts at its first word, and DrawMarqueeText rests there
    // 1.5 s before scrolling, so the hand-off after this animation is at 0 as well.
    static float TitleScrollOffset(const TitleScroll&, double) { return 0.0f; }

    // allLayouts = false keeps only the glyphs visible right now (the outgoing
    // title is frozen, so nothing else of it will ever be seen).
    bool BuildTitleGlyphs(const std::wstring& text, float wrapHeight, float available, double now,
                          bool allLayouts, std::vector<TitleGlyph>& out, TitleScroll& scroll) {
        out.clear();
        scroll = TitleScroll{};
        if (!dwriteFactory_ || !textFormat_ || text.empty() || text.size() > 120) return false;

        ComPtr<IDWriteTextLayout> full;
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                                    textFormat_.Get(), 2000.0f, wrapHeight, &full)) || !full) {
            return false;
        }
        DWRITE_TEXT_METRICS tm = {};
        full->GetMetrics(&tm);
        scroll.width = tm.widthIncludingTrailingWhitespace;
        scroll.scrolls = scroll.width > available;
        scroll.cycle = scroll.width + 38.0f;
        scroll.offset0 = TitleScrollOffset(scroll, now);

        for (UINT32 i = 0; i < text.size();) {
            const wchar_t c = text[i];
            if (TitleNeedsShaping(c)) return false;
            const UINT32 len = (IS_HIGH_SURROGATE(c) && i + 1 < text.size() && IS_LOW_SURROGATE(text[i + 1])) ? 2 : 1;
            if (c != L' ' && c != L'\t' && c != 0x00A0 && c != 0x3000) {
                TitleGlyph g;
                g.ch0 = c;
                g.ch1 = (len == 2) ? text[i + 1] : 0;
                float px = 0.0f, py = 0.0f;
                DWRITE_HIT_TEST_METRICS htm = {};
                if (FAILED(full->HitTestTextPosition(i, FALSE, &px, &py, &htm))) return false;
                g.x = px;
                g.sx = px - scroll.offset0;
                g.eligible = (g.sx + htm.width > 0.0f) && (g.sx < available);
                if (allLayouts || g.eligible) {
                    if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str() + i, len, textFormat_.Get(),
                                                                2000.0f, wrapHeight, &g.layout)) || !g.layout) {
                        return false;
                    }
                    DWRITE_TEXT_METRICS gm = {};
                    g.layout->GetMetrics(&gm);
                    g.w = gm.widthIncludingTrailingWhitespace;
                    g.h = gm.height;
                    const float hv = std::sin(static_cast<float>(i) * 12.9898f + static_cast<float>(c) * 78.233f) * 43758.5453f;
                    g.seed = hv - std::floor(hv);
                    out.push_back(std::move(g));
                }
            }
            i += len;
        }
        return !out.empty();
    }

    // Longest common subsequence of identical, visible glyphs; ties prefer the
    // pairs that travel the least. Order is preserved, so matched letters never cross.
    static void MatchTitleGlyphs(std::vector<TitleGlyph>& a, std::vector<TitleGlyph>& b) {
        const size_t n = a.size(), m = b.size();
        std::vector<int> dp((n + 1) * (m + 1), 0);
        auto at = [&](size_t i, size_t j) -> int& { return dp[i * (m + 1) + j]; };
        auto same = [&](size_t i, size_t j) {
            return a[i].eligible && b[j].eligible && a[i].ch0 == b[j].ch0 && a[i].ch1 == b[j].ch1;
        };
        auto pairScore = [&](size_t i, size_t j) {
            return 1000 - static_cast<int>(std::min(std::fabs(a[i].sx - b[j].sx), 900.0f));
        };
        for (size_t i = 1; i <= n; ++i) {
            for (size_t j = 1; j <= m; ++j) {
                int best = std::max(at(i - 1, j), at(i, j - 1));
                if (same(i - 1, j - 1)) best = std::max(best, at(i - 1, j - 1) + pairScore(i - 1, j - 1));
                at(i, j) = best;
            }
        }
        size_t i = n, j = m;
        while (i > 0 && j > 0) {
            if (same(i - 1, j - 1) && at(i, j) == at(i - 1, j - 1) + pairScore(i - 1, j - 1)) {
                a[i - 1].match = static_cast<int>(j - 1);
                b[j - 1].match = static_cast<int>(i - 1);
                --i; --j;
            } else if (at(i - 1, j) >= at(i, j - 1)) {
                --i;
            } else {
                --j;
            }
        }
    }

    void StartTitleTransition(const std::wstring& oldText, const std::wstring& newText,
                              D2D1_RECT_F rect, double now) {
        const float h = rect.bottom - rect.top;
        const float avail = rect.right - rect.left;
        if (!BuildTitleGlyphs(oldText, h, avail, now, false, titleOld_, titleOldScroll_) ||
            !BuildTitleGlyphs(newText, h, avail, now, true, titleNew_, titleNewScroll_)) {
            titleOld_.clear();
            titleNew_.clear();
            return;
        }
        MatchTitleGlyphs(titleOld_, titleNew_);
        titleAvail_ = avail;
        titleAnimStart_ = now;
        titleAnimDur_ = 1.0f;
        titleAnimActive_ = true;
    }

    // t: 0 = solid, 1 = gone. Rises, sways, stretches upward and thins out; extra
    // translucent copies trail above it so the edge looks soft instead of cut.
    // Used in reverse (t counting down, smaller amp) to make letters condense in.
    void DrawSmokeGlyph(const TitleGlyph& g, D2D1_POINT_2F origin, float t, float amp,
                        ID2D1Brush* brush, float baseOpacity) {
        if (t >= 0.999f || !g.layout) return;
        if (t <= 0.0f) {
            brush->SetOpacity(baseOpacity);
            target_->DrawTextLayout(origin, g.layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
            return;
        }
        D2D1_MATRIX_3X2_F oldT;
        target_->GetTransform(&oldT);

        const float seed = g.seed;
        const float rise = 16.0f * amp * std::pow(t, 1.15f);
        const float sway = std::sin(t * (4.5f + 3.0f * seed) + seed * 6.2832f) * 4.5f * amp * std::sqrt(t);
        const float angle = (seed - 0.5f) * 50.0f * t * amp;
        const float sx = 1.0f + 0.55f * t * amp;
        const float sy = 1.0f + 1.40f * t * amp;
        const float alpha = std::pow(1.0f - t, 1.6f) * baseOpacity;
        const float soft = SkipSmooth(t / 0.25f);  // soft copies fade in as it starts to dissolve
        const float spread = t * 3.2f * amp;
        const D2D1_POINT_2F pivot = D2D1::Point2F(origin.x + g.w * 0.5f, origin.y + g.h * 0.82f);

        for (int k = 0; k < 4; ++k) {
            const float dx = (k == 1) ? -spread * 0.9f : (k == 3 ? spread * 0.9f : 0.0f);
            const float dy = -static_cast<float>(k) * spread * 0.7f;
            const float a = (k == 0) ? alpha * (1.0f - 0.55f * soft) : alpha * 0.28f * soft;
            if (a <= 0.003f) continue;
            brush->SetOpacity(Clamp(a, 0.0f, 1.0f));
            target_->SetTransform(
                D2D1::Matrix3x2F::Scale(sx, sy, pivot) *
                D2D1::Matrix3x2F::Rotation(angle * (1.0f + 0.25f * static_cast<float>(k)), pivot) *
                D2D1::Matrix3x2F::Translation(sway + dx, -rise + dy) * oldT);
            target_->DrawTextLayout(origin, g.layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
        target_->SetTransform(oldT);
        brush->SetOpacity(baseOpacity);
    }

    // Returns true when it drew the title itself (animation running); false means
    // the caller should draw the title the normal way.
    bool DrawTitleTransition(const std::wstring& text, D2D1_RECT_F rect, ID2D1Brush* brush,
                             double now, bool realTitle) {
        // The title is only drawn while the expanded media card is visible, so a
        // long gap means the old title was never on screen: just snap.
        const bool recentlyDrawn = titleLastDrawn_ >= 0.0 && (now - titleLastDrawn_) < 0.25;
        titleLastDrawn_ = now;

        if (text != titleShown_) {
            const std::wstring oldText = titleShown_;
            const bool oldWasReal = !titleShownPlaceholder_;
            titleShown_ = text;
            titleShownPlaceholder_ = !realTitle;
            titleAnimActive_ = false;
            titleOld_.clear();
            titleNew_.clear();
            if (expandedAnim_ && recentlyDrawn && realTitle && oldWasReal && !oldText.empty()) {
                StartTitleTransition(oldText, text, rect, now);
            }
        } else if (titleAnimActive_ && (!recentlyDrawn || !expandedAnim_)) {
            titleAnimActive_ = false;
            titleOld_.clear();
            titleNew_.clear();
        }

        if (!titleAnimActive_) return false;

        const float time = static_cast<float>(now - titleAnimStart_);
        if (time >= titleAnimDur_) {
            titleAnimActive_ = false;
            titleOld_.clear();
            titleNew_.clear();
            return false;
        }

        const float baseOpacity = brush->GetOpacity();
        const float avail = std::max(titleAvail_, 1.0f);
        const float newOffset = TitleScrollOffset(titleNewScroll_, now);
        target_->PushAxisAlignedClip(
            D2D1::RectF(rect.left - 4.0f, rect.top - 20.0f, rect.right + 4.0f, rect.bottom + 6.0f),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Old letters with no partner: dissolve into smoke, left to right. The old
        // title is frozen at the spot where it was when the track changed.
        const float oldSpan = std::max(std::min(titleOldScroll_.width, avail), 1.0f);
        for (const TitleGlyph& g : titleOld_) {
            if (g.match >= 0 || !g.eligible) continue;
            const float f = Clamp(g.sx / oldSpan, 0.0f, 1.0f);
            const float t = Clamp((time - 0.22f * f) / 0.60f, 0.0f, 1.0f);
            DrawSmokeGlyph(g, D2D1::Point2F(rect.left + g.sx, rect.top), t, 1.0f, brush, baseOpacity);
        }

        // Letters present in both titles: glide to their new position (which keeps
        // scrolling if the new title is a marquee).
        brush->SetOpacity(baseOpacity);
        for (const TitleGlyph& g : titleOld_) {
            if (g.match < 0) continue;
            const TitleGlyph& n = titleNew_[static_cast<size_t>(g.match)];
            const float e = SkipEaseInOut((time - 0.04f) / 0.55f);
            const float targetX = n.x - newOffset;
            const float x = g.sx + (targetX - g.sx) * e;
            const float lift = -1.5f * std::sin(3.14159265f * e);
            target_->DrawTextLayout(D2D1::Point2F(rect.left + x, rect.top + lift), g.layout.Get(), brush,
                                    D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        // New letters with no partner: condense out of the smoke. A scrolling title
        // is drawn at its live marquee position, including the wrapped copy.
        const float newSpan = std::max(std::min(titleNewScroll_.width, avail), 1.0f);
        for (const TitleGlyph& g : titleNew_) {
            if (g.match >= 0) continue;
            const float xs = g.x - newOffset;
            const float f = Clamp(xs / newSpan, 0.0f, 1.0f);
            const float u = Clamp((time - 0.28f - 0.20f * f) / 0.50f, 0.0f, 1.0f);
            if (xs + g.w > 0.0f && xs < avail) {
                DrawSmokeGlyph(g, D2D1::Point2F(rect.left + xs, rect.top), 1.0f - u, 0.45f, brush, baseOpacity);
            }
            if (titleNewScroll_.scrolls) {
                const float xs2 = xs + titleNewScroll_.cycle;
                if (xs2 + g.w > 0.0f && xs2 < avail) {
                    DrawSmokeGlyph(g, D2D1::Point2F(rect.left + xs2, rect.top), 1.0f - u, 0.45f, brush, baseOpacity);
                }
            }
        }

        brush->SetOpacity(baseOpacity);
        target_->PopAxisAlignedClip();
        g_layoutDirty = true;
        return true;
    }

    // ── Progress bar styles (Wavy / Squiggle / Bar) ──────────────────────────
    // Slim is the original bar and keeps its own code path in DrawMedia. The
    // hit test is untouched: every shape here stays inside the scrubber's hit area.
    void DrawStyledProgress(ProgressStyle style, float barLeft, float barRight, float cy,
                            float progress, bool dragging, bool playing, double now) {
        const float width = barRight - barLeft;
        if (width <= 8.0f) return;
        progress = Clamp(progress, 0.0f, 1.0f);
        const float thumbX = barLeft + width * progress;

        ComPtr<ID2D1SolidColorBrush> accent, track;
        if (FAILED(target_->CreateSolidColorBrush(WithAlpha(material_.accent, 1.0f), &accent)) || !accent) return;
        if (FAILED(target_->CreateSolidColorBrush(material_.raisedStrong, &track)) || !track) return;
        EnsureRoundJoinStyle();

        // Wave strength eases toward 0 when paused and back to 1 when playing.
        {
            const bool fresh = progressLastTime_ >= 0.0 && (now - progressLastTime_) < 0.25;
            const float target = playing ? 1.0f : 0.0f;
            if (!fresh) {
                progressAmp_ = target;
            } else {
                const float dt = static_cast<float>(now - progressLastTime_);
                progressAmp_ += (target - progressAmp_) * (1.0f - std::exp(-dt / 0.16f));
                if (std::fabs(target - progressAmp_) < 0.004f) {
                    progressAmp_ = target;
                } else {
                    g_layoutDirty = true;
                }
            }
            progressLastTime_ = now;
        }
        const float amp = progressAmp_;

        auto drawEndDot = [&](float x) {
            ComPtr<ID2D1SolidColorBrush> dot;
            if (SUCCEEDED(target_->CreateSolidColorBrush(WithAlpha(material_.accent, 0.9f), &dot)) && dot) {
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 1.7f, 1.7f), dot.Get());
            }
        };

        if (style == ProgressStyle::Bar) {
            // Thick rounded bar split by a vertical line thumb, with a dot at the end.
            const float half = dragging ? 4.5f : 4.0f;
            const float gap = dragging ? 5.0f : 4.0f;
            const float fillRight = thumbX - gap;
            const float trackLeft = thumbX + gap;
            if (fillRight - barLeft > 1.0f) {
                target_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(barLeft, cy - half, fillRight, cy + half), half, half),
                    accent.Get());
            }
            if (barRight - trackLeft > 1.0f) {
                target_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(trackLeft, cy - half, barRight, cy + half), half, half),
                    track.Get());
                drawEndDot(barRight - half);
            }
            const float tw = dragging ? 4.0f : 3.0f;
            const float th = dragging ? 24.0f : 20.0f;
            target_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(thumbX - tw * 0.5f, cy - th * 0.5f, thumbX + tw * 0.5f, cy + th * 0.5f),
                                  tw * 0.5f, tw * 0.5f),
                accent.Get());
            return;
        }

        // Wavy and Squiggle: a travelling sine wave up to the thumb, a thin flat
        // line after it. Squiggle is shorter, shallower and about half the speed.
        const bool wavy = (style == ProgressStyle::Wavy);
        const float A = (wavy ? 3.2f : 1.8f) * amp;
        const float lambda = wavy ? 26.0f : 15.0f;
        const double freq = wavy ? 0.9 : 0.45;
        const float strokeW = wavy ? 3.4f : 3.0f;
        const float k = 6.2831853f / lambda;
        const float phase = static_cast<float>(std::fmod(now * freq, 1.0) * 6.2831853);
        const float endTaper = wavy ? 16.0f : 10.0f;  // flattens into the thumb

        if (thumbX - barLeft > 1.5f) {
            std::vector<D2D1_POINT_2F> pts;
            pts.reserve(static_cast<size_t>((thumbX - barLeft) / 1.25f) + 3);
            for (float x = barLeft;; x += 1.25f) {
                const float xx = std::min(x, thumbX);
                const float taper = std::min(SkipSmooth((xx - barLeft) / 10.0f),
                                             SkipSmooth((thumbX - xx) / endTaper));
                pts.push_back(D2D1::Point2F(xx, cy + A * taper * std::sin(k * (xx - barLeft) - phase)));
                if (x >= thumbX) break;
            }
            ComPtr<ID2D1PathGeometry> path;
            if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&path)) && path) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(path->Open(&sink)) && sink) {
                    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
                    if (pts.size() > 1) sink->AddLines(pts.data() + 1, static_cast<UINT32>(pts.size() - 1));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    if (SUCCEEDED(sink->Close())) {
                        target_->DrawGeometry(path.Get(), accent.Get(), strokeW, roundJoinStyle_.Get());
                    }
                }
            }
        }

        const float thumbR = dragging ? 7.0f : 5.5f;
        const float trackStart = thumbX + (wavy ? thumbR + 3.0f : 4.0f);
        if (barRight - trackStart > 2.0f) {
            const float tw = wavy ? 2.2f : 2.8f;
            target_->DrawLine(D2D1::Point2F(trackStart, cy), D2D1::Point2F(barRight - 2.0f, cy),
                              track.Get(), tw, roundJoinStyle_.Get());
            if (wavy) drawEndDot(barRight - 1.7f);
        }

        if (wavy) {
            if (dragging) {
                ComPtr<ID2D1SolidColorBrush> halo;
                if (SUCCEEDED(target_->CreateSolidColorBrush(WithAlpha(material_.accent, 0.22f), &halo)) && halo) {
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), thumbR * 2.0f, thumbR * 2.0f),
                                         halo.Get());
                }
            }
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), thumbR, thumbR), accent.Get());
        } else {
            const float tw = dragging ? 4.0f : 3.0f;
            const float th = dragging ? 22.0f : 18.0f;
            target_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(thumbX - tw * 0.5f, cy - th * 0.5f, thumbX + tw * 0.5f, cy + th * 0.5f),
                                  tw * 0.5f, tw * 0.5f),
                accent.Get());
        }
    }

    // ── Artist / album: gentle swap ──────────────────────────────────────────
    // The old line drifts up and fades while the new one rises into place. Only
    // runs when the line was already on screen; otherwise the text just appears.
    void DrawSwapMarquee(SwapTextState& st, const std::wstring& text, D2D1_RECT_F rect,
                         IDWriteTextFormat* format, ID2D1Brush* brush, double now, float speed,
                         MarqueeLayoutCache& cache, float delay) {
        constexpr float kDur = 0.38f;
        const bool recentlyDrawn = st.lastDrawn >= 0.0 && (now - st.lastDrawn) < 0.25;
        st.lastDrawn = now;

        if (text != st.shown) {
            if (recentlyDrawn && expandedAnim_) {
                st.prev = st.shown;
                st.start = now;
            } else {
                st.prev.clear();
                st.start = -1.0;
            }
            st.shown = text;
        } else if (st.start >= 0.0 && !recentlyDrawn) {
            st.start = -1.0;
        }

        const float elapsed = st.start >= 0.0 ? static_cast<float>(now - st.start) : -1.0f;
        if (elapsed < 0.0f || elapsed >= delay + kDur) {
            st.start = -1.0;
            DrawMarqueeText(text, rect, format, brush, now, speed, cache);
            return;
        }

        const float u = Clamp((elapsed - delay) / kDur, 0.0f, 1.0f);
        const float e = SkipEaseInOut(u);
        const float base = brush->GetOpacity();
        const float dy = 5.0f;
        D2D1_RECT_F up = rect;
        up.top -= dy * e;
        up.bottom -= dy * e;
        D2D1_RECT_F down = rect;
        down.top += dy * (1.0f - e);
        down.bottom += dy * (1.0f - e);

        brush->SetOpacity(base * (1.0f - SkipSmooth(u * 1.4f)));
        DrawMarqueeText(st.prev, up, format, brush, now, speed, st.prevCache);
        brush->SetOpacity(base * SkipSmooth((u - 0.25f) / 0.75f));
        DrawMarqueeText(text, down, format, brush, now, speed, cache);
        brush->SetOpacity(base);
        g_layoutDirty = true;
    }

    // ── Album art: flip ──────────────────────────────────────────────────────
    // The decoder assigns a fresh generation on every poll during the post-change
    // settle window even when the image is identical, so the flip is keyed on a
    // hash of the pixels instead and only fires when the picture really changed.
    uint64_t ArtContentHash(const MediaSnapshot& media) {
        if (!media.art) return 0;
        if (artHashValue_ != 0 && artHashGen_ == media.art->generation) return artHashValue_;
        uint64_t h = 1469598103934665603ull;
        h = (h ^ static_cast<uint64_t>(media.art->width)) * 1099511628211ull;
        h = (h ^ static_cast<uint64_t>(media.art->height)) * 1099511628211ull;
        const std::vector<uint8_t>& v = media.art->bgra;
        for (size_t i = 0; i < v.size(); i += 61) {
            h = (h ^ v[i]) * 1099511628211ull;
        }
        if (h == 0) h = 1;
        artHashGen_ = media.art->generation;
        artHashValue_ = h;
        return h;
    }

    void DrawArtFace(ID2D1Bitmap* bitmap, D2D1_RECT_F rect, float radius) {
        ComPtr<ID2D1RoundedRectangleGeometry> mask;
        HRESULT hrMask = d2dFactory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, radius, radius), &mask);
        ComPtr<ID2D1Layer> layer;
        HRESULT hrLayer = target_->CreateLayer(nullptr, &layer);
        const bool roundedClip = SUCCEEDED(hrMask) && SUCCEEDED(hrLayer) && mask && layer;
        if (roundedClip) {
            target_->PushLayer(D2D1::LayerParameters(rect, mask.Get()), layer.Get());
        } else {
            target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }
        if (bitmap) {
            target_->DrawBitmap(bitmap, rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            accentBrush_->SetOpacity(0.24f);
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), accentBrush_.Get());
            accentBrush_->SetOpacity(1.0f);
        }
        if (roundedClip) {
            target_->PopLayer();
        } else {
            target_->PopAxisAlignedClip();
        }
    }

    void DrawAlbumArtFlip(const MediaSnapshot& media, D2D1_RECT_F rect, double now,
                          float radius, bool drawBadge) {
        constexpr double kFlipDuration = 0.62;
        const bool recentlyDrawn = artFlipLastDrawn_ >= 0.0 && (now - artFlipLastDrawn_) < 0.25;
        artFlipLastDrawn_ = now;

        const uint64_t hash = ArtContentHash(media);
        if (!artFlipInit_ || !recentlyDrawn) {
            artFlipInit_ = true;
            artFlipHash_ = hash;
            artFlipStart_ = -1.0;
            artFlipOld_.Reset();
        } else if (hash != artFlipHash_) {
            // Mid-flip changes just retarget the face shown after the turn.
            if (expandedAnim_ && artFlipStart_ < 0.0 && hash != 0) {
                artFlipOld_ = artBitmap_;  // still the previous cover: not rebuilt yet
                artFlipStart_ = now;
            }
            artFlipHash_ = hash;
        }

        float p = -1.0f;
        if (artFlipStart_ >= 0.0) {
            p = static_cast<float>((now - artFlipStart_) / kFlipDuration);
            if (p >= 1.0f) {
                artFlipStart_ = -1.0;
                artFlipOld_.Reset();
                p = -1.0f;
            }
        }
        if (p < 0.0f) {
            DrawAlbumArt(media, rect, now, radius, drawBadge);
            return;
        }

        g_layoutDirty = true;
        const float q = SkipEaseInOut(p);
        const float c = std::fabs(std::cos(3.14159265f * q));  // 1 = facing us, 0 = edge-on
        if (c < 0.02f) return;

        const D2D1_POINT_2F center = D2D1::Point2F((rect.left + rect.right) * 0.5f,
                                                   (rect.top + rect.bottom) * 0.5f);
        D2D1_MATRIX_3X2_F oldTransform;
        target_->GetTransform(&oldTransform);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(c, 1.0f - 0.05f * (1.0f - c), center) * oldTransform);

        if (q < 0.5f) {
            DrawArtFace(artFlipOld_.Get(), rect, radius);
        } else {
            DrawAlbumArt(media, rect, now, radius, drawBadge);
        }

        // Darken the face as it turns away, for a sense of depth.
        ComPtr<ID2D1SolidColorBrush> shade;
        if (SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f * (1.0f - c)), &shade)) && shade) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), shade.Get());
        }
        target_->SetTransform(oldTransform);
    }

    // ── Album art (collapsed pill): record drop ──────────────────────────────
    // A track change plays as a record swap: the old disc spins up and is thrown
    // away while the new one drops in from above, spinning fast, then springs onto
    // the pill and slows to rest. Skipping backwards spins the other way. Same
    // cover, new track: a short press-and-rebound with the ring and glint.
    void EnsureArtBitmap(const MediaSnapshot& media) {
        if (!media.art) return;
        const BitmapPixels& art = *media.art;
        if (artGeneration_ != art.generation || !artBitmap_) {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            target_->CreateBitmap(D2D1::SizeU(art.width, art.height),
                                  art.bgra.data(), art.width * 4,
                                  &props, &artBitmap_);
            artGeneration_ = art.generation;
        }
    }

    // One disc: scaled about its centre, the picture spun inside a fixed round
    // mask. `lag` is how far the angle moved over the last few ms; it drives the
    // ghost copies drawn on top of the disc.
    void DrawArtDisc(ID2D1Bitmap* bitmap, D2D1_RECT_F rect, float radius,
                     float scale, float angle, float lag, float alpha) {
        if (alpha <= 0.004f || scale <= 0.02f) return;
        const D2D1_POINT_2F c = D2D1::Point2F((rect.left + rect.right) * 0.5f,
                                              (rect.top + rect.bottom) * 0.5f);
        D2D1_MATRIX_3X2_F base;
        target_->GetTransform(&base);
        const D2D1_MATRIX_3X2_F scaled = D2D1::Matrix3x2F::Scale(scale, scale, c) * base;
        target_->SetTransform(scaled);

        ComPtr<ID2D1RoundedRectangleGeometry> mask;
        ComPtr<ID2D1Layer> layer;
        const bool clipped =
            SUCCEEDED(d2dFactory_->CreateRoundedRectangleGeometry(
                D2D1::RoundedRect(rect, radius, radius), &mask)) &&
            SUCCEEDED(target_->CreateLayer(nullptr, &layer)) && mask && layer;
        if (clipped) {
            target_->PushLayer(D2D1::LayerParameters(rect, mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                     D2D1::IdentityMatrix(), Clamp(alpha, 0.0f, 1.0f)),
                               layer.Get());
        } else {
            target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }

        const float speed = std::min(std::fabs(lag) / 16.0f, 1.0f);
        const int ghosts = (bitmap && speed > 0.05f) ? 3 : 0;
        for (int k = 0; k <= ghosts; ++k) {
            const float a = (k == 0) ? 1.0f : 0.30f * speed * (1.0f - 0.30f * static_cast<float>(k - 1));
            target_->SetTransform(
                D2D1::Matrix3x2F::Rotation(angle - lag * static_cast<float>(k), c) * scaled);
            if (bitmap) {
                target_->DrawBitmap(bitmap, rect, a, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                accentBrush_->SetOpacity(0.24f);
                target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), accentBrush_.Get());
                accentBrush_->SetOpacity(1.0f);
            }
        }

        if (clipped) {
            target_->PopLayer();
        } else {
            target_->PopAxisAlignedClip();
        }
        target_->SetTransform(base);
    }

    // Accent ring leaving the rim + a soft glint sweeping across the disc.
    // ringP / sheenP: <0 or >1 = not running, otherwise progress 0..1.
    void DrawArtLandingFx(D2D1_RECT_F rect, float radius, float ringP, float sheenP, float dir) {
        const D2D1_POINT_2F c = D2D1::Point2F((rect.left + rect.right) * 0.5f,
                                              (rect.top + rect.bottom) * 0.5f);
        const float r0 = std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.5f;

        if (sheenP > 0.0f && sheenP < 1.0f) {
            const float e = SkipEaseInOut(sheenP);
            const float pos = (e * 2.0f - 1.0f) * 1.3f * r0;
            const float half = 0.55f * r0;
            const float dx = 0.82f * dir, dy = -0.57f;
            const float peak = 0.34f * std::sin(3.14159265f * sheenP);
            D2D1_GRADIENT_STOP stops[3] = {
                {0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f)},
                {0.5f, D2D1::ColorF(1.0f, 1.0f, 1.0f, peak)},
                {1.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f)},
            };
            ComPtr<ID2D1GradientStopCollection> coll;
            ComPtr<ID2D1LinearGradientBrush> glint;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, &coll)) && coll &&
                SUCCEEDED(target_->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(
                        D2D1::Point2F(c.x + dx * (pos - half), c.y + dy * (pos - half)),
                        D2D1::Point2F(c.x + dx * (pos + half), c.y + dy * (pos + half))),
                    coll.Get(), &glint)) && glint) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), glint.Get());
            }
        }

        for (int i = 0; i < 2; ++i) {
            const float p = (ringP - 0.16f * static_cast<float>(i)) / (1.0f - 0.16f * static_cast<float>(i));
            if (p <= 0.0f || p >= 1.0f) continue;
            const float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
            const float a = (i == 0 ? 0.60f : 0.28f) * (1.0f - p) * (1.0f - p);
            const float rr = r0 + 0.8f + 4.2f * e;
            accentBrush_->SetOpacity(a);
            target_->DrawEllipse(D2D1::Ellipse(c, rr, rr), accentBrush_.Get(), 0.6f + 1.4f * (1.0f - p));
        }
        accentBrush_->SetOpacity(1.0f);
    }

    void DrawAlbumArtDrop(const MediaSnapshot& media, D2D1_RECT_F rect, double now, float radius) {
        constexpr double kFullDur = 0.95;
        constexpr double kPulseDur = 0.55;
        constexpr double kTitleWait = 0.9;  // how long to wait for a new cover after a title change
        const bool recentlyDrawn = pillArtLastDrawn_ >= 0.0 && (now - pillArtLastDrawn_) < 0.25;
        pillArtLastDrawn_ = now;

        const uint64_t hash = ArtContentHash(media);
        if (!pillArtInit_ || !recentlyDrawn || !pillCoverAnim_) {
            // First frame, the pill was off screen, or the animation is turned off: snap.
            pillArtInit_ = true;
            pillArtHash_ = hash;
            pillTitle_ = media.title;
            pillArtStart_ = -1.0;
            pillTitlePendingAt_ = -1.0;
            pillArtOld_.Reset();
            pillArtLast_.Reset();
        } else {
            float dir = 1.0f;
            if (lastSkipClickAt_ >= 0.0 && (now - lastSkipClickAt_) < 6.0) dir = lastSkipDir_;

            if (hash != pillArtHash_) {
                // The picture really changed (the decoder re-issues generations
                // for identical covers, hence the hash).
                if (pillArtStart_ < 0.0 && hash != 0) {
                    pillArtOld_ = pillArtLast_;  // what was on screen last frame; may be empty
                    pillArtStart_ = now;
                    pillArtPulse_ = false;
                    pillArtDir_ = dir;
                    pillArtFullAt_ = now;
                    pillTitlePendingAt_ = -1.0;
                }
                pillArtHash_ = hash;
            }

            if (media.title != pillTitle_) {
                const bool hadTitle = !pillTitle_.empty();
                pillTitle_ = media.title;
                // Only arm the pulse if no drop is running or just finished; the
                // metadata often arrives before the new cover does.
                if (hadTitle && !media.title.empty() && pillArtStart_ < 0.0 &&
                    (now - pillArtFullAt_) > 1.6) {
                    pillTitlePendingAt_ = now;
                }
            }
            if (pillTitlePendingAt_ >= 0.0) {
                if (pillArtStart_ >= 0.0) {
                    pillTitlePendingAt_ = -1.0;
                } else if (now - pillTitlePendingAt_ >= kTitleWait) {
                    // No new cover showed up: same album, so do the gentle pulse.
                    pillTitlePendingAt_ = -1.0;
                    pillArtOld_.Reset();
                    pillArtStart_ = now;
                    pillArtPulse_ = true;
                    pillArtDir_ = dir;
                } else {
                    g_layoutDirty = true;  // keep frames coming while we wait
                }
            }
        }

        EnsureArtBitmap(media);
        ID2D1Bitmap* fresh = !media.art ? nullptr : artBitmap_.Get();

        float p = -1.0f;
        if (pillArtStart_ >= 0.0) {
            p = static_cast<float>((now - pillArtStart_) / (pillArtPulse_ ? kPulseDur : kFullDur));
            if (p >= 1.0f) {
                pillArtStart_ = -1.0;
                pillArtOld_.Reset();
                p = -1.0f;
            }
        }

        if (p < 0.0f) {
            DrawAlbumArt(media, rect, now, radius, false);
        } else {
            g_layoutDirty = true;
            const float dir = pillArtDir_;

            if (!pillArtPulse_) {
                // Old disc: spins up and is thrown away behind the new one.
                if (pillArtOld_) {
                    const float u = Clamp(p / 0.40f, 0.0f, 1.0f);
                    if (u < 1.0f) {
                        const float up = std::max(u - 0.058f, 0.0f);
                        const float k = u * u * u, kp = up * up * up;
                        DrawArtDisc(pillArtOld_.Get(), rect, radius,
                                    1.0f - 0.26f * k,
                                    dir * 150.0f * k,
                                    dir * 150.0f * (k - kp),
                                    1.0f - SkipSmooth(u / 0.9f));
                    }
                }
                // New disc: drops in big and fast-spinning, springs down, slows to rest.
                if (p > 0.14f) {
                    const float u = Clamp((p - 0.14f) / 0.86f, 0.0f, 1.0f);
                    const float up = std::max(u - 0.027f, 0.0f);
                    const float settle = 1.0f - SkipSmooth((u - 0.8f) / 0.2f);
                    const float scale = 1.0f + 0.30f * std::exp(-5.2f * u) * std::cos(7.5f * u) * settle;
                    const float angle = -dir * 220.0f * std::pow(1.0f - u, 3.0f);
                    const float anglePrev = -dir * 220.0f * std::pow(1.0f - up, 3.0f);
                    DrawArtDisc(fresh, rect, radius, scale, angle, angle - anglePrev,
                                SkipSmooth(u / 0.28f));
                }
                DrawArtLandingFx(rect, radius, (p - 0.32f) / 0.60f, (p - 0.34f) / 0.46f, dir);
            } else {
                // Same cover: press-and-rebound in place.
                auto wob = [](float x) { return std::exp(-5.5f * x) * std::sin(11.0f * x); };
                const float w = wob(p);
                const float wp = wob(std::max(p - 0.04f, 0.0f));
                DrawArtDisc(fresh, rect, radius, 1.0f - 0.20f * w, dir * 18.0f * w,
                            dir * 18.0f * (w - wp), 1.0f);
                DrawArtLandingFx(rect, radius, (p - 0.08f) / 0.62f, (p - 0.12f) / 0.55f, dir);
            }
        }

        if (fresh) {
            pillArtLast_ = artBitmap_;
        } else {
            pillArtLast_.Reset();
        }
    }

