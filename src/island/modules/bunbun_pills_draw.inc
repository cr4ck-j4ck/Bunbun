// Bunbun: drawing for the meeting and download pills, and the calendar tab's
// "Up next" panel. Included inside the Renderer class body (member functions).

    // A rounded badge on the left of a pill, like the battery and timer pills.
    D2D1_RECT_F DrawPillBadge(D2D1_RECT_F rect) {
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float sz = (rect.bottom - rect.top) - 16.0f;
        const D2D1_RECT_F badge = D2D1::RectF(rect.left + 14.0f, cy - sz * 0.5f, rect.left + 14.0f + sz, cy + sz * 0.5f);
        if (ID2D1SolidColorBrush* bg = AcquireBrush(material_.raisedStrong)) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(badge, sz * 0.35f, sz * 0.35f), bg);
        }
        return badge;
    }

    // Small calendar page: body, header band and two rings.
    void DrawCalendarGlyph(D2D1_POINT_2F c, float s, ID2D1Brush* brush) {
        const float stroke = std::max(1.1f, 0.10f * s);
        const D2D1_RECT_F body = D2D1::RectF(c.x - 0.38f * s, c.y - 0.30f * s, c.x + 0.38f * s, c.y + 0.36f * s);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(body, 0.08f * s, 0.08f * s), brush, stroke);
        target_->FillRectangle(D2D1::RectF(body.left, body.top, body.right, body.top + 0.18f * s), brush);
        for (int i = -1; i <= 1; i += 2)
            target_->DrawLine(D2D1::Point2F(c.x + i * 0.18f * s, c.y - 0.42f * s), D2D1::Point2F(c.x + i * 0.18f * s, c.y - 0.24f * s), brush, stroke);
        for (int i = 0; i < 3; ++i)
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + (i - 1) * 0.2f * s, c.y + 0.12f * s), 0.05f * s, 0.05f * s), brush);
    }

    // Two text lines beside a badge: a muted label and the main value.
    void DrawPillLines(D2D1_RECT_F rect, float textLeft, float textRight, const std::wstring& label, const std::wstring& value) {
        const float cy = (rect.top + rect.bottom) * 0.5f;
        if (smallTextFormat_) {
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            mutedBrush_->SetOpacity(0.55f);
            target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), smallTextFormat_.Get(),
                               D2D1::RectF(textLeft, cy - 16.0f, textRight, cy - 1.0f), mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            textBrush_->SetOpacity(0.95f);
            target_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), textFormat_.Get(),
                               D2D1::RectF(textLeft, cy - 1.0f, textRight, cy + 16.0f), textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
        mutedBrush_->SetOpacity(0.75f);
        textBrush_->SetOpacity(0.90f);
    }

    // An accent "Join" button on the right; returns its left edge.
    float DrawJoinButton(D2D1_RECT_F rect, const wchar_t* text) {
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float w = 54.0f, h = 26.0f;
        const D2D1_RECT_F b = D2D1::RectF(rect.right - 14.0f - w, cy - h * 0.5f, rect.right - 14.0f, cy + h * 0.5f);
        if (ID2D1SolidColorBrush* fill = AcquireBrush(WithAlpha(material_.accent, 0.95f * settingsOpacity_))) {
            target_->FillRoundedRectangle(D2D1::RoundedRect(b, h * 0.5f, h * 0.5f), fill);
        }
        if (smallTextFormat_) {
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (ID2D1SolidColorBrush* ink = AcquireBrush(D2D1::ColorF(0.07f, 0.06f, 0.10f, settingsOpacity_))) {
                target_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), smallTextFormat_.Get(), b, ink, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        return b.left;
    }

    // ── Privacy pill: a coloured dot and "Discord is using your microphone" ───
    void DrawPrivacyPill(const SharedState& state, const Settings& settings, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 20.0f || rect.right - rect.left < 120.0f) return;
        const PrivacyAlertSnapshot& a = state.privacyAlert;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        D2D1_COLOR_F c = a.kind == 1 ? settings.privacyDotsCamHex : a.kind == 2 ? settings.privacyDotsScreenHex : settings.privacyDotsMicHex;
        c.a = settingsOpacity_;
        if (ID2D1SolidColorBrush* glow = AcquireBrush(WithAlpha(c, 0.25f * settingsOpacity_))) {
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + 22.0f, cy), 9.0f, 9.0f), glow);
        }
        if (ID2D1SolidColorBrush* dot = AcquireBrush(c)) {
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + 22.0f, cy), 5.0f, 5.0f), dot);
        }
        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            textBrush_->SetOpacity(0.95f);
            target_->DrawTextW(a.text.c_str(), static_cast<UINT32>(a.text.size()), textFormat_.Get(),
                               D2D1::RectF(rect.left + 38.0f, rect.top, rect.right - 14.0f, rect.bottom), textBrush_.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    // ── Meeting pill: "in 4:12 · Standup" with a Join button ─────────────────
    void DrawMeetingPill(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 160.0f) return;
        const MeetingSnapshot& m = state.meeting;
        const D2D1_RECT_F badge = DrawPillBadge(rect);
        const double now = EpochNow();
        const bool soon = m.startsAt - now < 60.0;
        const D2D1_COLOR_F tint = soon ? material_.accent : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f);
        if (ID2D1SolidColorBrush* glyph = AcquireBrush(WithAlpha(tint, settingsOpacity_))) {
            DrawCalendarGlyph(D2D1::Point2F((badge.left + badge.right) * 0.5f, (badge.top + badge.bottom) * 0.5f),
                              (badge.bottom - badge.top) * 0.62f, glyph);
        }
        float right = rect.right - 14.0f;
        if (!m.joinUrl.empty()) right = DrawJoinButton(rect, Loc(L"Join")) - 10.0f;
        DrawPillLines(rect, badge.right + 14.0f, right, std::wstring(Loc(L"Meeting")) + L" · " + MeetingCountdown(m.startsAt, now), m.title);
    }

    // ── Download pill: name, size and speed, with an indeterminate bar ───────
    void DrawDownloadPill(const SharedState& state, D2D1_RECT_F rect, double now) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 160.0f) return;
        const DownloadSnapshot& d = state.download;
        const D2D1_RECT_F badge = DrawPillBadge(rect);
        const D2D1_POINT_2F bc = D2D1::Point2F((badge.left + badge.right) * 0.5f, (badge.top + badge.bottom) * 0.5f);
        const float bs = (badge.bottom - badge.top) * 0.62f;
        if (ID2D1SolidColorBrush* glyph = AcquireBrush(WithAlpha(d.finished ? material_.accent : D2D1::ColorF(1, 1, 1, 0.9f), settingsOpacity_))) {
            if (d.finished) {
                // A tick.
                const float st = std::max(1.4f, bs * 0.12f);
                target_->DrawLine(D2D1::Point2F(bc.x - bs * 0.30f, bc.y), D2D1::Point2F(bc.x - bs * 0.06f, bc.y + bs * 0.24f), glyph, st);
                target_->DrawLine(D2D1::Point2F(bc.x - bs * 0.06f, bc.y + bs * 0.24f), D2D1::Point2F(bc.x + bs * 0.34f, bc.y - bs * 0.26f), glyph, st);
            } else {
                // The download arrow, dropping gently.
                const float drop = static_cast<float>(std::fmod(now, 1.2) / 1.2);
                DrawMetricGlyph(D2D1::Point2F(bc.x, bc.y - bs * 0.06f + drop * bs * 0.12f), bs, 7, glyph);
            }
        }

        std::wstring label;
        if (d.finished) {
            label = Loc(L"Downloaded · click to show");
        } else {
            label = FormatBytes(static_cast<double>(d.bytes));
            if (d.bytesPerSecond > 1000.0) label += L" · " + FormatBytes(d.bytesPerSecond) + L"/s";
            if (d.others > 0) label += L" · +" + std::to_wstring(d.others);
        }
        DrawPillLines(rect, badge.right + 14.0f, rect.right - 16.0f, label, d.name);

        if (!d.finished) {
            // Indeterminate bar: a highlight sliding along a faint track.
            const float y = rect.bottom - 7.0f;
            const float left = badge.right + 14.0f, right = rect.right - 18.0f;
            if (ID2D1SolidColorBrush* track = AcquireBrush(D2D1::ColorF(1, 1, 1, 0.10f))) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, y - 1.5f, right, y + 1.5f), 1.5f, 1.5f), track);
            }
            const float phase = static_cast<float>(std::fmod(now, 1.6) / 1.6);
            const float w = (right - left) * 0.28f;
            const float x0 = left + (right - left + w) * phase - w;
            if (ID2D1SolidColorBrush* bar = AcquireBrush(WithAlpha(material_.accent, 0.95f * settingsOpacity_))) {
                target_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(std::max(left, x0), y - 1.5f, std::min(right, x0 + w), y + 1.5f), 1.5f, 1.5f), bar);
            }
        }
    }

    // ── Calendar tab: "Up next" in place of the date block ────────────────────
    // `panel` is the hero column left of the month grid (93 x ~148 px). The Join
    // button's position is mirrored by CalendarUpNextJoinHit.
    void DrawCalendarUpNext(const SharedState& state, D2D1_RECT_F panel, float scale) {
        const MeetingSnapshot& m = state.meeting;
        DrawCard(panel, 12.0f * scale);
        const float left = panel.left + 8.0f * scale, right = panel.right - 6.0f * scale;
        auto text = [&](const std::wstring& s, IDWriteTextFormat* f, ID2D1Brush* b, float y0, float y1, bool wrap) {
            if (!f || !b) return;
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
            target_->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), f, D2D1::RectF(left, y0, right, y1), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        };
        const float top = panel.top + 8.0f * scale;
        if (ID2D1SolidColorBrush* accent = AcquireBrush(WithAlpha(material_.accent, 0.95f * settingsOpacity_))) {
            text(Loc(L"UP NEXT"), smallTextFormat_.Get(), accent, top, top + 13.0f * scale, false);
        }
        textBrush_->SetOpacity(0.95f);
        text(m.title, boldTextFormat_.Get(), textBrush_.Get(), top + 15.0f * scale, top + 66.0f * scale, true);  // up to 3 lines

        // "10:30" (or "Tomorrow 10:30") and the countdown.
        SYSTEMTIME st{};
        {
            const ULONGLONG t = static_cast<ULONGLONG>((m.startsAt + 11644473600.0) * 1e7);
            FILETIME ft{static_cast<DWORD>(t), static_cast<DWORD>(t >> 32)}, lft;
            FileTimeToLocalFileTime(&ft, &lft);
            FileTimeToSystemTime(&lft, &st);
        }
        wchar_t when[32] = {};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, when, 32);
        SYSTEMTIME today;
        GetLocalTime(&today);
        std::wstring line = when;
        if (st.wDay != today.wDay) line = std::wstring(Loc(L"Tomorrow")) + L" " + line;
        mutedBrush_->SetOpacity(0.80f);
        text(line, smallTextFormat_.Get(), mutedBrush_.Get(), top + 68.0f * scale, top + 82.0f * scale, false);
        text(MeetingCountdown(m.startsAt, EpochNow()), smallTextFormat_.Get(), mutedBrush_.Get(), top + 82.0f * scale, top + 96.0f * scale, false);
        mutedBrush_->SetOpacity(0.75f);

        if (!m.joinUrl.empty()) {
            const D2D1_RECT_F b = D2D1::RectF(panel.left + 6.0f * scale, panel.bottom - 30.0f * scale,
                                              panel.right - 6.0f * scale, panel.bottom - 8.0f * scale);
            if (ID2D1SolidColorBrush* fill = AcquireBrush(WithAlpha(material_.accent, 0.95f * settingsOpacity_))) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(b, 11.0f * scale, 11.0f * scale), fill);
            }
            if (smallTextFormat_) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                if (ID2D1SolidColorBrush* ink = AcquireBrush(D2D1::ColorF(0.07f, 0.06f, 0.10f, settingsOpacity_))) {
                    const wchar_t* join = Loc(L"Join");
                    target_->DrawTextW(join, static_cast<UINT32>(wcslen(join)), smallTextFormat_.Get(), b, ink, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
        textBrush_->SetOpacity(0.90f);
    }
