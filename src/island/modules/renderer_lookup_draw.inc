// Bunbun: Renderer: Quick Lookup card and panel.
// Split out of island_core.cpp; included inside class Renderer at the
// original position, so declaration order is unchanged. Not a standalone file.

    // ── Quick Lookup card ────────────────────────────────────────────────────
   public:
    // Content-space height the Lookup card needs for the current result. Called
    // from the render loop (like MeasureIdleStrip) so the island animates to
    // exactly the size DrawLookup will paint.
    float MeasureLookupCard(const SharedState& state, const Settings& settings) {
        const LookupSnapshot& lk = state.lookup;
        if (lk.status == LookupStatus::Search) {
            const std::vector<LookupRow> rows = BuildLookupRows(state.lookupRecent, g_lookupUi);
            return LookupLayout::SearchHeight(static_cast<int>(rows.size()),
                                              LookupHasHeader(g_lookupUi, rows));
        }
        if (lk.status == LookupStatus::Found) {
            const float card = LookupCardHeight(lk, EnsureLookupFit(lk, settings), LookupGrow(settings));
            return Clamp(card + LookupLayout::kFieldH + LookupLayout::kFieldGap,
                         LookupLayout::kSimpleHeight, LookupLayout::kMaxHeight);
        }
        return LookupLayout::StatusHeight();
    }

   private:
    // Body / example text after being fitted to its line cap, plus measured heights.
    struct LookupFit {
        std::wstring key;
        std::wstring body;
        std::wstring example;
        float bodyH = 0.0f;
        float exampleH = 0.0f;
        bool valid = false;
    };

    // Title / subtitle rows grow with the Text size setting (never shrink).
    float LookupGrow(const Settings& settings) const {
        return std::max(1.0f, Clamp(settings.textScale, 0.7f, 1.6f));
    }

    float LookupCardHeight(const LookupSnapshot& lk, const LookupFit& fit, float grow) const {
        float inner = fit.bodyH;
        if (!fit.example.empty()) {
            inner += LookupLayout::kExampleGap + fit.exampleH;
        }
        const float h = LookupLayout::kPadTop + LookupLayout::kTitleH * grow +
                        (lk.subtitle.empty() ? 2.0f : LookupLayout::kSubtitleH * grow) +
                        LookupLayout::kCardGap + inner + LookupLayout::kCardPad * 2.0f +
                        LookupLayout::kPadBottom;
        return Clamp(h, LookupLayout::kSimpleHeight, LookupLayout::kMaxHeight);
    }

    void EnsureLookupFormats(const Settings& settings) {
        const float ts = Clamp(settings.textScale, 0.7f, 1.6f);
        if (lookupBodyFormat_ && lookupExampleFormat_ &&
            std::fabs(lookupFormatScale_ - ts) < 0.001f && lookupFormatFamily_ == settings.fontFamily) {
            return;
        }
        lookupBodyFormat_.Reset();
        lookupExampleFormat_.Reset();
        lookupFit_.valid = false;

        const wchar_t* family =
            settings.fontFamily.empty() ? L"Segoe UI Variable Small" : settings.fontFamily.c_str();
        auto make = [&](DWRITE_FONT_STYLE style, float size, ComPtr<IDWriteTextFormat>& out) {
            HRESULT hr = dwriteFactory_->CreateTextFormat(
                family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, style, DWRITE_FONT_STRETCH_NORMAL,
                size, L"", &out);
            if (FAILED(hr) || !out) {
                out.Reset();
                dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                 style, DWRITE_FONT_STRETCH_NORMAL, size, L"", &out);
            }
            if (out) {
                out->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }
        };
        make(DWRITE_FONT_STYLE_NORMAL, 12.0f * ts, lookupBodyFormat_);
        make(DWRITE_FONT_STYLE_ITALIC, 11.5f * ts, lookupExampleFormat_);
        lookupFormatScale_ = ts;
        lookupFormatFamily_ = settings.fontFamily;
    }

    // Wraps `text` to `width`; if it needs more than maxLines, cuts it at the end of
    // the last allowed line and ends it with an ellipsis. Reports the final height.
    std::wstring FitTextToLines(const std::wstring& text, IDWriteTextFormat* fmt, float width,
                                int maxLines, float* outHeight) {
        *outHeight = 0.0f;
        if (text.empty() || !fmt || !dwriteFactory_) {
            return text;
        }

        auto measure = [&](const std::wstring& s, UINT32* lines, float* height) -> bool {
            ComPtr<IDWriteTextLayout> layout;
            if (FAILED(dwriteFactory_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()),
                                                        fmt, width, 4096.0f, &layout)) || !layout) {
                return false;
            }
            DWRITE_TEXT_METRICS tm = {};
            if (FAILED(layout->GetMetrics(&tm))) {
                return false;
            }
            *lines = tm.lineCount;
            *height = tm.height;
            return true;
        };

        UINT32 lineCount = 0;
        float height = 0.0f;
        if (!measure(text, &lineCount, &height)) {
            return text;
        }
        if (static_cast<int>(lineCount) <= maxLines) {
            *outHeight = height;
            return text;
        }
        const float oneLine = lineCount > 0 ? height / static_cast<float>(lineCount) : 0.0f;

        // Where does the last allowed line end?
        size_t keep = text.size();
        {
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                                           fmt, width, 4096.0f, &layout)) && layout) {
                UINT32 n = 0;
                layout->GetLineMetrics(nullptr, 0, &n);
                std::vector<DWRITE_LINE_METRICS> lines(n);
                if (n > 0 && SUCCEEDED(layout->GetLineMetrics(lines.data(), n, &n))) {
                    size_t total = 0;
                    for (UINT32 i = 0; i < n && i < static_cast<UINT32>(maxLines); ++i) {
                        total += lines[i].length;
                    }
                    keep = std::min(total, text.size());
                }
            }
        }

        std::wstring cut = text.substr(0, keep);
        for (int attempt = 0; attempt < 60 && !cut.empty(); ++attempt) {
            while (!cut.empty() && (iswspace(cut.back()) || cut.back() == L',' || cut.back() == L';')) {
                cut.pop_back();
            }
            const std::wstring candidate = cut + L"\u2026";
            UINT32 lines = 0;
            float h = 0.0f;
            if (measure(candidate, &lines, &h) && static_cast<int>(lines) <= maxLines) {
                *outHeight = h;
                return candidate;
            }
            if (cut.empty()) {
                break;
            }
            cut.pop_back();
            if (!cut.empty() && IS_HIGH_SURROGATE(cut.back())) {
                cut.pop_back();
            }
        }
        *outHeight = oneLine;
        return L"\u2026";
    }

    // Cached: the text only changes when a new result arrives, so the layouts are
    // built once per result instead of once per frame.
    const LookupFit& EnsureLookupFit(const LookupSnapshot& lk, const Settings& settings) {
        EnsureLookupFormats(settings);
        const std::wstring key = lk.source + L"\x1F" + lk.body + L"\x1F" + lk.example;
        if (lookupFit_.valid && lookupFit_.key == key) {
            return lookupFit_;
        }
        const float textW = LookupLayout::kWidth - LookupLayout::kPadX * 2.0f -
                            LookupLayout::kCardPad * 2.0f;
        LookupFit fit;
        fit.key = key;
        // Bunbun: Bun's answers get more room than a dictionary definition.
        const int bodyLines = lk.source == L"Bun" ? 9 : LookupLayout::kMaxBodyLines;
        fit.body = FitTextToLines(lk.body, lookupBodyFormat_.Get(), textW, bodyLines, &fit.bodyH);
        fit.example = FitTextToLines(lk.example, lookupExampleFormat_.Get(), textW,
                                     LookupLayout::kMaxExampleLines, &fit.exampleH);
        fit.valid = true;
        lookupFit_ = std::move(fit);
        return lookupFit_;
    }

    // ── Quick Lookup panel ───────────────────────────────────────────────────
    ComPtr<IDWriteTextLayout> MakeLineLayout(const std::wstring& text, IDWriteTextFormat* fmt,
                                             float maxWidth, DWRITE_TEXT_METRICS* metrics,
                                             bool ellipsis = false) {
        ComPtr<IDWriteTextLayout> layout;
        if (!fmt || !dwriteFactory_) return layout;
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                                    fmt, std::max(1.0f, maxWidth), 200.0f,
                                                    &layout)) || !layout) {
            layout.Reset();
            return layout;
        }
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (ellipsis) {
            ComPtr<IDWriteInlineObject> sign;
            if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(layout.Get(), &sign)) && sign) {
                DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                layout->SetTrimming(&trimming, sign.Get());
            }
        }
        if (metrics) layout->GetMetrics(metrics);
        return layout;
    }

    ID2D1StrokeStyle* RoundStroke() {
        if (!roundStroke_ && d2dFactory_) {
            d2dFactory_->CreateStrokeStyle(
                D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                nullptr, 0, &roundStroke_);
        }
        return roundStroke_.Get();
    }

    // kind: 0 magnifier, 1 history clock, 2 clipboard, 3 return arrow.
    void DrawLookupGlyph(D2D1_POINT_2F c, float s, int kind, ID2D1Brush* brush) {
        ID2D1StrokeStyle* round = RoundStroke();
        const float stroke = std::max(1.3f, 0.10f * s);
        auto P = [](float x, float y) { return D2D1::Point2F(x, y); };

        switch (kind) {
            case 0: {
                const float r = 0.30f * s;
                const D2D1_POINT_2F lens = P(c.x - 0.08f * s, c.y - 0.08f * s);
                target_->DrawEllipse(D2D1::Ellipse(lens, r, r), brush, stroke, round);
                target_->DrawLine(P(lens.x + r * 0.72f, lens.y + r * 0.72f),
                                  P(c.x + 0.40f * s, c.y + 0.40f * s), brush, stroke, round);
                break;
            }
            case 1: {
                const float r = 0.40f * s;
                target_->DrawEllipse(D2D1::Ellipse(c, r, r), brush, stroke, round);
                target_->DrawLine(P(c.x, c.y - 0.22f * s), c, brush, stroke, round);
                target_->DrawLine(c, P(c.x + 0.17f * s, c.y + 0.10f * s), brush, stroke, round);
                break;
            }
            case 2: {
                target_->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(c.x - 0.30f * s, c.y - 0.28f * s,
                                                  c.x + 0.30f * s, c.y + 0.40f * s),
                                      0.08f * s, 0.08f * s),
                    brush, stroke, round);
                target_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(c.x - 0.13f * s, c.y - 0.40f * s,
                                                  c.x + 0.13f * s, c.y - 0.20f * s),
                                      0.05f * s, 0.05f * s),
                    brush);
                target_->DrawLine(P(c.x - 0.14f * s, c.y + 0.06f * s), P(c.x + 0.14f * s, c.y + 0.06f * s), brush, stroke * 0.85f, round);
                target_->DrawLine(P(c.x - 0.14f * s, c.y + 0.22f * s), P(c.x + 0.05f * s, c.y + 0.22f * s), brush, stroke * 0.85f, round);
                break;
            }
            default: {  // return arrow
                const D2D1_POINT_2F tip = P(c.x - 0.36f * s, c.y + 0.08f * s);
                target_->DrawLine(P(c.x + 0.34f * s, c.y - 0.28f * s), P(c.x + 0.34f * s, c.y + 0.08f * s), brush, stroke, round);
                target_->DrawLine(P(c.x + 0.34f * s, c.y + 0.08f * s), tip, brush, stroke, round);
                target_->DrawLine(tip, P(c.x - 0.14f * s, c.y - 0.12f * s), brush, stroke, round);
                target_->DrawLine(tip, P(c.x - 0.14f * s, c.y + 0.28f * s), brush, stroke, round);
                break;
            }
        }
    }

    // The search box: accent magnifier, text with a real caret, key hint on the right.
    // Bunbun: in Ask Bun the mascot sits in the box instead of the magnifier. It
    // watches what you type, thinks while the local AI writes, and cheers at a reply.
    void DrawAskBunMascot(D2D1_RECT_F box, const LookupSnapshot& lk, float lookX, double now) {
        static uint64_t s_seq = 0;
        static double s_foundAt = -10.0;
        static bb::mascot::Mood s_mood = bb::mascot::Mood::Idle;
        static double s_moodSince = 0.0;
        const uint64_t seq = g_lookupRequestSeq.load();
        const bool bun = lk.source == L"Bun";
        static bool s_wasBusy = false;
        const bool busy = lk.talking || BrainBusyForCurrentCard();
        if (lk.status == LookupStatus::Found && bun && (seq != s_seq || (s_wasBusy && !busy))) {
            s_seq = seq;
            s_foundAt = now;  // a new answer, or the local AI just finished one
        }
        s_wasBusy = busy;
        // Typing: listening. Local AI writing: talking (thinking until the first word).
        // An answer: a short cheer; no answer: puzzled for a few seconds.
        bb::mascot::Mood mood = bb::mascot::Mood::Idle;
        if (lk.talking) mood = bb::mascot::Mood::Talking;
        else if (BrainBusyForCurrentCard() || (lk.status == LookupStatus::Loading && bun)) mood = bb::mascot::Mood::Thinking;
        else if (!g_lookupUi.text.empty()) mood = bb::mascot::Mood::Listening;
        else if (lk.status == LookupStatus::Found && bun && lk.confused && now - s_foundAt < 3.5) mood = bb::mascot::Mood::Confused;
        else if (lk.status == LookupStatus::Found && bun && now - s_foundAt < 2.0) mood = bb::mascot::Mood::Happy;
        if (mood != s_mood) {
            s_mood = mood;
            s_moodSince = now;
        }
        D2D1_POINT_2F c{};
        float size = 0.0f;
        bb::mascot::FitToBox(box, &c, &size);
        // Eyes follow the caret while typing, the pointer otherwise.
        const D2D1_POINT_2F look = lookX >= 0.0f ? D2D1::Point2F(Clamp(lookX, 0.0f, 1.0f), 0.35f) : MascotLook(c);
        bb::mascot::Draw(target_.Get(), c, size, mood, now, s_moodSince, look);
    }

    void DrawLookupField(D2D1_RECT_F rect, const LookupSnapshot& lk, double now) {
        const LookupUiState& ui = g_lookupUi;
        const D2D1_RECT_F field = D2D1::RectF(
            rect.left + LookupLayout::kPadX, rect.top + LookupLayout::kFieldTop,
            rect.right - LookupLayout::kPadX,
            rect.top + LookupLayout::kFieldTop + LookupLayout::kFieldH);
        DrawCard(field, 12.0f, true);
        const float midY = (field.top + field.bottom) * 0.5f;

        const bool mascot = g_settings.bunbunAssistant && g_settings.bunbunMascot;  // Bunbun
        if (!mascot) {
            accentBrush_->SetOpacity(0.95f);
            DrawLookupGlyph(D2D1::Point2F(field.left + 19.0f, midY), 16.0f, 0, accentBrush_.Get());
            accentBrush_->SetOpacity(1.0f);
        }

        // Key hint chip: "Esc" while empty, the return arrow once there is text.
        const D2D1_RECT_F chip = D2D1::RectF(field.right - 44.0f, midY - 10.0f,
                                             field.right - 12.0f, midY + 10.0f);
        ComPtr<ID2D1SolidColorBrush> chipFill;
        if (SUCCEEDED(target_->CreateSolidColorBrush(material_.hairline, &chipFill)) && chipFill) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(chip, 6.0f, 6.0f), chipFill.Get());
        }
        mutedBrush_->SetOpacity(0.75f);
        if (ui.text.empty()) {
            if (smallTextFormat_) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                target_->DrawTextW(L"Esc", 3, smallTextFormat_.Get(), chip, mutedBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }
        } else {
            DrawLookupGlyph(D2D1::Point2F((chip.left + chip.right) * 0.5f, midY), 12.0f, 3,
                            mutedBrush_.Get());
        }

        // Bunbun: room for the mascot, and for a skin pack's art that reaches past its square.
        float reachL = 0.5f, reachR = 0.5f;
        if (mascot) bb::mascot::HorizontalReach(&reachL, &reachR);
        const float bunShift = std::max(0.0f, (reachL - 0.5f) * 30.0f);
        const float areaLeft = field.left + (mascot ? 42.0f + bunShift + std::max(0.0f, (reachR - 0.5f) * 30.0f) : 38.0f);
        const float areaRight = chip.left - 10.0f;
        const float areaW = std::max(8.0f, areaRight - areaLeft);
        float caretX = 0.0f;
        float scroll = 0.0f;

        target_->PushAxisAlignedClip(D2D1::RectF(areaLeft, field.top, areaRight, field.bottom),
                                     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        DWRITE_TEXT_METRICS tm{};
        if (ui.text.empty()) {
            auto hint = MakeLineLayout(g_settings.bunbunAssistant ? std::wstring(L"Ask Bun anything · define a word")
                                                                  : std::wstring(Loc(L"Search a word or phrase")),
                                       textFormat_.Get(), 4096.0f, &tm);
            if (hint) {
                mutedBrush_->SetOpacity(0.50f);
                target_->DrawTextLayout(D2D1::Point2F(areaLeft + 6.0f, midY - tm.height * 0.5f),
                                        hint.Get(), mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        } else if (auto layout = MakeLineLayout(ui.text, textFormat_.Get(), 4096.0f, &tm)) {
            const UINT32 caretIdx = static_cast<UINT32>(std::min(ui.caret, ui.text.size()));
            float caretY = 0.0f;
            DWRITE_HIT_TEST_METRICS hm{};
            layout->HitTestTextPosition(caretIdx, FALSE, &caretX, &caretY, &hm);
            if (caretX > areaW - 6.0f) scroll = caretX - (areaW - 6.0f);  // keep the caret in view

            const float ox = areaLeft - scroll;
            if (ui.selectAll) {
                ComPtr<ID2D1SolidColorBrush> sel;
                if (SUCCEEDED(target_->CreateSolidColorBrush(WithAlpha(material_.accent, 0.32f), &sel)) && sel) {
                    target_->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(ox - 2.0f, midY - 10.0f,
                                                      ox + tm.widthIncludingTrailingWhitespace + 2.0f,
                                                      midY + 10.0f), 4.0f, 4.0f),
                        sel.Get());
                }
            }
            textBrush_->SetOpacity(0.98f);
            target_->DrawTextLayout(D2D1::Point2F(ox, midY - tm.height * 0.5f), layout.Get(),
                                    textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        if (!ui.selectAll && LookupCaretVisible(now, ui.caretResetAt)) {
            const float cx = areaLeft + 1.0f + caretX - scroll;
            accentBrush_->SetOpacity(0.95f);
            target_->DrawLine(D2D1::Point2F(cx, midY - 9.0f), D2D1::Point2F(cx, midY + 9.0f),
                              accentBrush_.Get(), 1.6f, RoundStroke());
            accentBrush_->SetOpacity(1.0f);
        }
        target_->PopAxisAlignedClip();
        mutedBrush_->SetOpacity(0.75f);
        if (mascot) {
            const float lookX = ui.text.empty() ? -1.0f : (caretX - scroll) / areaW;
            DrawAskBunMascot(D2D1::RectF(field.left + 6.0f + bunShift, field.top + 3.0f, field.left + 36.0f + bunShift, field.bottom - 3.0f),
                             lk, lookX, now);
        }
    }

    // Clipboard suggestion + recents (box empty) or Look up + matching recents (typing).
    void DrawLookupRows(D2D1_RECT_F rect, const std::vector<LookupRow>& rows, bool header) {
        const LookupUiState& ui = g_lookupUi;
        const float left = rect.left + LookupLayout::kPadX;
        const float right = rect.right - LookupLayout::kPadX;
        float y = rect.top + LookupLayout::kBelowField;

        if (rows.empty()) {
            DWRITE_TEXT_METRICS tm{};
            auto hint = MakeLineLayout(Loc(L"Type a word or phrase, then press Enter"),
                                       smallTextFormat_.Get(), right - left - 8.0f, &tm, true);
            if (hint) {
                mutedBrush_->SetOpacity(0.60f);
                target_->DrawTextLayout(D2D1::Point2F(left + 4.0f, y + (LookupLayout::kStatusH - tm.height) * 0.5f),
                                        hint.Get(), mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
            mutedBrush_->SetOpacity(0.75f);
            return;
        }

        if (header && smallTextFormat_) {
            const D2D1_RECT_F band = D2D1::RectF(left + 4.0f, y, right - 4.0f, y + LookupLayout::kSectionH);
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            mutedBrush_->SetOpacity(0.55f);
            const wchar_t* title = Loc(L"Recent");
            target_->DrawTextW(title, static_cast<UINT32>(wcslen(title)), smallTextFormat_.Get(), band,
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (LookupHasRecentRows(rows)) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                const wchar_t* clear = Loc(L"Clear");
                target_->DrawTextW(clear, static_cast<UINT32>(wcslen(clear)), smallTextFormat_.Get(), band,
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            y += LookupLayout::kSectionH;
        }

        for (size_t i = 0; i < rows.size(); ++i) {
            const LookupRow& row = rows[i];
            const D2D1_RECT_F rr = D2D1::RectF(left, y, right, y + LookupLayout::kRowH);
            const bool selected = static_cast<int>(i) == ui.selected;
            const float midY = y + LookupLayout::kRowH * 0.5f;
            if (selected) {
                DrawCard(rr, 10.0f, false);
            }

            const int glyph = row.kind == LookupRowKind::Typed ? 0
                              : row.kind == LookupRowKind::Clipboard ? 2 : 1;
            ID2D1SolidColorBrush* glyphBrush = (row.kind == LookupRowKind::Recent) ? mutedBrush_.Get()
                                                                                    : accentBrush_.Get();
            glyphBrush->SetOpacity(row.kind == LookupRowKind::Recent ? 0.70f : 0.92f);
            DrawLookupGlyph(D2D1::Point2F(rr.left + 17.0f, midY), 14.0f, glyph, glyphBrush);
            glyphBrush->SetOpacity(row.kind == LookupRowKind::Recent ? 0.75f : 1.0f);

            const float x0 = rr.left + 34.0f;
            const float x1 = rr.right - (selected ? 34.0f : 10.0f);
            const float areaW = std::max(10.0f, x1 - x0);

            const std::wstring label = row.kind == LookupRowKind::Typed
                ? std::wstring(Loc(L"Look up")) + L" \u201c" + row.label + L"\u201d"
                : row.label;

            DWRITE_TEXT_METRICS lm{};
            MakeLineLayout(label, textFormat_.Get(), 4096.0f, &lm);  // natural width
            const float labelW = std::min(lm.widthIncludingTrailingWhitespace,
                                          areaW * (row.detail.empty() ? 1.0f : 0.62f));
            if (auto labelLayout = MakeLineLayout(label, textFormat_.Get(), labelW + 0.5f, &lm, true)) {
                textBrush_->SetOpacity(0.96f);
                target_->DrawTextLayout(D2D1::Point2F(x0, midY - lm.height * 0.5f), labelLayout.Get(),
                                        textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            }

            if (!row.detail.empty()) {
                const float dx = x0 + labelW + 10.0f;
                const float dw = x1 - dx;
                if (dw > 40.0f) {
                    DWRITE_TEXT_METRICS dm{};
                    if (auto detail = MakeLineLayout(row.detail, smallTextFormat_.Get(), dw, &dm, true)) {
                        ID2D1SolidColorBrush* b = (row.kind == LookupRowKind::Clipboard) ? accentBrush_.Get()
                                                                                          : mutedBrush_.Get();
                        b->SetOpacity(row.kind == LookupRowKind::Clipboard ? 0.85f : 0.62f);
                        target_->DrawTextLayout(D2D1::Point2F(dx, midY - dm.height * 0.5f), detail.Get(), b,
                                                D2D1_DRAW_TEXT_OPTIONS_NONE);
                        b->SetOpacity(b == accentBrush_.Get() ? 1.0f : 0.75f);
                    }
                }
            }

            if (selected) {
                mutedBrush_->SetOpacity(0.80f);
                DrawLookupGlyph(D2D1::Point2F(rr.right - 17.0f, midY), 12.0f, 3, mutedBrush_.Get());
                mutedBrush_->SetOpacity(0.75f);
            }
            y += LookupLayout::kRowH + LookupLayout::kRowGap;
        }
        textBrush_->SetOpacity(0.96f);
    }

    // "Looking up..." / "No result found" under the box.
    void DrawLookupStatus(D2D1_RECT_F rect, const LookupSnapshot& lk) {
        const float left = rect.left + LookupLayout::kPadX + 4.0f;
        const float right = rect.right - LookupLayout::kPadX - 4.0f;
        const float y = rect.top + LookupLayout::kBelowField;
        const bool bun = lk.source == L"Bun";
        const wchar_t* msg = lk.status == LookupStatus::Loading
                                 ? (bun ? L"Bun is thinking…" : Loc(L"Looking up..."))
                                 : Loc(L"No result found");
        // Bunbun: Bun thinks in the box above (DrawAskBunMascot), not beside this line.
        const float textLeft = left;
        DWRITE_TEXT_METRICS tm{};
        if (auto layout = MakeLineLayout(msg, textFormat_.Get(), right - textLeft, &tm, true)) {
            mutedBrush_->SetOpacity(0.75f);
            target_->DrawTextLayout(D2D1::Point2F(textLeft, y + (LookupLayout::kStatusH - tm.height) * 0.5f),
                                    layout.Get(), mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }

    // The definition / summary card under the box (the old "Found" layout, shifted down).
    void DrawLookupResult(const SharedState& state, D2D1_RECT_F rect, const Settings& settings) {
        const LookupSnapshot& lk = state.lookup;
        const LookupFit& fit = EnsureLookupFit(lk, settings);
        const float grow = LookupGrow(settings);
        const float titleH = LookupLayout::kTitleH * grow;
        const float left = rect.left + LookupLayout::kPadX;
        const float right = rect.right - LookupLayout::kPadX;
        float y = rect.top + LookupLayout::kBelowField;

        constexpr float kTagW = 74.0f;
        textBrush_->SetOpacity(0.97f);
        target_->DrawTextW(lk.title.c_str(), static_cast<UINT32>(lk.title.size()),
                           textFormat_.Get(), D2D1::RectF(left, y, right - kTagW - 6.0f, y + titleH),
                           textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (!lk.source.empty() && smallTextFormat_) {
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            accentBrush_->SetOpacity(0.90f);
            target_->DrawTextW(lk.source.c_str(), static_cast<UINT32>(lk.source.size()),
                               smallTextFormat_.Get(),
                               D2D1::RectF(right - kTagW, y + 3.0f, right, y + titleH),
                               accentBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            accentBrush_->SetOpacity(1.0f);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        y += titleH;

        if (!lk.subtitle.empty()) {
            const float subH = LookupLayout::kSubtitleH * grow;
            mutedBrush_->SetOpacity(0.78f);
            target_->DrawTextW(lk.subtitle.c_str(), static_cast<UINT32>(lk.subtitle.size()),
                               smallTextFormat_.Get(), D2D1::RectF(left, y - 2.0f, right, y + subH - 2.0f),
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            y += subH;
        } else {
            y += 2.0f;
        }
        y += LookupLayout::kCardGap;

        float inner = fit.bodyH;
        if (!fit.example.empty()) {
            inner += LookupLayout::kExampleGap + fit.exampleH;
        }
        const float pad = LookupLayout::kCardPad;
        const D2D1_RECT_F card = D2D1::RectF(left, y, right, y + inner + pad * 2.0f);
        DrawCard(card, 10.0f);

        float ty = card.top + pad;
        textBrush_->SetOpacity(0.92f);
        target_->DrawTextW(fit.body.c_str(), static_cast<UINT32>(fit.body.size()),
                           lookupBodyFormat_.Get(),
                           D2D1::RectF(card.left + pad, ty, card.right - pad, ty + fit.bodyH + 2.0f),
                           textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        ty += fit.bodyH;

        if (!fit.example.empty()) {
            ty += LookupLayout::kExampleGap;
            mutedBrush_->SetOpacity(0.70f);
            target_->DrawTextW(fit.example.c_str(), static_cast<UINT32>(fit.example.size()),
                               lookupExampleFormat_.Get(),
                               D2D1::RectF(card.left + pad, ty, card.right - pad, ty + fit.exampleH + 2.0f),
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    void DrawLookup(const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now) {
        const LookupSnapshot& lk = state.lookup;
        const float h = rect.bottom - rect.top;
        const float w = rect.right - rect.left;
        if (h < 24.0f || w < 100.0f) return;

        // Hit tests (clicks, hover, cursor) map through this rect.
        PublishContentGeometry(rect);

        const float need = MeasureLookupCard(state, settings);
        const float alpha = SmoothStep01((h - need * 0.55f) / (need * 0.45f));
        if (alpha <= 0.01f) return;  // island still too small; content fades in as it opens

        ComPtr<ID2D1Geometry> mask =
            CreateIslandMaskGeometry(rect, ContentIslandRadius(h), settings.notchStyle);
        ID2D1Layer* layer = AcquireLayer();
        const bool masked = mask && layer;
        if (masked) {
            target_->PushLayer(D2D1::LayerParameters(rect, mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                      D2D1::IdentityMatrix(), alpha, nullptr,
                                                      D2D1_LAYER_OPTIONS_NONE),
                               layer);
        } else {
            target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }

        DrawLookupField(rect, lk, now);  // Bunbun: lk for the mascot's mood

        if (lk.status == LookupStatus::Search) {
            const std::vector<LookupRow> rows = BuildLookupRows(state.lookupRecent, g_lookupUi);
            DrawLookupRows(rect, rows, LookupHasHeader(g_lookupUi, rows));
        } else if (lk.status == LookupStatus::Found) {
            DrawLookupResult(state, rect, settings);
        } else {
            DrawLookupStatus(rect, lk);
        }

        if (masked) {
            target_->PopLayer();
        } else {
            target_->PopAxisAlignedClip();
        }
        textBrush_->SetOpacity(0.96f);
        mutedBrush_->SetOpacity(0.75f);
    }

    ComPtr<ID2D1StrokeStyle> roundStroke_;
    ComPtr<IDWriteTextFormat> lookupBodyFormat_;
    ComPtr<IDWriteTextFormat> lookupExampleFormat_;
    float lookupFormatScale_ = -1.0f;
    std::wstring lookupFormatFamily_;
    LookupFit lookupFit_;

    void DrawBattery(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 140.0f) return;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14, cy - badgeSz * 0.5f,
                                        rect.left + 14 + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        // Bunbun: the charging reminder and "fully charged" pills show the mascot
        // (holding its plug, or cheering) instead of the battery icon.
        if (g_settings.bunbunMascot && g_settings.batteryMoods && (state.battery.reminder || state.battery.full)) {
            D2D1_POINT_2F mc{};
            float msize = 0.0f;
            bb::mascot::FitToBox(D2D1::RectF(badge.left + 2.0f, badge.top + 2.0f, badge.right - 2.0f, badge.bottom - 2.0f), &mc, &msize);
            const double t = NowSeconds();
            bb::mascot::Draw(target_.Get(), mc, msize, state.battery.full ? bb::mascot::Mood::Happy : bb::mascot::Mood::Idle,
                             t, g_batteryCheerAt.load(std::memory_order_relaxed), MascotLook(mc));
        } else {
            // Draw battery vector icon
            const float bx = badge.left + badgeSz * 0.25f;
            const float by = cy - badgeSz * 0.22f;
            const float bw = badgeSz * 0.45f;
            const float bh = badgeSz * 0.44f;
            D2D1_RECT_F batRect = D2D1::RectF(bx, by, bx + bw, by + bh);

            ComPtr<ID2D1SolidColorBrush> batBorder;
            target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.85f), &batBorder);
            target_->DrawRoundedRectangle(D2D1::RoundedRect(batRect, 2, 2), batBorder.Get(), 1.5f);

            // Battery Terminal (Nub)
            D2D1_RECT_F nubRect = D2D1::RectF(batRect.right, cy - 3, batRect.right + 2.5f, cy + 3);
            target_->FillRectangle(nubRect, batBorder.Get());

            // Battery Fill
            const float pct = Clamp(state.battery.percent / 100.0f, 0.0f, 1.0f);
            D2D1_RECT_F fillRect = D2D1::RectF(batRect.left + 2, batRect.top + 2,
                                               batRect.left + 2 + (bw - 4) * pct, batRect.bottom - 2);

            ComPtr<ID2D1SolidColorBrush> batFill;
            if (state.battery.low) {
                target_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.23f, 0.18f, 1.0f), &batFill); // Red
            } else if (state.battery.charging) {
                target_->CreateSolidColorBrush(D2D1::ColorF(0.19f, 0.83f, 0.38f, 1.0f), &batFill); // Green
            } else {
                target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &batFill); // White
            }
            target_->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 1, 1), batFill.Get());
        }

        // Text Labels
        const float tx = badge.right + 14;
        D2D1_RECT_F labelRect = D2D1::RectF(tx, cy - 16.0f, rect.right - 14.0f, cy - 1.0f);
        mutedBrush_->SetOpacity(0.50f);
        std::wstring label = state.battery.full ? Loc(L"Fully charged")
                           : state.battery.reminder ? Loc(L"Charging")
                           : state.battery.charging ? L"Power Connected" : L"Battery Alert";
        target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                           smallTextFormat_.Get(), labelRect, mutedBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        wchar_t value[128] = {};
        if (state.battery.secondsRemaining != BATTERY_LIFE_UNKNOWN && !state.battery.charging) {
            const DWORD minutes = state.battery.secondsRemaining / 60;
            swprintf_s(value, ARRAYSIZE(value), L"%d%% \u2022 %luh %02lum left",
                       state.battery.percent, minutes / 60, minutes % 60);
        } else if (state.battery.reallyCharging && state.battery.minutesToFull > 0) {
            // Bunbun: "64% · full in 1h 10m"
            swprintf_s(value, ARRAYSIZE(value), L"%d%% \u2022 %s %dh %02dm", state.battery.percent, Loc(L"full in"),
                       state.battery.minutesToFull / 60, state.battery.minutesToFull % 60);
        } else {
            swprintf_s(value, ARRAYSIZE(value), L"%d%%", state.battery.percent);
        }

        D2D1_RECT_F valueRect = D2D1::RectF(tx, cy - 1.0f, rect.right - 14.0f, cy + 16.0f);
        textBrush_->SetOpacity(0.95f);
        target_->DrawTextW(value, static_cast<UINT32>(wcslen(value)), textFormat_.Get(),
                           valueRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        textBrush_->SetOpacity(0.90f);
    }

    void DrawProgress(const SharedState& state, D2D1_RECT_F rect) {
        wchar_t buffer[64] = {};
        swprintf_s(buffer, L"Progress %d%%", state.progress.percent);
        IDWriteTextFormat* fmt = idleTextFormat_ ? idleTextFormat_.Get() : textFormat_.Get();
        target_->DrawTextW(buffer, static_cast<UINT32>(wcslen(buffer)), fmt,
                           rect,
                           textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void DrawProgressRing(D2D1_RECT_F rect, int percent) {
        ComPtr<ID2D1PathGeometry> geometry;
        d2dFactory_->CreatePathGeometry(&geometry);
        ComPtr<ID2D1GeometrySink> sink;
        geometry->Open(&sink);

        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float rx = (rect.right - rect.left) * 0.5f + 6.0f;
        const float ry = (rect.bottom - rect.top) * 0.5f + 6.0f;
        const float start = -3.14159265f * 0.5f;
        const float sweep = 2.0f * 3.14159265f * Clamp(percent / 100.0f, 0.0f, 1.0f);
        const int segments = std::max(2, static_cast<int>(48 * percent / 100.0f));

        auto pointAt = [&](float a) {
            return D2D1::Point2F(cx + std::cos(a) * rx, cy + std::sin(a) * ry);
        };

        sink->BeginFigure(pointAt(start), D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 1; i <= segments; ++i) {
            const float a = start + sweep * i / segments;
            sink->AddLine(pointAt(a));
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();

        accentBrush_->SetOpacity(0.92f);
        target_->DrawGeometry(geometry.Get(), accentBrush_.Get(), 3.0f);
        accentBrush_->SetOpacity(1.0f);
    }

