// Bunbun: drawing for the To-do tab and the reminder pill. Included inside the
// Renderer class body (member functions). Layout constants: TodoLayout.

    // ── To-do tab ────────────────────────────────────────────────────────────
    void DrawTodoDashboard(const SharedState& state, D2D1_RECT_F rect, float scale) {
        using namespace TodoLayout;
        const float padX = kPadX * scale;
        const std::vector<TodoItem> rows = TodoRows(state.todos);
        int open = 0;
        for (const TodoItem& t : state.todos) open += t.done ? 0 : 1;

        // Title, with the number still to do on the right.
        if (boldTextFormat_) {
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textBrush_->SetOpacity(0.96f);
            const wchar_t* title = Loc(L"To-do");
            target_->DrawTextW(title, static_cast<UINT32>(wcslen(title)), boldTextFormat_.Get(),
                               D2D1::RectF(rect.left + padX, rect.top + 14.0f * scale, rect.right - padX, rect.top + 32.0f * scale),
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (smallTextFormat_ && open > 0) {
            const std::wstring left = std::to_wstring(open) + L" " + Loc(L"to do");
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            mutedBrush_->SetOpacity(0.6f);
            target_->DrawTextW(left.c_str(), static_cast<UINT32>(left.size()), smallTextFormat_.Get(),
                               D2D1::RectF(rect.left + padX, rect.top + 17.0f * scale, rect.right - padX, rect.top + 32.0f * scale),
                               mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        if (rows.empty()) {
            if (smallTextFormat_) {
                const wchar_t* hint = Loc(L"Nothing to do. Press Ctrl+Alt+B and try “add buy milk” or “remind me at 3pm to call Sam”.");
                mutedBrush_->SetOpacity(0.7f);
                target_->DrawTextW(hint, static_cast<UINT32>(wcslen(hint)), smallTextFormat_.Get(),
                                   D2D1::RectF(rect.left + padX, rect.top + (kListTop + 6.0f) * scale, rect.right - padX, rect.top + (kListTop + 60.0f) * scale),
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            mutedBrush_->SetOpacity(0.75f);
            return;
        }

        const double now = EpochNow();
        for (size_t i = 0; i < rows.size(); ++i) {
            const TodoItem& t = rows[i];
            const float top = rect.top + (kListTop + static_cast<float>(i) * kRowHeight) * scale;
            const D2D1_RECT_F row = D2D1::RectF(rect.left + padX - 6.0f * scale, top, rect.right - padX + 6.0f * scale, top + (kRowHeight - 3.0f) * scale);
            if (ID2D1SolidColorBrush* bg = AcquireBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, t.done ? 0.02f : 0.045f))) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(row, 8.0f * scale, 8.0f * scale), bg);
            }
            // Tick box: a ring, filled with a check when done.
            const D2D1_POINT_2F box = D2D1::Point2F(row.left + 14.0f * scale, (row.top + row.bottom) * 0.5f);
            const float r = 6.5f * scale;
            if (t.done) {
                if (ID2D1SolidColorBrush* fill = AcquireBrush(WithAlpha(material_.accent, 0.9f * settingsOpacity_))) {
                    target_->FillEllipse(D2D1::Ellipse(box, r, r), fill);
                }
                if (ID2D1SolidColorBrush* ink = AcquireBrush(D2D1::ColorF(0.07f, 0.06f, 0.10f, settingsOpacity_))) {
                    target_->DrawLine(D2D1::Point2F(box.x - r * 0.45f, box.y), D2D1::Point2F(box.x - r * 0.1f, box.y + r * 0.38f), ink, 1.6f * scale);
                    target_->DrawLine(D2D1::Point2F(box.x - r * 0.1f, box.y + r * 0.38f), D2D1::Point2F(box.x + r * 0.5f, box.y - r * 0.4f), ink, 1.6f * scale);
                }
            } else if (ID2D1SolidColorBrush* ring = AcquireBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f))) {
                target_->DrawEllipse(D2D1::Ellipse(box, r, r), ring, 1.4f * scale);
            }

            // Due time on the right: red once it's past.
            float textRight = row.right - 10.0f * scale;
            if (t.due > 0.0 && !t.done && smallTextFormat_) {
                const std::wstring due = Todos::FormatDue(t.due);
                const bool late = t.due < now;
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                ID2D1SolidColorBrush* b = late ? AcquireBrush(D2D1::ColorF(1.0f, 0.42f, 0.38f, settingsOpacity_)) : mutedBrush_.Get();
                if (!late) mutedBrush_->SetOpacity(0.72f);
                if (b) {
                    target_->DrawTextW(due.c_str(), static_cast<UINT32>(due.size()), smallTextFormat_.Get(),
                                       D2D1::RectF(row.right - 120.0f * scale, row.top + 5.0f * scale, row.right - 10.0f * scale, row.bottom),
                                       b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                textRight = row.right - 124.0f * scale;
            }
            if (textFormat_) {
                textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                textBrush_->SetOpacity(t.done ? 0.45f : 0.95f);
                const D2D1_RECT_F tr = D2D1::RectF(row.left + 28.0f * scale, row.top + 2.0f * scale, textRight, row.bottom);
                target_->DrawTextW(t.text.c_str(), static_cast<UINT32>(t.text.size()), textFormat_.Get(), tr, textBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            }
        }
        textBrush_->SetOpacity(0.96f);
        mutedBrush_->SetOpacity(0.75f);
    }

    // Quiet reminder: a small still bell and the text. A tap opens the full pill
    // (ReminderPillClick), so the whole pill is recorded as its hit area.
    void DrawQuietReminderPill(const SharedState& state, D2D1_RECT_F rect) {
        const float cy = (rect.top + rect.bottom) * 0.5f;
        if (ID2D1SolidColorBrush* bell = AcquireBrush(WithAlpha(material_.accent, settingsOpacity_))) {
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + 22.0f, cy), 5.0f, 5.0f), bell);
        }
        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            textBrush_->SetOpacity(0.92f);
            target_->DrawTextW(state.reminder.text.c_str(), static_cast<UINT32>(state.reminder.text.size()), textFormat_.Get(),
                               D2D1::RectF(rect.left + 36.0f, rect.top, rect.right - 14.0f, rect.bottom), textBrush_.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        D2D1_MATRIX_3X2_F m;
        target_->GetTransform(&m);
        float dpiX = 96.0f, dpiY = 96.0f;
        target_->GetDpi(&dpiX, &dpiY);
        auto apply = [&](float x, float y) { return D2D1::Point2F((x * m._11 + y * m._21 + m._31) * dpiX / 96.0f, (x * m._12 + y * m._22 + m._32) * dpiY / 96.0f); };
        const D2D1_POINT_2F a = apply(rect.left, rect.top), c = apply(rect.right, rect.bottom);
        g_reminderQuietRect = D2D1::RectF(std::min(a.x, c.x), std::min(a.y, c.y), std::max(a.x, c.x), std::max(a.y, c.y));
        g_reminderButtonsAt = NowSeconds();
    }

    // ── Reminder pill: a bell, the to-do, and Snooze / Done ──────────────────
    void DrawReminderPill(const SharedState& state, D2D1_RECT_F rect, double now) {
        using namespace TodoLayout;
        if (state.reminder.quiet) {
            DrawQuietReminderPill(state, rect);
            return;
        }
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 200.0f) return;
        const D2D1_RECT_F badge = DrawPillBadge(rect);
        const D2D1_POINT_2F bc = D2D1::Point2F((badge.left + badge.right) * 0.5f, (badge.top + badge.bottom) * 0.5f);
        const float bs = (badge.bottom - badge.top) * 0.55f;
        // A bell that rings for the first couple of seconds of each 4 s.
        const float ring = std::fmod(static_cast<float>(now), 4.0f) < 1.2f ? std::sin(static_cast<float>(now) * 28.0f) * 14.0f : 0.0f;
        D2D1_MATRIX_3X2_F saved;
        target_->GetTransform(&saved);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(ring, D2D1::Point2F(bc.x, bc.y - bs * 0.45f)) * saved);
        if (ID2D1SolidColorBrush* bell = AcquireBrush(WithAlpha(material_.accent, settingsOpacity_))) {
            ComPtr<ID2D1PathGeometry> g;
            if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&g)) && g) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(g->Open(&sink)) && sink) {
                    sink->BeginFigure(D2D1::Point2F(bc.x - bs * 0.45f, bc.y + bs * 0.25f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(bc.x - bs * 0.35f, bc.y - bs * 0.55f), D2D1::Point2F(bc.x + bs * 0.35f, bc.y - bs * 0.55f),
                                                        D2D1::Point2F(bc.x + bs * 0.45f, bc.y + bs * 0.25f)));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    target_->FillGeometry(g.Get(), bell);
                }
            }
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bc.x, bc.y + bs * 0.38f), bs * 0.12f, bs * 0.12f), bell);
        }
        target_->SetTransform(saved);

        // Buttons from the right: Done (accent), then Snooze.
        const float cy = (rect.top + rect.bottom) * 0.5f;
        auto button = [&](float right, const wchar_t* text, bool accent) {
            const D2D1_RECT_F b = D2D1::RectF(right - kButtonW, cy - 13.0f, right, cy + 13.0f);
            if (ID2D1SolidColorBrush* fill = AcquireBrush(accent ? WithAlpha(material_.accent, 0.95f * settingsOpacity_) : D2D1::ColorF(1, 1, 1, 0.12f))) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(b, 13.0f, 13.0f), fill);
            }
            if (smallTextFormat_) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                if (ID2D1SolidColorBrush* ink = AcquireBrush(accent ? D2D1::ColorF(0.07f, 0.06f, 0.10f, settingsOpacity_) : D2D1::ColorF(1, 1, 1, 0.9f))) {
                    target_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), smallTextFormat_.Get(), b, ink, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            return b.left;
        };
        const float doneLeft = button(rect.right - kButtonRight, Loc(L"Done"), true);
        const float snoozeLeft = button(doneLeft - kButtonGap, Loc(L"Snooze"), false);

        // Record the buttons in window pixels for ReminderPillClick: through the
        // pill's current transform (size scale), then DIPs to pixels.
        D2D1_MATRIX_3X2_F m;
        target_->GetTransform(&m);
        float dpiX = 96.0f, dpiY = 96.0f;
        target_->GetDpi(&dpiX, &dpiY);
        auto toClient = [&](float l, float t, float r, float b) {
            auto apply = [&](float x, float y) {  // MinGW's matrix has no TransformPoint
                return D2D1::Point2F(x * m._11 + y * m._21 + m._31, x * m._12 + y * m._22 + m._32);
            };
            const D2D1_POINT_2F a = apply(l, t), c = apply(r, b);
            return D2D1::RectF(std::min(a.x, c.x) * dpiX / 96.0f, std::min(a.y, c.y) * dpiY / 96.0f,
                               std::max(a.x, c.x) * dpiX / 96.0f, std::max(a.y, c.y) * dpiY / 96.0f);
        };
        g_reminderDoneRect = toClient(doneLeft, cy - 13.0f, doneLeft + kButtonW, cy + 13.0f);
        g_reminderSnoozeRect = toClient(snoozeLeft, cy - 13.0f, snoozeLeft + kButtonW, cy + 13.0f);
        g_reminderButtonsAt = NowSeconds();
        DrawPillLines(rect, badge.right + 14.0f, snoozeLeft - 10.0f, Loc(L"Reminder"), state.reminder.text);
    }
