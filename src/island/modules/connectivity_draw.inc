// Bunbun: the Connectivity tab's drawing. Included inside the Renderer class body,
// so these are member functions.

    // ── Bunbun: Connectivity tab (Wi-Fi / Ethernet, Bluetooth, Do Not Disturb, Battery) ──
    // Click a card to toggle it (Wi-Fi, Bluetooth), hold Wi-Fi or Bluetooth for
    // its list, right-click for its Windows Settings page. See the input handlers
    // in connectivity_module.inc.
    void DrawConnectivityDashboard(const SharedState& state, D2D1_RECT_F rect, float scale) {
        using namespace ConnectivityLayout;
        const float padX = kPadX * scale;

        // A list left open closes once the tab has been off screen for a moment.
        const double drawnAt = NowSeconds();
        if (g_connectivityList >= 0 && drawnAt - g_connectivityLastDrawnAt > 0.75) CloseConnectivityList();
        g_connectivityLastDrawnAt = drawnAt;
        if (g_connectivityList >= 0) {
            DrawConnectivityList(state, rect, scale);
            return;
        }

        textBrush_->SetOpacity(0.96f);
        const wchar_t* title = Loc(L"Connectivity");
        if (boldTextFormat_) {
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            target_->DrawTextW(title, static_cast<UINT32>(wcslen(title)), boldTextFormat_.Get(),
                               D2D1::RectF(rect.left + padX, rect.top + 16.0f * scale,
                                           rect.right - padX, rect.top + 32.0f * scale),
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        const ConnectivitySnapshot& c = state.connectivity;
        const bool dnd = g_isDnDActive.load(std::memory_order_relaxed);
        const wchar_t* waiting = L"…";

        struct Card {
            int glyph;
            std::wstring label, value, detail;
            bool on;
        };
        Card cards[kMaxCards];
        const int cardCount = c.hasBattery ? 4 : 3;
        g_connectivityCards = cardCount;

        // Network: Wi-Fi first; Ethernet takes over when it's the only link.
        if (!c.wifiConnected && c.ethernetConnected) {
            cards[0] = {13, Loc(L"ETHERNET"), Loc(L"Connected"), Loc(L"Wired network"), true};
        } else {
            Card& w = cards[0];
            w.glyph = 10;
            w.label = Loc(L"WI-FI");
            w.on = c.wifiConnected;
            if (!c.sampled) {
                w.value = waiting;
            } else if (c.wifiConnected) {
                w.value = c.wifiName.empty() ? Loc(L"Connected") : c.wifiName;
                wchar_t buf[48];
                swprintf_s(buf, L"%s %d%%", Loc(L"Signal"), c.wifiSignal);
                w.detail = buf;
            } else {
                w.value = c.wifiRadio == 1 ? Loc(L"Not connected") : c.wifiRadio == 0 ? Loc(L"Off") : Loc(L"No adapter");
            }
        }

        {
            Card& b = cards[1];
            b.glyph = 11;
            b.label = Loc(L"BLUETOOTH");
            b.on = c.bluetoothRadio == 1;
            if (!c.sampled) {
                b.value = waiting;
            } else if (c.bluetoothRadio < 0) {
                b.value = Loc(L"Unavailable");
            } else if (c.bluetoothRadio == 0) {
                b.value = Loc(L"Off");
            } else if (c.bluetoothDevices.empty()) {
                b.value = Loc(L"On");
                b.detail = Loc(L"No devices");
            } else {
                b.value = c.bluetoothDevices.front();
                if (c.bluetoothDevices.size() > 1) {
                    wchar_t buf[48];
                    swprintf_s(buf, L"+%d %s", static_cast<int>(c.bluetoothDevices.size() - 1), Loc(L"more"));
                    b.detail = buf;
                } else {
                    b.detail = Loc(L"Connected");
                }
            }
        }

        cards[2] = {12, Loc(L"DO NOT DISTURB"), dnd ? Loc(L"On") : Loc(L"Off"),
                    dnd ? Loc(L"Alerts silenced") : Loc(L"Alerts on"), dnd};

        // Bunbun: battery, on laptops only. Plugged in is not the same as charging:
        // many laptops hold the battery at a limit, so that case says so.
        if (cardCount == 4) {
            Card& p = cards[3];
            p.label = Loc(L"BATTERY");
            wchar_t pct[16];
            swprintf_s(pct, L"%d%%", c.batteryPercent);
            if (c.charging) {
                p = {15, p.label, Loc(L"Charging"), pct, true};
                // Time to full from the charge rate, when the firmware reports it.
                if (c.chargeRateMw > 0 && c.fullMwh > c.remainingMwh) {
                    const int minutes = static_cast<int>((c.fullMwh - c.remainingMwh) * 60LL / c.chargeRateMw);
                    if (minutes > 0 && minutes < 24 * 60) {
                        wchar_t buf[64];
                        swprintf_s(buf, L"%s · %s %dh %02dm", pct, Loc(L"full in"), minutes / 60, minutes % 60);
                        p.detail = buf;
                    }
                }
            } else if (c.onPower && c.batteryPercent >= 95) {
                p = {16, p.label, Loc(L"Fully charged"), Loc(L"On power"), true};
            } else if (c.onPower) {
                // Plugged in but not charging: the laptop is holding the battery at a
                // limit (battery care / smart charging) to slow its wear.
                p = {16, p.label, Loc(L"Plugged in"), std::wstring(Loc(L"Held at")) + L" " + pct + L" · " + Loc(L"battery care"), true};
            } else {
                p.glyph = 14;
                p.value = pct;
                p.on = false;
                if (c.batterySaver) {
                    p.detail = Loc(L"Battery saver on");
                } else if (c.batterySecondsLeft != BATTERY_LIFE_UNKNOWN && c.batterySecondsLeft > 0) {
                    wchar_t left[48];
                    const DWORD minutes = c.batterySecondsLeft / 60;
                    swprintf_s(left, L"%luh %02lum %s", minutes / 60, minutes % 60, Loc(L"left"));
                    p.detail = left;
                } else {
                    p.detail = Loc(L"On battery");
                }
            }
            // Hovering shows wear: today's full charge against the design capacity.
            if (g_hoveredConnectivityCard.load(std::memory_order_relaxed) == 3 && c.designMwh > 0 && c.fullMwh > 0) {
                wchar_t buf[64];
                swprintf_s(buf, L"%s %d%%", Loc(L"Health"),
                           static_cast<int>(std::lround(std::min(100.0, c.fullMwh * 100.0 / c.designMwh))));
                p.value = buf;
                swprintf_s(buf, L"%.0f %s %.0f Wh", c.fullMwh / 1000.0, Loc(L"of"), c.designMwh / 1000.0);
                p.detail = buf;
            }
        }

        const int hovered = g_hoveredConnectivityCard.load(std::memory_order_relaxed);
        const bool compact = cardCount > 3;  // 2x2 grid (see ConnectivityCardRect)
        const float contentW = (rect.right - rect.left) / scale;

        for (int i = 0; i < cardCount; ++i) {
            const Card& k = cards[i];
            const D2D1_RECT_F r = ConnectivityCardRect(i, cardCount, contentW);
            const D2D1_RECT_F card = D2D1::RectF(rect.left + r.left * scale, rect.top + r.top * scale,
                                                 rect.left + r.right * scale, rect.top + r.bottom * scale);
            DrawCard(card, 11.0f * scale);
            if (i == hovered) {
                if (ID2D1SolidColorBrush* hi = AcquireBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f))) {
                    target_->FillRoundedRectangle(D2D1::RoundedRect(card, 11.0f * scale, 11.0f * scale), hi);
                }
            }

            if (compact) {
                DrawCompactConnectivityCard(card, k.glyph, k.label, k.value, k.detail, k.on, scale);
                continue;
            }

            // Icon bubble: accent when on, neutral when off.
            const D2D1_POINT_2F ic = D2D1::Point2F(card.left + 24.0f * scale, card.top + 24.0f * scale);
            const D2D1_COLOR_F tint = k.on ? material_.accent : material_.raisedStrong;
            if (ID2D1SolidColorBrush* bubble = AcquireBrush(WithAlpha(tint, k.on ? 0.22f : 0.9f))) {
                target_->FillEllipse(D2D1::Ellipse(ic, 15.0f * scale, 15.0f * scale), bubble);
            }
            const D2D1_COLOR_F glyphColor = k.on ? material_.accent : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f);
            if (ID2D1SolidColorBrush* glyphBrush = AcquireBrush(WithAlpha(glyphColor, 0.95f * settingsOpacity_))) {
                DrawMetricGlyph(ic, 17.0f * scale, k.glyph, glyphBrush);
            }

            const float textLeft = card.left + 12.0f * scale;
            const float textRight = card.right - 10.0f * scale;
            if (smallTextFormat_) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                mutedBrush_->SetOpacity(0.60f);
                target_->DrawTextW(k.label.c_str(), static_cast<UINT32>(k.label.size()), smallTextFormat_.Get(),
                                   D2D1::RectF(textLeft, card.top + 48.0f * scale, textRight, card.top + 62.0f * scale),
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (textFormat_) {
                textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                textBrush_->SetOpacity(0.95f);
                target_->DrawTextW(k.value.c_str(), static_cast<UINT32>(k.value.size()), textFormat_.Get(),
                                   D2D1::RectF(textLeft, card.top + 64.0f * scale, textRight, card.top + 84.0f * scale),
                                   textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            }
            if (smallTextFormat_ && !k.detail.empty()) {
                mutedBrush_->SetOpacity(0.70f);
                target_->DrawTextW(k.detail.c_str(), static_cast<UINT32>(k.detail.size()), smallTextFormat_.Get(),
                                   D2D1::RectF(textLeft, card.top + 88.0f * scale, textRight, card.top + 104.0f * scale),
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }

        textBrush_->SetOpacity(0.96f);
        mutedBrush_->SetOpacity(0.75f);
    }

    // A compact card for the 2x2 grid: icon bubble on the left, then label, value
    // and detail stacked beside it.
    void DrawCompactConnectivityCard(D2D1_RECT_F card, int glyph, const std::wstring& label, const std::wstring& value,
                                     const std::wstring& detail, bool on, float scale) {
        const D2D1_POINT_2F ic = D2D1::Point2F(card.left + 21.0f * scale, (card.top + card.bottom) * 0.5f);
        const D2D1_COLOR_F tint = on ? material_.accent : material_.raisedStrong;
        if (ID2D1SolidColorBrush* bubble = AcquireBrush(WithAlpha(tint, on ? 0.22f : 0.9f))) {
            target_->FillEllipse(D2D1::Ellipse(ic, 13.0f * scale, 13.0f * scale), bubble);
        }
        const D2D1_COLOR_F glyphColor = on ? material_.accent : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f);
        if (ID2D1SolidColorBrush* glyphBrush = AcquireBrush(WithAlpha(glyphColor, 0.95f * settingsOpacity_))) {
            DrawMetricGlyph(ic, 15.0f * scale, glyph, glyphBrush);
        }
        const float left = card.left + 41.0f * scale, right = card.right - 8.0f * scale;
        const float top = card.top + 5.0f * scale;
        if (smallTextFormat_) {
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            smallTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            mutedBrush_->SetOpacity(0.60f);
            target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), smallTextFormat_.Get(),
                               D2D1::RectF(left, top, right, top + 13.0f * scale), mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            textBrush_->SetOpacity(0.95f);
            target_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), textFormat_.Get(),
                               D2D1::RectF(left, top + 13.0f * scale, right, top + 31.0f * scale), textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
        if (smallTextFormat_ && !detail.empty()) {
            mutedBrush_->SetOpacity(0.70f);
            target_->DrawTextW(detail.c_str(), static_cast<UINT32>(detail.size()), smallTextFormat_.Get(),
                               D2D1::RectF(left, top + 31.0f * scale, right, top + 45.0f * scale), mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (smallTextFormat_) smallTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }

    // The long-press list: Wi-Fi networks or paired Bluetooth devices. A row
    // connects or disconnects; the header has Back and a Settings link.
    void DrawConnectivityList(const SharedState& state, D2D1_RECT_F rect, float scale) {
        using namespace ConnectivityLayout;
        const ConnectivitySnapshot& c = state.connectivity;
        const bool wifi = g_connectivityList == 0;
        const float padX = kPadX * scale;

        // Header: "‹ Wi-Fi networks" and a "Settings" link on the right.
        std::wstring title = std::wstring(L"‹  ") + (wifi ? Loc(L"Wi-Fi networks") : Loc(L"Bluetooth devices"));
        if (boldTextFormat_) {
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            textBrush_->SetOpacity(0.96f);
            target_->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), boldTextFormat_.Get(),
                               D2D1::RectF(rect.left + padX, rect.top + 14.0f * scale, rect.right - padX, rect.top + 32.0f * scale),
                               textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (smallTextFormat_) {
            const wchar_t* link = Loc(L"Settings");
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            if (ID2D1SolidColorBrush* accent = AcquireBrush(WithAlpha(material_.accent, 0.95f * settingsOpacity_))) {
                target_->DrawTextW(link, static_cast<UINT32>(wcslen(link)), smallTextFormat_.Get(),
                                   D2D1::RectF(rect.right - padX - kSettingsLinkWidth * scale, rect.top + 17.0f * scale,
                                               rect.right - padX, rect.top + 32.0f * scale),
                                   accent, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        struct Row {
            std::wstring name, status;
            bool on;
        };
        std::vector<Row> rows;
        const double now = NowSeconds();
        const bool busyLive = now < g_connectivityBusyUntil;
        if (wifi) {
            for (const WifiNetwork& n : c.wifiNetworks) {
                std::wstring status;
                if (busyLive && n.ssid == g_connectivityBusyName) status = n.connected ? Loc(L"Connected") : Loc(L"Connecting…");
                else if (n.connected) status = Loc(L"Connected");
                else if (!n.profile.empty()) status = Loc(L"Saved");
                else status = n.secured ? Loc(L"Secured") : Loc(L"Open");
                wchar_t sig[16];
                swprintf_s(sig, L" · %d%%", n.signal);
                rows.push_back({n.ssid, status + sig, n.connected});
            }
        } else {
            for (const BluetoothListDevice& d : c.bluetoothList) {
                std::wstring status;
                if (busyLive && d.name == g_connectivityBusyName) status = Loc(L"Working…");
                else if (d.connected) status = d.audio ? Loc(L"Connected · tap to disconnect") : Loc(L"Connected");
                else status = d.audio ? Loc(L"Tap to connect") : Loc(L"Paired");
                rows.push_back({d.name, status, d.connected});
            }
        }

        const float left = rect.left + padX;
        const float right = rect.right - padX;
        if (rows.empty()) {
            const wchar_t* msg = wifi ? (c.wifiRadio == 1 ? Loc(L"Scanning…") : Loc(L"Wi-Fi is off"))
                                      : (c.bluetoothRadio == 1 ? Loc(L"Looking for paired devices…") : Loc(L"Bluetooth is off"));
            if (smallTextFormat_) {
                mutedBrush_->SetOpacity(0.7f);
                target_->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), smallTextFormat_.Get(),
                                   D2D1::RectF(left, rect.top + (kListTop + 6.0f) * scale, right, rect.top + (kListTop + 24.0f) * scale),
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            mutedBrush_->SetOpacity(0.75f);
            return;
        }

        const int hovered = g_hoveredConnectivityCard.load(std::memory_order_relaxed);  // reused as the hovered row
        const int shown = std::min(static_cast<int>(rows.size()), kMaxRows);
        for (int i = 0; i < shown; ++i) {
            const Row& r = rows[static_cast<size_t>(i)];
            const float top = rect.top + (kListTop + i * kRowHeight) * scale;
            const D2D1_RECT_F row = D2D1::RectF(left - 6.0f * scale, top, right + 6.0f * scale, top + (kRowHeight - 3.0f) * scale);
            const float radius = 8.0f * scale;
            if (ID2D1SolidColorBrush* bg = AcquireBrush(r.on ? WithAlpha(material_.accent, 0.16f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, i == hovered ? 0.08f : 0.035f))) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(row, radius, radius), bg);
            }
            const D2D1_POINT_2F ic = D2D1::Point2F(row.left + 14.0f * scale, (row.top + row.bottom) * 0.5f);
            if (ID2D1SolidColorBrush* glyph = AcquireBrush(r.on ? material_.accent : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f))) {
                DrawMetricGlyph(ic, 13.0f * scale, wifi ? 10 : 11, glyph);
            }
            const float mid = row.left + (row.right - row.left) * 0.55f;
            if (textFormat_) {
                textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                textBrush_->SetOpacity(0.95f);
                target_->DrawTextW(r.name.c_str(), static_cast<UINT32>(r.name.size()), textFormat_.Get(),
                                   D2D1::RectF(row.left + 28.0f * scale, row.top + 2.0f * scale, mid, row.bottom),
                                   textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            }
            if (smallTextFormat_) {
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                mutedBrush_->SetOpacity(0.72f);
                target_->DrawTextW(r.status.c_str(), static_cast<UINT32>(r.status.size()), smallTextFormat_.Get(),
                                   D2D1::RectF(mid, row.top + 5.0f * scale, row.right - 10.0f * scale, row.bottom),
                                   mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
        textBrush_->SetOpacity(0.96f);
        mutedBrush_->SetOpacity(0.75f);
    }
