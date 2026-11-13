// Bunbun: Renderer: agent pill and approval card.
// Split out of island_core.cpp; included inside class Renderer at the
// original position, so declaration order is unchanged. Not a standalone file.

    // ── Bunbun: agent pill and approval card ─────────────────────────────────
    // Gaze toward the cursor, so Bun watches you reach for the buttons.
    // Bunbun: on every display. Another display's own island (offscreen_) looks from
    // where it sits there (screenOrigin_); with Linked copies, a pointer on another
    // display counts from the matching spot on the island, so the copy there looks at it.
   public:
    POINT screenOrigin_{};
   private:
    D2D1_POINT_2F MascotLook(D2D1_POINT_2F mascotCenter) {
        POINT p{};
        if (!GetCursorPos(&p)) return D2D1::Point2F(0, 0);
        if (offscreen_) {
            p.x -= screenOrigin_.x;
            p.y -= screenOrigin_.y;
        } else {
            Mirrors::PointerToIsland(&p);
            if (!ScreenToClient(hwnd_, &p)) return D2D1::Point2F(0, 0);
        }
        return D2D1::Point2F(Clamp((p.x - mascotCenter.x) / 160.0f, -1.0f, 1.0f),
                             Clamp((p.y - mascotCenter.y) / 90.0f, -1.0f, 1.0f));
    }

    // Bun in the collapsed island: vibing with headphones while music plays,
    // napping after prolonged inactivity, breathing and blinking beside the clock otherwise.
    void DrawBunSlot(const SharedState& state, const Settings& settings, const Activity& activity, D2D1_RECT_F slot,
                     double now) {
        const bool music = activity.kind == IslandKind::Media && state.media.playing;
        static bool s_wasMusic = false;
        static double s_moodSince = 0.0;
        if (music != s_wasMusic) { s_wasMusic = music; s_moodSince = now; }

        // Poke reaction: hopping happily for 2 seconds after poke
        static double s_lastPokeTime = 0.0;
        extern std::atomic<double> g_lastMascotPokeTime;
        const double pokeTime = g_lastMascotPokeTime.load(std::memory_order_relaxed);
        if (pokeTime > s_lastPokeTime) {
            s_lastPokeTime = pokeTime;
            s_moodSince = now;
        }

        bb::mascot::Mood mood = bb::mascot::Mood::Idle;
        // Bunbun: battery moods. A cheer when the battery fills up; drowsy below 15%.
        const bool cheer = settings.batteryMoods && now - g_batteryCheerAt.load(std::memory_order_relaxed) < 2.5;
        const bool drowsy = settings.batteryMoods && state.battery.hasBattery && !state.battery.charging && state.battery.percent < 15;
        // Bunbun: the cheer starts the mood (max, not min): with min an idle mascot's
        // mood had begun long ago, so the 2 s celebration (Celebrate / Hop) never played.
        if (cheer) s_moodSince = std::max(s_moodSince, g_batteryCheerAt.load(std::memory_order_relaxed));
        // Bunbun: seconds since the last keyboard or mouse input anywhere (AFK).
        LASTINPUTINFO lii{sizeof(lii)};
        const double awaySeconds = GetLastInputInfo(&lii) ? (GetTickCount() - lii.dwTime) / 1000.0 : 0.0;
        if (now - s_lastPokeTime < 2.0 || cheer) {
            mood = bb::mascot::Mood::Happy;
        } else if (settings.liveMood && state.system.screenActive) {
            mood = bb::mascot::Mood::Live;  // Bunbun: streaming, sharing or recording the screen
        } else if (drowsy && !music) {
            mood = bb::mascot::Mood::Sleepy;
        } else if (music) {
            mood = MediaMascotMood(state.media, settings);  // Bunbun: popcorn for a video
        } else if (settings.mascotDnd || FocusModeActive() ||  // Bunbun: Focus mode too
                   (settings.dndFollowSystem && g_isDnDActive.load(std::memory_order_relaxed))) {  // and Windows' DND
            // Bunbun: the mascot's own do-not-disturb (settings or Ask Bun): headphones
            // on, eyes closed, a muted bell. Deliberately independent of Windows' DND.
            mood = bb::mascot::Mood::Dnd;
        } else if (settings.sleepMinutes > 0 && awaySeconds >= settings.sleepMinutes * 60.0) {
            mood = bb::mascot::Mood::Sleepy;  // Bunbun: away for Sleep after: asleep
        } else if (settings.afkMinutes > 0 && awaySeconds >= settings.afkMinutes * 60.0) {
            mood = bb::mascot::Mood::Snack;  // Bunbun: away for AFK after: a snack while you're gone
        } else {
            // Idle life cycle: after Sleep after (Bunbun: was a fixed 8 minutes) without
            // touching the island, the mascot takes a cozy nap.
            extern std::atomic<double> g_lastIslandInteractionTime;
            const double lastActive = g_lastIslandInteractionTime.load(std::memory_order_relaxed);
            if (settings.sleepMinutes > 0 && lastActive > 0.0 && (now - lastActive > settings.sleepMinutes * 60.0)) {
                mood = bb::mascot::Mood::Sleepy;
            }
        }

        // Bunbun: a coding agent at work shows on the mascot even with the agent pill
        // off (Bunbun.AgentPill): working, thinking, waiting for you, done or failed.
        bool agentShown = false;
        {
            static bb::AgentPhase s_agentPhase = bb::AgentPhase::None;
            const bb::AgentPhase phase = settings.bunbunAgents ? state.agent.phase : bb::AgentPhase::None;
            if (phase != bb::AgentPhase::None) {
                if (phase != s_agentPhase) s_moodSince = now;
                mood = AgentMood(phase);
                agentShown = true;
                g_layoutDirty = true;  // its animation keeps going
            }
            s_agentPhase = phase;
        }
        // Bunbun: Work mode (working at the laptop, lunch) when no agent is on show,
        // over music and the idle moods.
        // Not over Do Not Disturb you chose (Mascot DND, Focus, Windows' DND): turning it
        // on during work hours did nothing.
        if (!agentShown && mood != bb::mascot::Mood::Dnd) {
            static bb::mascot::Mood s_work = bb::mascot::Mood::Idle;
            bb::mascot::Mood work = mood;
            if (WorkMood(state.work, &work)) {
                if (work != s_work) s_moodSince = now;
                mood = work;
                g_layoutDirty = true;
            }
            s_work = work;
        }
        // Bunbun: away or asleep wins in every state (music, agents at work, Work mode);
        // only an agent waiting for your answer still shows.
        {
            static bool s_away = false;
            bb::mascot::Mood away = mood;
            const bool isAway = AwayMood(settings, state, &away);
            if (isAway) {
                if (!s_away) s_moodSince = now;
                mood = away;
            }
            s_away = isAway;
        }
        // Bunbun: a glass of water logged: Bun drinks it, and cheers the one that
        // reaches the day's goal. Pokes below still win.
        {
            double since = 0.0;
            bb::mascot::Mood water = mood;
            if (WaterMood(now, &water, &since)) {
                mood = water;
                s_moodSince = since;
                g_layoutDirty = true;  // keep frames coming while it plays
            }
        }
        // Bunbun: poke reactions win over everything (cross, then dizzy), and the
        // mascot says hello for a moment after Bunbun starts.
        if (settings.pokeReactions) {
            static double s_helloAt = -1.0;
            if (s_helloAt < 0.0) { s_helloAt = now; s_moodSince = now; }
            const double dizzyUntil = g_mascotDizzyUntil.load(std::memory_order_relaxed);
            const double crossUntil = g_mascotCrossUntil.load(std::memory_order_relaxed);
            static bb::mascot::Mood s_reaction = bb::mascot::Mood::Idle;
            bb::mascot::Mood reaction = mood;
            if (NowSeconds() < dizzyUntil) reaction = bb::mascot::Mood::Confused;
            else if (NowSeconds() < crossUntil) reaction = bb::mascot::Mood::Alert;
            else if (now - s_helloAt < 2.6) reaction = bb::mascot::Mood::Happy;
            if (reaction != mood) {
                if (reaction != s_reaction) s_moodSince = now;
                mood = reaction;
                g_layoutDirty = true;  // keep frames coming until the reaction ends
            }
            s_reaction = reaction;
        }

        // Bunbun: fit the character's real outline to the slot, so short-eared
        // mascots fill the height like Bun does instead of leaving a gap above.
        const float w = slot.right - slot.left;
        const float pad = 2.0f * settings.sizeScale;
        D2D1_POINT_2F c{};
        float size = 0.0f;
        bb::mascot::FitToBox(D2D1::RectF(slot.left + w * 0.16f - w * 0.05f, slot.top + pad, slot.right + w * 0.05f, slot.bottom - pad),
                             &c, &size);
        if (settings.idleEyes) {  // Bunbun: just the eyes
            DrawIdleEyes(state, D2D1::Point2F((slot.left + slot.right) * 0.5f, (slot.top + slot.bottom) * 0.5f),
                         slot.bottom - slot.top, mood, now, s_moodSince);
            return;
        }
        bb::mascot::Draw(target_.Get(), c, size, mood, now, s_moodSince, MascotLook(c));
    }

    // One recoloured brush for the agent pill's status colours and button ink.
    ID2D1SolidColorBrush* AgentInk(D2D1_COLOR_F color) {
        if (!agentInkBrush_) target_->CreateSolidColorBrush(color, &agentInkBrush_);
        if (agentInkBrush_) agentInkBrush_->SetColor(color);
        return agentInkBrush_ ? agentInkBrush_.Get() : textBrush_.Get();
    }

    void DrawAgentText(const std::wstring& text, D2D1_RECT_F r, IDWriteTextFormat* format, ID2D1Brush* brush,
                       bool center = false) {
        if (!format || text.empty()) return;
        // The shared formats carry whatever alignment their owners last set
        // (boldTextFormat_ is centred, for one), so set ours and put theirs back.
        const DWRITE_TEXT_ALIGNMENT oldAlign = format->GetTextAlignment();
        const DWRITE_PARAGRAPH_ALIGNMENT oldPara = format->GetParagraphAlignment();
        format->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, r, brush,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
        format->SetTextAlignment(oldAlign);
        format->SetParagraphAlignment(oldPara);
    }

    // Bunbun: one line that ends in an ellipsis when it doesn't fit, never wrapped
    // or slid; the start of the text always shows.
    void DrawEllipsizedText(const std::wstring& text, D2D1_RECT_F r, IDWriteTextFormat* format, ID2D1Brush* brush) {
        if (!format || text.empty() || !dwriteFactory_) return;
        ComPtr<IDWriteTextLayout> layout;
        const float w = std::max(1.0f, r.right - r.left), h = std::max(1.0f, r.bottom - r.top);
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, w, h, &layout)) || !layout)
            return;
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(format, &ellipsis))) {
            const DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            layout->SetTrimming(&trim, ellipsis.Get());
        }
        target_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        target_->PopAxisAlignedClip();
    }

    // Bunbun: a finished agent's reply, cut to its first sentence and stripped of
    // markdown, for the one-line pill ("**Done.** The `x` tab…" → "Done.").
    static std::wstring AgentSummaryLine(const std::wstring& text) {
        std::wstring out;
        out.reserve(std::min<size_t>(text.size(), 240));
        bool lineStart = true;
        for (size_t i = 0; i < text.size() && out.size() < 240; ++i) {
            const wchar_t c = text[i];
            if (c == L'\n' || c == L'\r') {
                if (!out.empty()) break;  // the first line (or paragraph) only
                lineStart = true;
                continue;
            }
            if (c == L'*' || c == L'`' || (c == L'_' && (i == 0 || text[i - 1] == L' '))) continue;
            if (lineStart && (c == L'#' || c == L'>' || c == L'-' || c == L' ')) continue;  // headings, quotes, bullets
            lineStart = false;
            out += c;
            // The end of the first sentence.
            if ((c == L'.' || c == L'!' || c == L'?') && i + 1 < text.size() &&
                (text[i + 1] == L' ' || text[i + 1] == L'\n' || text[i + 1] == L'\r'))
                break;
        }
        while (!out.empty() && out.back() == L' ') out.pop_back();
        return out;
    }

    void DrawAgent(const SharedState& state, D2D1_RECT_F rect, const Settings& settings, double now) {
        const bb::AgentView& a = state.agent;
        PublishContentGeometry(rect);
        g_agentButtons.clear();
        g_agentButtonsRequest = a.requestId;
        if (rect.right - rect.left < 120.0f || rect.bottom - rect.top < 30.0f) return;

        const std::wstring who = a.project.empty() ? a.agent : a.agent + L" · " + a.project;
        const auto mood = AgentMood(a.phase);

        if (!AgentLayout::IsCard(a.phase)) {
            // Compact pill: Bun | who / step | status
            const float cy = (rect.top + rect.bottom) * 0.5f;
            // Bunbun: same 2 px margin as the clock-side slot (was 4 px), so a mood
            // shown here is as big as the idle mascot beside the clock.
            const float m = (rect.bottom - rect.top) - 4.0f;
            const D2D1_POINT_2F mc = D2D1::Point2F(rect.left + 10.0f + m * 0.5f, cy);
            float tx = settings.bunbunMascot ? mc.x + m * 0.5f + 6.0f : rect.left + 16.0f;
            if (settings.bunbunMascot) {
                // Bunbun: fit the character's real outline to the square, not Bun's ears.
                D2D1_POINT_2F fc{};
                float fs = 0.0f;
                bb::mascot::FitToBox(D2D1::RectF(mc.x - m * 0.5f, mc.y - m * 0.5f, mc.x + m * 0.5f, mc.y + m * 0.5f), &fc, &fs);
                // Bunbun: room for art reaching past the head (a skin pack's sparks,
                // question marks, a raised hand): clear of the pill's edge and the text.
                float reachL = 0.5f, reachR = 0.5f;
                bb::mascot::HorizontalReach(&reachL, &reachR);
                fc.x += std::max(0.0f, rect.left + 8.0f - (fc.x - reachL * fs));
                tx = std::max(tx, fc.x + reachR * fs + 4.0f);
                bb::mascot::Draw(target_.Get(), fc, fs, mood, now, a.phaseSince, MascotLook(fc));
            }
            const float statusW = 70.0f;
            // Bunbun: the Reply button is wider than the status column; keep text clear of it.
            const bool replyButton = a.phase == bb::AgentPhase::Finished && a.replyId;
            const float textRight = rect.right - statusW - (replyButton ? 12.0f : 0.0f);

            mutedBrush_->SetOpacity(0.75f);
            DrawEllipsizedText(who, D2D1::RectF(tx, cy - 18.0f, textRight, cy - 3.0f), smallTextFormat_.Get(),
                               mutedBrush_.Get());
            textBrush_->SetOpacity(0.95f);
            if (a.phase == bb::AgentPhase::Finished) {
                // Bunbun: a finished task's reply can be paragraphs of markdown; show its
                // first sentence, still, ending in "…" if it doesn't fit.
                std::wstring line = AgentSummaryLine(a.step);
                if (line.empty()) line = L"Done";
                DrawEllipsizedText(line, D2D1::RectF(tx, cy - 3.0f, textRight, cy + 15.0f), textFormat_.Get(), textBrush_.Get());
            } else {
                const std::wstring step = a.step.empty() ? std::wstring(L"Getting started…") : a.step;
                DrawMarqueeText(step, D2D1::RectF(tx, cy - 3.0f, textRight, cy + 15.0f), textFormat_.Get(),
                                textBrush_.Get(), now, 28.0f, marqueeAgentCache_);
            }

            const D2D1_RECT_F status = D2D1::RectF(rect.right - statusW, cy - 10.0f, rect.right - 14.0f, cy + 10.0f);
            if (a.phase == bb::AgentPhase::Working && a.added >= 0) {
                const std::wstring plus = L"+" + std::to_wstring(a.added);
                const std::wstring minus = L"−" + std::to_wstring(a.removed);
                const float mid = (status.left + status.right) * 0.5f;
                DrawAgentText(plus, D2D1::RectF(status.left, status.top, mid + 4.0f, status.bottom),
                              smallTextFormat_.Get(), AgentInk(D2D1::ColorF(0x34C759)), true);
                DrawAgentText(minus, D2D1::RectF(mid + 4.0f, status.top, status.right, status.bottom),
                              smallTextFormat_.Get(), AgentInk(D2D1::ColorF(0xFF6B5E)), true);
            } else if (a.phase == bb::AgentPhase::Thinking || a.phase == bb::AgentPhase::Working) {
                // Three dots taking turns, like a typing indicator.
                const float dx = 8.0f;
                const float x0 = (status.left + status.right) * 0.5f - dx;
                for (int i = 0; i < 3; ++i) {
                    const float ph = static_cast<float>(std::fmod(now * 1.6 - i * 0.18, 1.0));
                    const float lift = std::max(0.0f, std::sin(ph * 3.14159265f));
                    accentBrush_->SetOpacity(0.35f + 0.65f * lift);
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x0 + i * dx, cy - lift * 3.0f), 2.6f, 2.6f),
                                         accentBrush_.Get());
                }
                accentBrush_->SetOpacity(1.0f);
            } else if (a.phase == bb::AgentPhase::Finished && a.replyId) {
                // Bunbun: the finished task is listening; a Reply button instead of "Done".
                const D2D1_RECT_F rb = D2D1::RectF(status.left - 6.0f, cy - 12.0f, status.right + 6.0f, cy + 12.0f);
                const bool hovered = static_cast<int>(g_agentButtons.size()) == g_agentHoveredButton;
                raisedStrongBrush_->SetOpacity(hovered ? 1.0f : 0.85f);
                target_->FillRoundedRectangle(D2D1::RoundedRect(rb, 12.0f, 12.0f), raisedStrongBrush_.Get());
                raisedStrongBrush_->SetOpacity(1.0f);
                DrawAgentText(L"Reply", rb, smallTextFormat_.Get(), AgentInk(D2D1::ColorF(0x34C759)), true);
                AgentButton b;
                b.rect = D2D1::RectF(rb.left - rect.left, rb.top - rect.top, rb.right - rect.left, rb.bottom - rect.top);
                b.action = 2;  // ActReply
                g_agentButtons.push_back(b);
            } else if (a.phase == bb::AgentPhase::Finished) {
                DrawAgentText(L"Done", status, smallTextFormat_.Get(), AgentInk(D2D1::ColorF(0x34C759)), true);
            } else if (a.phase == bb::AgentPhase::Error) {
                DrawAgentText(L"Oops", status, smallTextFormat_.Get(), AgentInk(D2D1::ColorF(0xFF6B5E)), true);
            }
            if (a.sessions > 1) {
                mutedBrush_->SetOpacity(0.7f);
                DrawAgentText(L"×" + std::to_wstring(a.sessions),
                              D2D1::RectF(status.left, rect.top + 2.0f, status.right, rect.top + 14.0f),
                              smallTextFormat_.Get(), mutedBrush_.Get(), true);
            }
            mutedBrush_->SetOpacity(0.50f);
            textBrush_->SetOpacity(0.90f);
            return;
        }

        // ── Card: an agent is waiting for you ────────────────────────────────
        if (DrawAgentRichCard(state, rect, settings, now)) return;  // Bunbun: plan / several questions
        const float left = rect.left + AgentLayout::kPadX;
        const float right = rect.right - AgentLayout::kPadX;
        const float top = rect.top + AgentLayout::kPadTop;
        const float ms = AgentLayout::kMascot;
        const D2D1_POINT_2F mc = D2D1::Point2F(left + ms * 0.5f, top + ms * 0.5f + 2.0f);
        float tx = settings.bunbunMascot ? left + ms + 12.0f : left;
        if (settings.bunbunMascot) {
            D2D1_POINT_2F fc{};  // Bunbun: outline fit, as in the compact pill
            float fs = 0.0f;
            bb::mascot::FitToBox(D2D1::RectF(mc.x - ms * 0.5f, mc.y - ms * 0.5f, mc.x + ms * 0.5f, mc.y + ms * 0.5f), &fc, &fs);
            // Bunbun: room for art reaching past the head, as in the compact pill.
            float reachL = 0.5f, reachR = 0.5f;
            bb::mascot::HorizontalReach(&reachL, &reachR);
            fc.x += std::max(0.0f, rect.left + 6.0f - (fc.x - reachL * fs));
            tx = std::max(tx, fc.x + reachR * fs + 8.0f);
            bb::mascot::Draw(target_.Get(), fc, fs, mood, now, a.phaseSince, MascotLook(fc));
        }

        mutedBrush_->SetOpacity(0.75f);
        DrawAgentText(who, D2D1::RectF(tx, top, right - 40.0f, top + 16.0f), smallTextFormat_.Get(), mutedBrush_.Get());
        if (a.queued > 0) {
            DrawAgentText(L"+" + std::to_wstring(a.queued) + L" more", D2D1::RectF(right - 60.0f, top, right, top + 16.0f),
                          smallTextFormat_.Get(), mutedBrush_.Get());
        }
        const bool question = a.phase == bb::AgentPhase::Question;
        const std::wstring title = question ? std::wstring(L"Has a question for you")
                                            : (a.tool.empty() ? std::wstring(L"Needs your OK") : a.tool);
        textBrush_->SetOpacity(0.97f);
        DrawAgentText(title, D2D1::RectF(tx, top + 17.0f, right, top + 38.0f), boldTextFormat_.Get(), textBrush_.Get());

        // Detail well: the command, file or question being asked about, with diff preview for edits.
        const float wellTop = top + ms + 6.0f;
        const float buttonsTop = rect.bottom - AgentLayout::kPadBottom - AgentLayout::kButtonH;
        const D2D1_RECT_F well = D2D1::RectF(left, wellTop, right, buttonsTop - 8.0f);
        target_->FillRoundedRectangle(D2D1::RoundedRect(well, 9.0f, 9.0f), raisedBrush_.Get());
        
        const bool hasDiff = !a.diffOld.empty() || !a.diffNew.empty();
        if (hasDiff) {
            // Bunbun: the file, then every − and + line on a tinted row, in a mono font.
            // Longer than the card (see ActivityForKind) and the wheel scrolls it.
            textBrush_->SetOpacity(0.92f);
            DrawEllipsizedText(a.detail, D2D1::RectF(well.left + 10.0f, well.top + 6.0f, well.right - 10.0f, well.top + 22.0f),
                               smallTextFormat_.Get(), textBrush_.Get());
            const std::vector<bb::DiffRow> rows = bb::DiffRows(a);
            if (g_diffScrollFor != a.requestId) { g_diffScrollFor = a.requestId; g_diffScroll = 0.0f; }
            const D2D1_RECT_F view = D2D1::RectF(well.left + 4.0f, well.top + 26.0f, well.right - 4.0f, well.bottom - 4.0f);
            const float viewH = view.bottom - view.top;
            const float h = kDiffRowH * static_cast<float>(rows.size());
            g_diffScrollMax = std::max(0.0f, h - viewH);
            g_diffScroll = Clamp(g_diffScroll, 0.0f, g_diffScrollMax);
            target_->PushAxisAlignedClip(view, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            float y = view.top - g_diffScroll;
            for (const bb::DiffRow& r : rows) {
                if (y + kDiffRowH >= view.top && y <= view.bottom) {
                    const bool del = r.sign < 0;
                    const D2D1_RECT_F rr = D2D1::RectF(view.left, y, view.right - (g_diffScrollMax > 0.0f ? 8.0f : 0.0f), y + kDiffRowH);
                    if (ID2D1SolidColorBrush* bg = AcquireBrush(del ? D2D1::ColorF(1.0f, 0.27f, 0.23f, 0.13f)
                                                                    : D2D1::ColorF(0.2f, 0.84f, 0.29f, 0.12f)))
                        target_->FillRectangle(rr, bg);
                    SText(std::wstring(del ? L"\x2212 " : L"+ ") + r.text, D2D1::RectF(rr.left + 6.0f, rr.top, rr.right - 4.0f, rr.bottom),
                          SFMono, del ? D2D1::ColorF(0xFF8A80) : D2D1::ColorF(0x7EE787));
                }
                y += kDiffRowH;
            }
            target_->PopAxisAlignedClip();
            if (g_diffScrollMax > 0.0f) {
                // A thin scroll bar on the right edge, as on the plan card.
                const float thumbH = std::max(18.0f, viewH * viewH / h);
                const float ty = view.top + (viewH - thumbH) * (g_diffScroll / g_diffScrollMax);
                if (ID2D1SolidColorBrush* b = AcquireBrush(D2D1::ColorF(1, 1, 1, 0.3f)))
                    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(view.right - 4.0f, ty, view.right - 1.0f, ty + thumbH), 1.5f, 1.5f), b);
            }
        } else {
            textBrush_->SetOpacity(0.88f);
            DrawAgentText(a.detail, D2D1::RectF(well.left + 10.0f, well.top + 5.0f, well.right - 10.0f, well.bottom - 4.0f),
                          smallTextFormat_.Get(), textBrush_.Get());
        }

        // Buttons, recorded in content space for HandleAgentClick.
        auto button = [&](D2D1_RECT_F r, const std::wstring& label, bool primary, bb::AgentDecision d, int answer) {
            const int index = static_cast<int>(g_agentButtons.size());
            const bool hovered = index == g_agentHoveredButton;
            ID2D1SolidColorBrush* fill = primary ? accentBrush_.Get() : raisedStrongBrush_.Get();
            fill->SetOpacity(hovered ? 1.0f : (primary ? 0.85f : 0.9f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(r, 9.0f, 9.0f), fill);
            fill->SetOpacity(1.0f);
            ID2D1SolidColorBrush* ink = textBrush_.Get();
            if (primary) {
                // Dark ink on a bright accent, light ink on a dark one.
                const D2D1_COLOR_F c = accentBrush_->GetColor();
                const float lum = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
                ink = AgentInk(lum > 0.6f ? D2D1::ColorF(0x111114) : D2D1::ColorF(0xFFFFFF));
            }
            textBrush_->SetOpacity(0.95f);
            DrawAgentText(label, r, smallTextFormat_.Get(), ink, true);
            g_agentButtons.push_back({D2D1::RectF(r.left - rect.left, r.top - rect.top, r.right - rect.left,
                                                  r.bottom - rect.top),
                                      d, answer});
        };

        const float bh = AgentLayout::kButtonH;
        const float gap = AgentLayout::kButtonGap;
        if (question && !a.options.empty()) {
            const int n = static_cast<int>(a.options.size());
            const float w = (right - left - gap * (n - 1)) / n;
            for (int i = 0; i < n; ++i) {
                const float x = left + i * (w + gap);
                button(D2D1::RectF(x, buttonsTop, x + w, buttonsTop + bh), a.options[i], i == 0,
                       bb::AgentDecision::Allow, i);
            }
        } else if (question) {
            button(D2D1::RectF(right - 170.0f, buttonsTop, right, buttonsTop + bh), L"Answer in terminal", false,
                   bb::AgentDecision::Terminal, -1);
        } else {
            const float w = 96.0f;
            button(D2D1::RectF(left, buttonsTop, left + 130.0f, buttonsTop + bh), L"Reply in terminal", false,
                   bb::AgentDecision::Terminal, -1);
            button(D2D1::RectF(right - 2 * w - gap, buttonsTop, right - w - gap, buttonsTop + bh), L"Deny", false,
                   bb::AgentDecision::Deny, -1);
            button(D2D1::RectF(right - w, buttonsTop, right, buttonsTop + bh), L"Allow", true,
                   bb::AgentDecision::Allow, -1);
        }
        mutedBrush_->SetOpacity(0.50f);
        textBrush_->SetOpacity(0.90f);
    }

    void DrawNotification(const SharedState& state, D2D1_RECT_F rect) {
        // Bunbun: Samsung-style. ActivityForKind grows the island, in its own shape, from
        // the icon alone, to the title, to two lines (and back as it goes); what
        // is drawn here follows the island's real size, so words only show once there's
        // room for them and the springs carry the motion.
        const float w = rect.right - rect.left, h = rect.bottom - rect.top;
        if (h < 16.0f || w < 16.0f) return;
        const double now = NowSeconds();
        // Bunbun: its real lifetime (the update notice lasts 8 s, a Bluetooth alert 5 s).
        const NotificationSnapshot& note = state.notification;
        const float ttl = note.shownAt > 0.0 ? std::max(0.5f, static_cast<float>(note.expiresAt - note.shownAt)) : 4.0f;
        const float remaining = Clamp(static_cast<float>(state.notification.expiresAt - now), 0.0f, ttl);
        const float cy = (rect.top + rect.bottom) * 0.5f;

        // The app's icon in a circle: centred while the island is narrow, then at the left.
        const float iconSz = std::min(h - 12.0f, 34.0f);
        const float iconCx = rect.left + std::min(w * 0.5f, 12.0f + iconSz * 0.5f);
        const D2D1_RECT_F badge = D2D1::RectF(iconCx - iconSz * 0.5f, cy - iconSz * 0.5f, iconCx + iconSz * 0.5f, cy + iconSz * 0.5f);
        ComPtr<ID2D1SolidColorBrush> plateBrush;
        target_->CreateSolidColorBrush(material_.raisedStrong, &plateBrush);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iconCx, cy), iconSz * 0.5f, iconSz * 0.5f), plateBrush.Get());
        if (state.notification.icon) {
            const float inset = iconSz * 0.16f;  // the logo inside its circle
            const D2D1_RECT_F logo = D2D1::RectF(badge.left + inset, badge.top + inset, badge.right - inset, badge.bottom - inset);
            DrawRoundedBitmapPixels(*state.notification.icon, logo, (logo.right - logo.left) * 0.22f,
                                    notificationIconBitmap_, notificationIconGeneration_, 1.0f);
        } else if (iconFormat_) {
            const wchar_t* glyph = usingFluentIcons_ ? L"" : L"";
            textBrush_->SetOpacity(0.95f);
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), iconFormat_.Get(), badge, textBrush_.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        // The words, as the island makes room: the title once it's wide enough, the
        // second line ("App · message") once it's two lines tall.
        const float titleIn = Clamp((w - 190.0f) / 70.0f, 0.0f, 1.0f);
        const float twoLine = Clamp((h - 56.0f) / 10.0f, 0.0f, 1.0f);
        if (titleIn > 0.0f) {
            const float tx = badge.right + 12.0f - 8.0f * (1.0f - titleIn);
            const float right = rect.right - 16.0f;
            const float titleTop = (cy - 10.0f) + (-9.0f) * twoLine;
            textBrush_->SetOpacity(0.97f * titleIn);
            // (Not DrawMarqueeText: boldTextFormat_ is centred, which it doesn't expect.)
            DrawEllipsizedText(state.notification.title.empty() ? L"Notification" : state.notification.title,
                               D2D1::RectF(tx, titleTop, right, titleTop + 19.0f), boldTextFormat_.Get(), textBrush_.Get());
            if (twoLine > 0.0f) {
                std::wstring second = state.notification.app;
                if (!state.notification.body.empty())
                    second = second.empty() ? state.notification.body : second + L" \x2022 " + state.notification.body;
                mutedBrush_->SetOpacity(0.8f * twoLine * titleIn);
                DrawEllipsizedText(second, D2D1::RectF(tx, cy + 2.0f, right, cy + 18.0f), smallTextFormat_.Get(),
                                   mutedBrush_.Get());
            }
            textBrush_->SetOpacity(0.90f);
            if (twoLine >= 1.0f) DrawCountdownProgress(tx, right, rect.bottom - 6.0f, remaining / ttl);
        }
        mutedBrush_->SetOpacity(0.50f);
    }

    void DrawVolume(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 140.0f) return;
        const bool muted = state.volume.muted || state.volume.percent == 0;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14, cy - badgeSz * 0.5f,
                                        rect.left + 14 + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f; // Softer squircle corners

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        const wchar_t* glyph = muted ? L"\uE74F" : (usingFluentIcons_ ? L"\uE767" : L"\uE993");
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
        const std::wstring deviceLabel =
            state.volume.deviceName.empty() ? std::wstring(Loc(L"Volume")) : state.volume.deviceName;
        target_->DrawTextW(deviceLabel.c_str(), static_cast<UINT32>(deviceLabel.size()),
                           smallTextFormat_.Get(), labelRect, mutedBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        wchar_t value[32] = {};
        if (muted) {
            wcscpy_s(value, ARRAYSIZE(value), Loc(L"Muted"));
        } else {
            swprintf_s(value, L"%d%%", state.volume.percent);
        }
        D2D1_RECT_F valueRect = D2D1::RectF(rect.right - 58, cy - 13.0f, rect.right - 14, cy + 3.0f);
        target_->DrawTextW(value, static_cast<UINT32>(wcslen(value)), smallTextFormat_.Get(),
                           valueRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        textBrush_->SetOpacity(0.90f);

        D2D1_RECT_F track = D2D1::RectF(tx, cy + 7.0f, rect.right - 14, cy + 12.0f);
        // Shared accent track, so the volume bar matches the media scrubber.
        const float pct = Clamp(state.volume.displayPercent / 100.0f, 0.0f, 1.0f);
        if (muted) {
            // Muted still shows the level, just drained of colour.
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

    void DrawTimer(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 140.0f) return;
        const double now = NowSeconds();
        // Bunbun: a running stopwatch with no other timer: the ring sweeps once a minute.
        const bool stopwatch = !state.timer.active && !state.timer.justFinished && state.stopwatch.running;
        const double elapsed = stopwatch ? Timers::StopwatchElapsed(state.stopwatch, now) : 0.0;
        double remaining = state.timer.justFinished
            ? 0.0
            : (state.timer.running ? std::max(0.0, state.timer.endsAt - now)
                                    : state.timer.remainingAtPause);
        const int totalSec = state.timer.totalSeconds > 0 ? state.timer.totalSeconds : 1;
        const float progress = stopwatch ? static_cast<float>(std::fmod(elapsed, 60.0) / 60.0)
            : state.timer.justFinished
            ? 1.0f
            : Clamp(1.0f - static_cast<float>(remaining / totalSec), 0.0f, 1.0f);

        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14, cy - badgeSz * 0.5f,
                                        rect.left + 14 + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        const float ringR = badgeSz * 0.30f;
        D2D1_POINT_2F ringCenter = D2D1::Point2F((badge.left + badge.right) * 0.5f,
                                                  (badge.top + badge.bottom) * 0.5f);
        ComPtr<ID2D1SolidColorBrush> ringTrack;
        target_->CreateSolidColorBrush(material_.raisedStrong, &ringTrack);
        target_->DrawEllipse(D2D1::Ellipse(ringCenter, ringR, ringR), ringTrack.Get(), 2.0f);

        ComPtr<ID2D1PathGeometry> geometry;
        d2dFactory_->CreatePathGeometry(&geometry);
        ComPtr<ID2D1GeometrySink> sink;
        geometry->Open(&sink);
        const float start = -3.14159265f * 0.5f;
        const float sweep = 2.0f * 3.14159265f * progress;
        const int segments = std::max(2, static_cast<int>(40 * progress));
        auto pointAt = [&](float a) {
            return D2D1::Point2F(ringCenter.x + std::cos(a) * ringR,
                                  ringCenter.y + std::sin(a) * ringR);
        };
        sink->BeginFigure(pointAt(start), D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 1; i <= segments; ++i) {
            sink->AddLine(pointAt(start + sweep * i / segments));
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();

        ComPtr<ID2D1SolidColorBrush> ringFg;
        D2D1_COLOR_F fgColor = state.timer.isBreak
            ? D2D1::ColorF(0.19f, 0.83f, 0.38f, 1.0f)
            : D2D1::ColorF(1.0f, 0.58f, 0.0f, 1.0f);
        if (state.timer.justFinished) {
            fgColor = D2D1::ColorF(1.0f, 0.23f, 0.18f, 1.0f);
        }
        target_->CreateSolidColorBrush(fgColor, &ringFg);
        target_->DrawGeometry(geometry.Get(), ringFg.Get(), 2.4f);

        if (state.timer.active && !state.timer.running) {
            ComPtr<ID2D1SolidColorBrush> pauseBrush;
            target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.9f), &pauseBrush);
            const float h = ringR * 0.7f;
            target_->FillRectangle(
                D2D1::RectF(ringCenter.x - 2.6f, ringCenter.y - h * 0.5f,
                            ringCenter.x - 0.8f, ringCenter.y + h * 0.5f), pauseBrush.Get());
            target_->FillRectangle(
                D2D1::RectF(ringCenter.x + 0.8f, ringCenter.y - h * 0.5f,
                            ringCenter.x + 2.6f, ringCenter.y + h * 0.5f), pauseBrush.Get());
        }

        const float tx = badge.right + 14;
        mutedBrush_->SetOpacity(0.50f);
        std::wstring label = state.timer.justFinished
            ? (state.timer.isBreak ? L"Break Complete" : L"Focus Complete")
            : (state.timer.isBreak ? L"Break Timer" : L"Focus Timer");
        // Bunbun: the Timers page names its timers ("Focus 1 of 4", "Timer") and their end.
        if (stopwatch) label = L"Stopwatch";
        else if (state.timer.justFinished && !state.timer.doneLabel.empty()) label = state.timer.doneLabel;
        else if (!state.timer.justFinished && !state.timer.label.empty()) label = state.timer.label;
        D2D1_RECT_F labelRect = D2D1::RectF(tx, cy - 16.0f, rect.right - 14.0f, cy - 1.0f);
        target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                           smallTextFormat_.Get(), labelRect, mutedBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        wchar_t value[32] = {};
        int rem = static_cast<int>(std::ceil(remaining));
        if (stopwatch) rem = static_cast<int>(elapsed);  // Bunbun: counts up
        if (rem >= 3600) swprintf_s(value, L"%d:%02d:%02d", rem / 3600, rem / 60 % 60, rem % 60);  // Bunbun
        else swprintf_s(value, L"%d:%02d", rem / 60, rem % 60);
        D2D1_RECT_F valueRect = D2D1::RectF(tx, cy - 1.0f, rect.right - 14.0f, cy + 16.0f);
        textBrush_->SetOpacity(0.95f);
        target_->DrawTextW(value, static_cast<UINT32>(wcslen(value)), textFormat_.Get(),
                           valueRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        textBrush_->SetOpacity(0.90f);
        mutedBrush_->SetOpacity(0.58f);
    }

    void DrawCapsLock(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 110.0f) return;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14, cy - badgeSz * 0.5f,
                                        rect.left + 14 + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        const wchar_t* glyph = nullptr;
        std::wstring label;
        bool isOn = false;

        if (state.capsLock.isNumEvent) {
            glyph = L"1";
            label = Loc(L"Num Lock");
            isOn = state.capsLock.numOn;
        } else {
            glyph = L"A";
            label = Loc(L"Caps Lock");
            isOn = state.capsLock.capsOn;
        }

        // Draw central bold keycap glyph, vertically and horizontally centered
        textBrush_->SetOpacity(0.95f);
        target_->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), clockFormat_.Get(), badge,
                           textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

        // Draw physical glowing status LED inside the keycap at top-right with padding
        ComPtr<ID2D1SolidColorBrush> ledBrush;
        D2D1_COLOR_F ledColor = isOn
            ? D2D1::ColorF(0.19f, 0.83f, 0.38f, 1.0f)   // Green glowing LED for ON
            : D2D1::ColorF(1.0f,  1.0f,  1.0f,  0.22f);  // Dim white for OFF
        target_->CreateSolidColorBrush(ledColor, &ledBrush);

        const float ledR = 2.2f;
        D2D1_POINT_2F ledCenter = D2D1::Point2F(badge.right - 5.5f, badge.top + 5.5f);
        target_->FillEllipse(D2D1::Ellipse(ledCenter, ledR, ledR), ledBrush.Get());

        // Draw label text ("Caps Lock" / "Num Lock") - increased font size and vertically centered
        const float tx = badge.right + 14.0f;
        D2D1_RECT_F labelRect = D2D1::RectF(tx, cy - 9.0f, rect.right - 46.0f, cy + 11.0f);
        textBrush_->SetOpacity(0.95f);
        target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                           textFormat_.Get(), labelRect, textBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        // Draw status string (ON/OFF) - increased font size and vertically centered
        std::wstring status = isOn ? Loc(L"On") : Loc(L"Off");
        D2D1_RECT_F statusRect = D2D1::RectF(rect.right - 44.0f, cy - 9.0f, rect.right - 14.0f, cy + 11.0f);
        if (isOn) {
            textBrush_->SetOpacity(0.95f);
            target_->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), textFormat_.Get(),
                               statusRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
            mutedBrush_->SetOpacity(0.75f);
            target_->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), textFormat_.Get(),
                               statusRect, mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    void DrawDevice(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 100.0f) return;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const bool connected = (state.device.eventType == DeviceEventType::Connected);

        // Badge circle with colored dot
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14, cy - badgeSz * 0.5f,
                                        rect.left + 14 + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        const wchar_t* glyph = usingFluentIcons_ ? L"\uECF0" : L"\uE88E";
        textBrush_->SetOpacity(0.95f);
        if (iconFormat_) {
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), iconFormat_.Get(), badge,
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        textBrush_->SetOpacity(0.90f);

        ComPtr<ID2D1SolidColorBrush> dotBrush;
        D2D1_COLOR_F dotColor = connected
            ? D2D1::ColorF(0.19f, 0.83f, 0.38f, 1.0f)
            : D2D1::ColorF(1.0f,  0.27f, 0.22f, 1.0f);
        target_->CreateSolidColorBrush(dotColor, &dotBrush);

        D2D1_POINT_2F dotCenter = D2D1::Point2F(badge.right - 4.5f, badge.bottom - 4.5f);
        target_->FillEllipse(D2D1::Ellipse(dotCenter, 4.5f, 4.5f), dotBrush.Get());

        const float tx = badge.right + 14.0f;
        const bool showBar = g_settings.statusCountdownProgress;
        const double now = NowSeconds();
        const float ttl = 3.0f;
        const float remaining = Clamp(static_cast<float>(state.device.expiresAt - now), 0.0f, ttl);
        const float progress = remaining / ttl;

        mutedBrush_->SetOpacity(0.50f);
        std::wstring label = connected ? L"Device Connected" : L"Device Removed";
        D2D1_RECT_F labelRect = showBar
            ? D2D1::RectF(tx, cy - 18.0f, rect.right - 14.0f, cy - 2.0f)
            : D2D1::RectF(tx, cy - 16.0f, rect.right - 14.0f, cy - 1.0f);
        target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                           smallTextFormat_.Get(), labelRect, mutedBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        textBrush_->SetOpacity(0.95f);
        const std::wstring& name = state.device.deviceName.empty()
            ? (state.device.isBluetoothLike ? std::wstring(L"Bluetooth") : std::wstring(L"USB Device"))
            : state.device.deviceName;
        D2D1_RECT_F nameRect = showBar
            ? D2D1::RectF(tx, cy - 2.0f, rect.right - 14.0f, cy + 15.0f)
            : D2D1::RectF(tx, cy - 1.0f, rect.right - 14.0f, cy + 16.0f);
        target_->DrawTextW(name.c_str(), static_cast<UINT32>(name.size()),
                           textFormat_.Get(), nameRect, textBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
        textBrush_->SetOpacity(0.90f);

        DrawCountdownProgress(tx, rect.right - 14.0f, rect.bottom - 6.0f, progress);
        mutedBrush_->SetOpacity(0.58f);
    }

    void DrawDoNotDisturb(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 110.0f) return;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14.0f, cy - badgeSz * 0.5f,
                                        rect.left + 14.0f + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        const bool isOn = state.doNotDisturb.enabled;
        const wchar_t* glyph = isOn ? L"\uE7ED" : L"\uEA8F";
        textBrush_->SetOpacity(0.95f);
        if (iconFormat_) {
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), iconFormat_.Get(), badge,
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        ComPtr<ID2D1SolidColorBrush> ledBrush;
        D2D1_COLOR_F ledColor = isOn
            ? D2D1::ColorF(0.19f, 0.83f, 0.38f, 1.0f)
            : D2D1::ColorF(1.0f,  1.0f,  1.0f,  0.22f);
        target_->CreateSolidColorBrush(ledColor, &ledBrush);

        const float ledR = 2.2f;
        D2D1_POINT_2F ledCenter = D2D1::Point2F(badge.right - 5.5f, badge.top + 5.5f);
        target_->FillEllipse(D2D1::Ellipse(ledCenter, ledR, ledR), ledBrush.Get());

        const float tx = badge.right + 14.0f;
        D2D1_RECT_F labelRect = D2D1::RectF(tx, cy - 9.0f, rect.right - 46.0f, cy + 11.0f);
        textBrush_->SetOpacity(0.95f);
        std::wstring label = Loc(L"Do Not Disturb");
        target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                           textFormat_.Get(), labelRect, textBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        std::wstring status = isOn ? Loc(L"On") : Loc(L"Off");
        D2D1_RECT_F statusRect = D2D1::RectF(rect.right - 44.0f, cy - 9.0f, rect.right - 14.0f, cy + 11.0f);
        if (isOn) {
            textBrush_->SetOpacity(0.95f);
            target_->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), textFormat_.Get(),
                               statusRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
            mutedBrush_->SetOpacity(0.75f);
            target_->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), textFormat_.Get(),
                               statusRect, mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        const double now = NowSeconds();
        const float ttl = 3.0f;
        const float remaining = Clamp(static_cast<float>(state.doNotDisturb.expiresAt - now), 0.0f, ttl);
        const float progress = remaining / ttl;
        DrawCountdownProgress(tx, rect.right - 14.0f, rect.bottom - 4.0f, progress);
        mutedBrush_->SetOpacity(0.58f);
        textBrush_->SetOpacity(0.90f);
    }

    void DrawBluetoothCategoryIcon(D2D1_RECT_F badge, BluetoothDeviceCategory category) {
        const wchar_t* glyph = L"\uE702";
        switch (category) {
            case BluetoothDeviceCategory::Headphones:
                glyph = L"\uE7F6";
                break;
            case BluetoothDeviceCategory::Speaker:
                glyph = L"\uE7F5";
                break;
            case BluetoothDeviceCategory::Mouse:
                glyph = L"\uE962";
                break;
            case BluetoothDeviceCategory::Keyboard:
                glyph = L"\uE92E";
                break;
            case BluetoothDeviceCategory::Phone:
                glyph = L"\uE8EA";
                break;
            case BluetoothDeviceCategory::Generic:
            default:
                glyph = L"\uE702";
                break;
        }

        textBrush_->SetOpacity(0.95f);
        if (iconFormat_) {
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), iconFormat_.Get(), badge,
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        textBrush_->SetOpacity(0.90f);
    }

    void DrawBluetoothDevice(const SharedState& state, D2D1_RECT_F rect) {
        if (rect.bottom - rect.top < 24.0f || rect.right - rect.left < 140.0f) return;
        const double now = NowSeconds();
        const float ttl = 4.0f;
        const float remaining = Clamp(static_cast<float>(state.bluetoothDevice.expiresAt - now), 0.0f, ttl);
        const float progress = remaining / ttl;

        const bool connected = state.bluetoothDevice.connected;
        const int battery = state.bluetoothDevice.batteryPercent;
        const bool hasBattery = g_settings.bluetoothShowBattery && battery >= 0;

        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float badgeSz = (rect.bottom - rect.top) - 16.0f;
        D2D1_RECT_F badge = D2D1::RectF(rect.left + 14, cy - badgeSz * 0.5f,
                                        rect.left + 14 + badgeSz, cy + badgeSz * 0.5f);
        const float br = badgeSz * 0.35f;

        ComPtr<ID2D1SolidColorBrush> badgeBg;
        target_->CreateSolidColorBrush(material_.raisedStrong, &badgeBg);
        target_->FillRoundedRectangle(D2D1::RoundedRect(badge, br, br), badgeBg.Get());

        DrawBluetoothCategoryIcon(badge, state.bluetoothDevice.category);

        ComPtr<ID2D1SolidColorBrush> dotBrush;
        D2D1_COLOR_F dotColor = connected
            ? D2D1::ColorF(0.19f, 0.83f, 0.38f, 1.0f)
            : D2D1::ColorF(1.0f, 0.27f, 0.22f, 1.0f);
        target_->CreateSolidColorBrush(dotColor, &dotBrush);
        D2D1_POINT_2F dotCenter = D2D1::Point2F(badge.right - 4.5f, badge.bottom - 4.5f);
        target_->FillEllipse(D2D1::Ellipse(dotCenter, 4.5f, 4.5f), dotBrush.Get());

        const float tx = badge.right + 14.0f;
        const bool showBar = g_settings.statusCountdownProgress;
        const float rightEdge = hasBattery ? rect.right - 62.0f : rect.right - 14.0f;

        mutedBrush_->SetOpacity(0.50f);
        std::wstring label = std::wstring(Loc(L"Bluetooth")) + L" " + (connected ? Loc(L"Connected") : Loc(L"Disconnected"));
        D2D1_RECT_F labelRect = showBar
            ? D2D1::RectF(tx, cy - 18.0f, rightEdge, cy - 2.0f)
            : D2D1::RectF(tx, cy - 16.0f, rightEdge, cy - 1.0f);
        target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                           smallTextFormat_.Get(), labelRect, mutedBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        textBrush_->SetOpacity(0.95f);
        const std::wstring& name = state.bluetoothDevice.deviceName.empty()
            ? std::wstring(L"Bluetooth Device")
            : state.bluetoothDevice.deviceName;
        D2D1_RECT_F nameRect = showBar
            ? D2D1::RectF(tx, cy - 2.0f, rightEdge, cy + 15.0f)
            : D2D1::RectF(tx, cy - 1.0f, rightEdge, cy + 16.0f);
        target_->DrawTextW(name.c_str(), static_cast<UINT32>(name.size()),
                           textFormat_.Get(), nameRect, textBrush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);

        if (hasBattery) {
            const float colCenter = rect.right - 31.0f;
            const float bw = 16.0f;
            const float bh = 8.5f;
            const float nubW = 1.6f;
            const float totalW = bw + nubW;
            const float batLeft = colCenter - totalW * 0.5f;
            const float batY = showBar ? cy - 10.0f : cy - 9.0f;

            D2D1_RECT_F batRect = D2D1::RectF(batLeft, batY - bh * 0.5f,
                                              batLeft + bw, batY + bh * 0.5f);
            ComPtr<ID2D1SolidColorBrush> batBorder;
            target_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.75f), &batBorder);
            target_->DrawRoundedRectangle(D2D1::RoundedRect(batRect, 1.5f, 1.5f), batBorder.Get(), 1.2f);
            D2D1_RECT_F nub = D2D1::RectF(batRect.right, batY - 2.0f, batRect.right + nubW, batY + 2.0f);
            target_->FillRectangle(nub, batBorder.Get());

            const float pct = Clamp(battery / 100.0f, 0.0f, 1.0f);
            D2D1_RECT_F fill = D2D1::RectF(batRect.left + 1.5f, batRect.top + 1.5f,
                                           batRect.left + 1.5f + (bw - 3.0f) * pct, batRect.bottom - 1.5f);
            ComPtr<ID2D1SolidColorBrush> fillBrush;
            D2D1_COLOR_F fillColor = battery <= 20 ? D2D1::ColorF(1.0f, 0.23f, 0.18f, 1.0f)
                                                    : D2D1::ColorF(1, 1, 1, 0.92f);
            target_->CreateSolidColorBrush(fillColor, &fillBrush);
            target_->FillRoundedRectangle(D2D1::RoundedRect(fill, 0.8f, 0.8f), fillBrush.Get());

            wchar_t pctBuf[16] = {};
            swprintf_s(pctBuf, L"%d%%", battery);
            D2D1_RECT_F pctRect = showBar
                ? D2D1::RectF(colCenter - 25.0f, cy - 2.0f, colCenter + 25.0f, cy + 14.0f)
                : D2D1::RectF(colCenter - 25.0f, cy - 1.0f, colCenter + 25.0f, cy + 15.0f);
            textBrush_->SetOpacity(0.85f);
            if (smallTextFormat_) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                target_->DrawTextW(pctBuf, static_cast<UINT32>(wcslen(pctBuf)), smallTextFormat_.Get(),
                                   pctRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }

        DrawCountdownProgress(tx, rect.right - 14.0f, rect.bottom - 6.0f, progress);
        mutedBrush_->SetOpacity(0.58f);
        textBrush_->SetOpacity(0.90f);
    }

