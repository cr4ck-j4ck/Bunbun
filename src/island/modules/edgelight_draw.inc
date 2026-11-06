    // ── Edge light ──────────────────────────────────────────────────────────
    // Bunbun: drawing the light along the island's outline. When and how bright is
    // EdgeLight (edgelight_module.inc); DrawIslandSurface leaves the outline in
    // edgeRect_ / edgeRadius_, and Render calls DrawEdgeLight after the content.
    D2D1_RECT_F edgeRect_{};
    float edgeRadius_ = 0.0f;
    bool edgeValid_ = false;
    // The outline as a path, kept while the island keeps its size.
    ComPtr<ID2D1PathGeometry> edgePath_;
    D2D1_RECT_F edgePathRect_{};
    float edgePathRadius_ = -1.0f, edgePathLen_ = 0.0f;
    bool edgePathNotch_ = false, edgePathClosed_ = true;

    // A rounded rectangle from its top centre, clockwise, so twin comets set off
    // together from the top and meet at the bottom.
    ComPtr<ID2D1PathGeometry> RoundedOutline(D2D1_RECT_F rect, float radius) {
        ComPtr<ID2D1PathGeometry> g;
        if (FAILED(d2dFactory_->CreatePathGeometry(&g)) || !g) return nullptr;
        ComPtr<ID2D1GeometrySink> k;
        if (FAILED(g->Open(&k)) || !k) return nullptr;
        const float r = std::max(0.0f, std::min({radius, (rect.right - rect.left) * 0.5f, (rect.bottom - rect.top) * 0.5f}));
        const float cx = (rect.left + rect.right) * 0.5f;
        auto arc = [&](float x, float y) {
            if (r > 0.0f)
                k->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(r, r), 0.0f,
                                           D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        };
        k->BeginFigure(D2D1::Point2F(cx, rect.top), D2D1_FIGURE_BEGIN_HOLLOW);
        k->AddLine(D2D1::Point2F(rect.right - r, rect.top));
        arc(rect.right, rect.top + r);
        k->AddLine(D2D1::Point2F(rect.right, rect.bottom - r));
        arc(rect.right - r, rect.bottom);
        k->AddLine(D2D1::Point2F(rect.left + r, rect.bottom));
        arc(rect.left, rect.bottom - r);
        k->AddLine(D2D1::Point2F(rect.left, rect.top + r));
        arc(rect.left + r, rect.top);
        k->AddLine(D2D1::Point2F(cx, rect.top));
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
        k->Close();
        return g;
    }

    bool EdgeOutline(D2D1_RECT_F rect, float radius, bool notch) {
        const bool same = edgePath_ && notch == edgePathNotch_ && radius == edgePathRadius_ &&
                          rect.left == edgePathRect_.left && rect.top == edgePathRect_.top &&
                          rect.right == edgePathRect_.right && rect.bottom == edgePathRect_.bottom;
        if (same) return true;
        // A notch is open at the top (that edge is the screen's); it runs from one
        // ear's tip round to the other's.
        edgePath_ = notch ? CreateNotchGeometry(rect, radius, true) : RoundedOutline(rect, radius);
        edgePathLen_ = 0.0f;
        if (!edgePath_ || FAILED(edgePath_->ComputeLength(nullptr, &edgePathLen_))) {
            edgePath_.Reset();
            return false;
        }
        edgePathRect_ = rect;
        edgePathRadius_ = radius;
        edgePathNotch_ = notch;
        edgePathClosed_ = !notch;
        return true;
    }

    D2D1_POINT_2F EdgePoint(float d) {
        const float L = edgePathLen_;
        if (edgePathClosed_) d = std::fmod(std::fmod(d, L) + L, L);
        else d = std::clamp(d, 0.0f, L);
        D2D1_POINT_2F p{};
        edgePath_->ComputePointAtLength(d, nullptr, &p, nullptr);
        return p;
    }

    // One comet: a bright head at `s` with a tail fading out behind it (`dir` is
    // the way it is moving along the outline).
    void EdgeComet(float s, float dir, D2D1_COLOR_F col, float level) {
        const float L = edgePathLen_;
        const float tail = std::clamp(L * 0.2f, 36.0f, 200.0f);
        // On an open notch outline the light slips in and out at the ear tips.
        const float ends = edgePathClosed_ ? 1.0f : std::clamp(std::min(s, L - s) / 28.0f, 0.0f, 1.0f);
        const float a0 = level * ends;
        if (a0 <= 0.01f) return;
        constexpr int kSteps = 24;
        D2D1_POINT_2F prev = EdgePoint(s);
        const D2D1_POINT_2F head = prev;
        for (int i = 1; i <= kSteps; ++i) {
            const float d = s - dir * tail * static_cast<float>(i) / kSteps;
            if (!edgePathClosed_ && (d < 0.0f || d > L)) break;
            const D2D1_POINT_2F p = EdgePoint(d);
            const float f = 1.0f - static_cast<float>(i - 1) / kSteps;  // 1 at the head
            const float a = a0 * f * f;
            if (ID2D1SolidColorBrush* b = AcquireBrush(WithAlpha(col, a * 0.22f))) target_->DrawLine(prev, p, b, 5.0f);
            if (ID2D1SolidColorBrush* b = AcquireBrush(WithAlpha(col, a * 0.95f))) target_->DrawLine(prev, p, b, 1.6f);
            prev = p;
        }
        // A soft bloom round the head, then its bright point.
        if (ID2D1SolidColorBrush* b = AcquireBrush(WithAlpha(col, a0 * 0.10f))) target_->FillEllipse(D2D1::Ellipse(head, 4.5f, 4.5f), b);
        if (ID2D1SolidColorBrush* b = AcquireBrush(WithAlpha(col, a0))) target_->FillEllipse(D2D1::Ellipse(head, 1.5f, 1.5f), b);
    }

    void DrawEdgeLight(const Settings& settings, double now) {
        const bool valid = edgeValid_;
        edgeValid_ = false;  // DrawIslandSurface sets it again next frame
        if (!valid || g_tucked.load(std::memory_order_relaxed)) return;
        const float level = EdgeLight::Level(settings, now) * settingsOpacity_;
        if (level <= 0.01f) return;
        // Half a pixel in, so the line sits on the surface's edge, not beside it.
        const D2D1_RECT_F r = D2D1::RectF(edgeRect_.left + 0.75f, edgeRect_.top + 0.75f, edgeRect_.right - 0.75f, edgeRect_.bottom - 0.75f);
        if (!EdgeOutline(r, std::max(0.0f, edgeRadius_ - 0.75f), settings.notchStyle) || edgePathLen_ < 24.0f) return;

        // Default white; Dynamic follows the island's accent (album art or yours); Custom is the picked colour.
        D2D1_COLOR_F col = D2D1::ColorF(1, 1, 1, 1);
        if (settings.edgeLightColor == 1 && currentAccent_.a > 0.0f) col = currentAccent_;
        else if (settings.edgeLightColor == 2) col = settings.edgeLightCustom;
        col.a = 1.0f;
        const float L = edgePathLen_;
        const double t = EdgeLight::Clock(settings, now);
        constexpr double kPi = 3.14159265358979;

        if (settings.edgeLight == 3) {  // Breathe: the whole outline glows and fades
            const float b = static_cast<float>(0.5 - 0.5 * std::cos(t * 2.0 * kPi / 2.8));
            if (ID2D1SolidColorBrush* br = AcquireBrush(WithAlpha(col, level * (0.06f + 0.16f * b))))
                target_->DrawGeometry(edgePath_.Get(), br, 5.0f);
            if (ID2D1SolidColorBrush* br = AcquireBrush(WithAlpha(col, level * (0.18f + 0.55f * b))))
                target_->DrawGeometry(edgePath_.Get(), br, 1.3f);
            return;
        }

        // The comets ease along: out from the start, together at the far point, back.
        const double period = 3.6;
        const double u = std::fmod(t, period) / period;
        const double ping = u < 0.5 ? u * 2.0 : 2.0 - u * 2.0;           // 0 → 1 → 0
        const float eased = static_cast<float>(ping * ping * (3.0 - 2.0 * ping));
        const float dir = u < 0.5 ? 1.0f : -1.0f;
        if (settings.edgeLight == 2) {  // Twin comets: from both ends, meeting halfway
            const float s = eased * L * 0.5f;
            EdgeComet(s, dir, col, level);
            EdgeComet(L - s, -dir, col, level);
        } else if (edgePathClosed_) {   // Comet round a pill: laps the outline
            EdgeComet(static_cast<float>(std::fmod(t, period) / period) * L, 1.0f, col, level);
        } else {                        // Comet along a notch: ear to ear and back
            EdgeComet(eased * L, dir, col, level);
        }
    }
