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
                // Bunbun: wrapped; on one line it ran off the page's right edge.
                const DWRITE_WORD_WRAPPING wrap = smallTextFormat_->GetWordWrapping();
                smallTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                target_->DrawTextW(hint, static_cast<UINT32>(wcslen(hint)), smallTextFormat_.Get(),
                                   D2D1::RectF(rect.left + padX, rect.top + (kListTop + 6.0f) * scale, rect.right - padX, rect.top + (kListTop + 60.0f) * scale),
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                smallTextFormat_->SetWordWrapping(wrap);
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
    // (ReminderPillAction), so the whole pill is recorded as its hit area.
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
        SHit(rect, StudioAction::ReminderExpand);  // Bunbun: a Studio hit (HandleStudioClick)
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
        // Bunbun: a water reminder has a drop that bobs, instead of the bell.
        const bool water = IsWaterReminderId(state.reminder.id);
        if (water) {
            EnsureRoundStroke();
            const float bob = std::sin(static_cast<float>(now) * 4.0f) * bs * 0.08f;
            SIcon(StudioPage::Water, D2D1::Point2F(bc.x, bc.y + bob), bs * 1.7f, SAccent());  // Bunbun: the accent
        }
        // A bell that rings for the first couple of seconds of each 4 s.
        const float ring = water ? 0.0f
                                 : (std::fmod(static_cast<float>(now), 4.0f) < 1.2f ? std::sin(static_cast<float>(now) * 28.0f) * 14.0f : 0.0f);
        D2D1_MATRIX_3X2_F saved;
        target_->GetTransform(&saved);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(ring, D2D1::Point2F(bc.x, bc.y - bs * 0.45f)) * saved);
        if (ID2D1SolidColorBrush* bell = water ? nullptr : AcquireBrush(WithAlpha(material_.accent, settingsOpacity_))) {
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
        // Bunbun: a water reminder says Drank (or Refilled) and Later.
        const float doneLeft = button(rect.right - kButtonRight,
                                      water ? (state.reminder.id == kWaterRefillId ? L"Refilled" : L"Drank") : Loc(L"Done"), true);
        const float snoozeLeft = button(doneLeft - kButtonGap, water ? L"Later" : Loc(L"Snooze"), false);

        // Bunbun: the buttons are Studio hits (HandleStudioClick -> ReminderPillAction):
        // SHit maps them the way every Studio button is mapped.
        SHit(D2D1::RectF(doneLeft, cy - 13.0f, doneLeft + kButtonW, cy + 13.0f), StudioAction::ReminderDone);
        SHit(D2D1::RectF(snoozeLeft, cy - 13.0f, snoozeLeft + kButtonW, cy + 13.0f), StudioAction::ReminderSnooze);
        // Bunbun: a water reminder's label carries the day so far, so its line stays short.
        std::wstring label = Loc(L"Reminder");
        if (water) label = L"Water \x00B7 " + std::to_wstring(state.water.takenMl) + L" of " + std::to_wstring(state.water.goalMl) + L" ml";
        DrawPillLines(rect, badge.right + 14.0f, snoozeLeft - 10.0f, label, state.reminder.text);
    }
