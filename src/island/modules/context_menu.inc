// Bunbun: Right-click menu and media source menu commands.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

void ShowContextMenu(HWND hwnd, POINT screenPoint) {
    bool timerActive = false;
    bool timerRunning = false;
    {
        std::lock_guard lock(g_stateMutex);
        timerActive = g_state.timer.active;
        timerRunning = g_state.timer.running;
    }

    // Bunbun: a dark menu with icons, grouped: quick actions, the live bits (timer,
    // music, tray), lyrics, appearance, settings. Command ids are unchanged except
    // the new ones in the 44-59 range (see the switch below).
    UseDarkMenus();
    HMENU menu = CreatePopupMenu();
    auto add = [](HMENU m, UINT flags, UINT_PTR id, const wchar_t* text, wchar_t glyph = 0) {
        AppendMenuW(m, flags, id, text);
        if (glyph) SetMenuGlyph(m, GetMenuItemCount(m) - 1, glyph);
    };
    auto submenu = [&](HMENU parent, HMENU child, const wchar_t* text, wchar_t glyph) {
        add(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(child), text, glyph);
    };

    // ── Quick actions ──
    add(menu, MF_STRING, 46, L"Pick a colour on screen", kGlyphEyedropper);
    add(menu, MF_STRING, 47, g_tucked.load() ? L"Bring the island back" : L"Tuck away\tCtrl+Alt+H", kGlyphTuck);
    add(menu, MF_STRING | (g_pinnedExpanded.load() ? MF_CHECKED : 0), 2, L"Keep open", kGlyphPin);
    add(menu, MF_STRING, 1, L"Dismiss alerts", kGlyphDismiss);
    add(menu, MF_STRING, 40, g_manuallyHidden.load() ? L"Show the island" : L"Hide the island", kGlyphHide);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // ── Timer, music, tray ──
    HMENU timerMenu = CreatePopupMenu();
    AppendMenuW(timerMenu, MF_STRING, 30, L"Focus for 25 min");
    AppendMenuW(timerMenu, MF_STRING, 31, L"Focus for 50 min");
    AppendMenuW(timerMenu, MF_STRING, 32, L"Break for 5 min");
    if (timerActive) {
        AppendMenuW(timerMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(timerMenu, MF_STRING, 33, timerRunning ? L"Pause" : L"Resume");
        AppendMenuW(timerMenu, MF_STRING, 34, L"Stop");
    }
    submenu(menu, timerMenu, timerActive ? (timerRunning ? L"Focus timer (running)" : L"Focus timer (paused)")
                                         : L"Focus timer", kGlyphTimer);

    std::vector<MediaSourceMenuItem> mediaSourceItems;
    {
        HMENU sourceMenu = CreatePopupMenu();
        mediaSourceItems = AppendMediaSourceItems(sourceMenu);
        submenu(menu, sourceMenu, Loc(L"Media source"), kGlyphMusic);
    }
    if (g_settings.fileTrayModule) {
        size_t trayCount = 0;
        {
            std::lock_guard lock(g_stateMutex);
            trayCount = g_state.fileTrayItems.size();
        }
        HMENU trayMenu = CreatePopupMenu();
        AppendMenuW(trayMenu, MF_STRING, 42, L"Paste from clipboard");
        const std::wstring clearLabel = trayCount == 0
            ? std::wstring(L"Clear tray")
            : L"Clear tray (" + std::to_wstring(trayCount) + L")";
        AppendMenuW(trayMenu, MF_STRING | (trayCount == 0 ? (MF_GRAYED | MF_DISABLED) : 0), 41,
                    clearLabel.c_str());
        submenu(menu, trayMenu, Loc(L"File Tray"), kGlyphTray);
    }

    // ── Lyrics ──
    {
        HMENU lyricsMenu = CreatePopupMenu();
        AppendMenuW(lyricsMenu, MF_STRING | (g_settings.collapsedLyrics ? MF_CHECKED : 0), 13,
                    L"On the small island");
        AppendMenuW(lyricsMenu, MF_STRING | (g_settings.mediaTabLyrics ? MF_CHECKED : 0), 15, L"In the music page");
        AppendMenuW(lyricsMenu,
                    MF_STRING | (Wh_GetIntValue(L"LyricsPinnedFirst", 0) ? MF_CHECKED : 0) |
                        (g_settings.lyrics ? 0 : (MF_GRAYED | MF_DISABLED)),
                    14, L"Lyrics as the first page");
        AppendMenuW(lyricsMenu, MF_SEPARATOR, 0, nullptr);
        {
            // Shows and adjusts whichever profile is active right now.
            const bool bt = g_defaultOutputIsBluetooth.load(std::memory_order_relaxed);
            const int effective = bt ? g_lyricsEffectiveOffsetBtMs.load(std::memory_order_relaxed)
                                     : g_lyricsEffectiveOffsetMs.load(std::memory_order_relaxed);
            wchar_t header[96] = {};
            swprintf_s(header, L"%s: %+d ms", bt ? L"Bluetooth" : L"Speakers", effective);

            HMENU lyricsSyncMenu = CreatePopupMenu();
            AppendMenuW(lyricsSyncMenu, MF_STRING | MF_GRAYED | MF_DISABLED, kLyricsSyncMenuIdBase, header);
            AppendMenuW(lyricsSyncMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(lyricsSyncMenu, MF_STRING, kLyricsSyncMenuIdBase + 1, L"Earlier +10 ms");
            AppendMenuW(lyricsSyncMenu, MF_STRING, kLyricsSyncMenuIdBase + 2, L"Earlier +50 ms");
            AppendMenuW(lyricsSyncMenu, MF_STRING, kLyricsSyncMenuIdBase + 3, L"Earlier +250 ms");
            AppendMenuW(lyricsSyncMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(lyricsSyncMenu, MF_STRING, kLyricsSyncMenuIdBase + 4, L"Later -10 ms");
            AppendMenuW(lyricsSyncMenu, MF_STRING, kLyricsSyncMenuIdBase + 5, L"Later -50 ms");
            AppendMenuW(lyricsSyncMenu, MF_STRING, kLyricsSyncMenuIdBase + 6, L"Later -250 ms");
            AppendMenuW(lyricsSyncMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(lyricsSyncMenu, MF_STRING, kLyricsSyncMenuIdBase + 7, L"Reset this profile");
            AppendMenuW(lyricsMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(lyricsSyncMenu), header);
        }
        bool cacheOn = false;
        {
            std::lock_guard lock(g_settingsMutex);
            cacheOn = g_settings.lyricsCache && g_settings.lyricsCacheMaxMB > 0;
        }
        // Counters are maintained by the lyrics thread; nothing is scanned here.
        std::wstring cacheLabel = L"Clear saved lyrics";
        if (g_lyricsCacheCountsValid.load(std::memory_order_relaxed)) {
            const int songs = g_lyricsCacheFileCount.load(std::memory_order_relaxed);
            const uint64_t bytes = g_lyricsCacheBytes.load(std::memory_order_relaxed);
            wchar_t detail[64] = {};
            if (bytes >= 1048576ull) {
                swprintf_s(detail, L" (%d songs, %.1f MB)", songs, static_cast<double>(bytes) / 1048576.0);
            } else {
                swprintf_s(detail, L" (%d songs, %llu KB)", songs,
                           static_cast<unsigned long long>((bytes + 1023ull) / 1024ull));
            }
            cacheLabel += detail;
        }
        AppendMenuW(lyricsMenu, MF_STRING | (cacheOn ? 0 : (MF_GRAYED | MF_DISABLED)), 43, cacheLabel.c_str());
        submenu(menu, lyricsMenu, L"Lyrics", kGlyphLyrics);
    }

    // ── Appearance ──
    {
        HMENU look = CreatePopupMenu();
        AppendMenuW(look, MF_STRING, 48, L"Classic layout");
        AppendMenuW(look, MF_STRING, 49, L"Studio layout");
        CheckMenuRadioItem(look, 48, 49, g_settings.studioLayout ? 49 : 48, MF_BYCOMMAND);
        AppendMenuW(look, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(look, MF_STRING, 53, L"Mascot when idle");
        AppendMenuW(look, MF_STRING, 54, L"Eyes when idle");
        CheckMenuRadioItem(look, 53, 54, g_settings.idleEyes ? 54 : 53, MF_BYCOMMAND);
        AppendMenuW(look, MF_SEPARATOR, 0, nullptr);

        // g_settings already reflects any right-click override, so it is the
        // source of truth for which style is active.
        const int currentShape = g_settings.notchStyle ? 2 : (g_settings.w11Style ? 1 : 0);
        HMENU shapeMenu = CreatePopupMenu();
        AppendMenuW(shapeMenu, MF_STRING, kShapeMenuIdBase, L"Pill");
        AppendMenuW(shapeMenu, MF_STRING, kShapeMenuIdBase + 1, L"Windows 11 flyout");
        if (IsBottomPosition(g_settings.position)) {
            AppendMenuW(shapeMenu, MF_STRING | MF_GRAYED | MF_DISABLED, kShapeMenuIdBase + 2,
                        L"Notch (not at the bottom)");
        } else {
            AppendMenuW(shapeMenu, MF_STRING, kShapeMenuIdBase + 2, L"Notch");
        }
        CheckMenuRadioItem(shapeMenu, kShapeMenuIdBase, kShapeMenuIdBase + 2,
                           kShapeMenuIdBase + static_cast<UINT>(currentShape), MF_BYCOMMAND);
        AppendMenuW(look, MF_POPUP, reinterpret_cast<UINT_PTR>(shapeMenu), L"Shape");

        const int activeOpacity = g_pillOpacityOverride.load();
        HMENU opacityMenu = CreatePopupMenu();
        AppendMenuW(opacityMenu, MF_STRING | (activeOpacity == 100 ? MF_CHECKED : 0), 4, L"100% (opaque)");
        AppendMenuW(opacityMenu, MF_STRING | (activeOpacity == 85 ? MF_CHECKED : 0), 5, L"85%");
        AppendMenuW(opacityMenu, MF_STRING | (activeOpacity == 70 ? MF_CHECKED : 0), 6, L"70%");
        AppendMenuW(opacityMenu, MF_STRING | (activeOpacity == 55 ? MF_CHECKED : 0), 7, L"55%");
        AppendMenuW(opacityMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(opacityMenu, MF_STRING | (activeOpacity < 0 ? MF_CHECKED : 0), 8, L"Theme default");
        AppendMenuW(look, MF_POPUP, reinterpret_cast<UINT_PTR>(opacityMenu), L"Transparency");

        // Color theme presets. Built from kThemePalettes and nested in a submenu:
        // nine palettes listed flat would dominate the menu.
        const int activeTheme = Wh_GetIntValue(kThemeValueName, static_cast<int>(g_settings.themePreset));
        HMENU themeMenu = CreatePopupMenu();
        for (int i = 0; i < kCustomThemeIndex; ++i) {
            AppendMenuW(themeMenu, MF_STRING | (activeTheme == i ? MF_CHECKED : 0),
                        kThemeMenuIdBase + static_cast<UINT>(i), kThemePalettes[i].label);
        }
        AppendMenuW(themeMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(themeMenu, MF_STRING | (activeTheme >= kCustomThemeIndex ? MF_CHECKED : 0),
                    kThemeMenuIdBase + static_cast<UINT>(kCustomThemeIndex), L"Custom colours");
        AppendMenuW(look, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"Theme");
        AppendMenuW(look, MF_SEPARATOR, 0, nullptr);

        const int activeExpandOnHover = Wh_GetIntValue(L"ExpandOnHoverOverride", -1) >= 0
                              ? Wh_GetIntValue(L"ExpandOnHoverOverride", 0)
                              : (Wh_GetIntSetting(L"Behavior.ExpandOnHover") != 0);
        AppendMenuW(look, MF_STRING | (activeExpandOnHover ? MF_CHECKED : 0), 11, L"Open on hover");
        AppendMenuW(look, MF_STRING | (g_gameOverlayPinned.load() ? MF_CHECKED : 0), 3, L"Game overlay");
        submenu(menu, look, L"Appearance", kGlyphAppearance);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(menu, MF_STRING, 9, L"Settings\x2026", kGlyphSettings);

    SetForegroundWindow(hwnd);
    DwmBackdrop::EnsureBehind(hwnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                   screenPoint.x, screenPoint.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DwmBackdrop::EnsureBehind(hwnd);
    g_layoutDirty = true;

    // Theme ids are a contiguous range rather than individual cases, so adding a
    // palette to kThemePalettes needs no change here.
    if (cmd >= kThemeMenuIdBase &&
        cmd <= kThemeMenuIdBase + static_cast<UINT>(kCustomThemeIndex)) {
        Wh_SetIntValue(kThemeValueName, static_cast<int>(cmd - kThemeMenuIdBase));
        LoadSettings();
        return;
    }

    if (cmd >= kMediaSourceMenuIdBase) {
        ApplyMediaSourceMenuCommand(cmd, mediaSourceItems);
        return;
    }

    switch (cmd) {
        case 1:
            DismissTransientState();
            g_clickExpanded = false;
            g_layoutDirty = true;
            TriggerNudge();
            break;
        case 2: {
            const bool nextVal = !g_pinnedExpanded.load();
            g_pinnedExpanded.store(nextVal);
            Wh_SetIntValue(L"PinnedExpanded", nextVal ? 1 : 0);
            break;
        }
        case 3: {
            const bool nextVal = !g_gameOverlayPinned.load();
            g_gameOverlayPinned.store(nextVal);
            Wh_SetIntValue(L"GameOverlayPinned", nextVal ? 1 : 0);
            break;
        }
        case 4:
            g_pillOpacityOverride.store(100);
            Wh_SetIntValue(L"PillOpacityOverride", 100);
            LoadSettings();
            break;
        case 5:
            g_pillOpacityOverride.store(85);
            Wh_SetIntValue(L"PillOpacityOverride", 85);
            LoadSettings();
            break;
        case 6:
            g_pillOpacityOverride.store(70);
            Wh_SetIntValue(L"PillOpacityOverride", 70);
            LoadSettings();
            break;
        case 7:
            g_pillOpacityOverride.store(55);
            Wh_SetIntValue(L"PillOpacityOverride", 55);
            LoadSettings();
            break;
        case 8:
            g_pillOpacityOverride.store(-1);
            Wh_SetIntValue(L"PillOpacityOverride", -1);
            LoadSettings();
            break;
        case 9: {
            wchar_t appPath[MAX_PATH] = {};
            const DWORD pathLen = GetModuleFileNameW(nullptr, appPath, ARRAYSIZE(appPath));
            if (pathLen > 0 && pathLen < ARRAYSIZE(appPath)) {
                wchar_t* lastSlash = wcsrchr(appPath, L'\\');
                if (lastSlash) {
                    *(lastSlash + 1) = L'\0';
                    std::wstring settingsExe = std::wstring(appPath) + L"bunbun-settings.exe";
                    ShellExecuteW(nullptr, L"open", settingsExe.c_str(), nullptr, appPath, SW_SHOWNORMAL);
                }
            }
            break;
        }
        case kShapeMenuIdBase:
        case kShapeMenuIdBase + 1:
        case kShapeMenuIdBase + 2: {
            const bool wantW11 = (cmd == kShapeMenuIdBase + 1);
            const bool wantNotch = (cmd == kShapeMenuIdBase + 2);
            if (wantNotch && IsBottomPosition(g_settings.position)) {
                break;  // a bottom-anchored island can't be a top-edge notch
            }
            // Both overrides are written every time, so exactly one style is active.
            Wh_SetIntValue(L"W11StyleOverride", wantW11 ? 1 : 0);
            Wh_SetIntValue(L"NotchStyleOverride", wantNotch ? 1 : 0);
            LoadSettings();
            g_layoutDirty = true;
            TriggerNudge();
            break;
        }
        case 11: {
            const int activeExpandOnHover = Wh_GetIntValue(L"ExpandOnHoverOverride", -1) >= 0
                                  ? Wh_GetIntValue(L"ExpandOnHoverOverride", 0)
                                  : (Wh_GetIntSetting(L"Behavior.ExpandOnHover") != 0);
            Wh_SetIntValue(L"ExpandOnHoverOverride", activeExpandOnHover ? 0 : 1);
            LoadSettings();
            g_layoutDirty = true;
            break;
        }
        case 13: {
            // Bunbun: the same switch as Settings (Bunbun.PillLyrics); off removes the key.
            WriteBunbunSetting(L"PillLyrics", g_settings.collapsedLyrics ? L"-" : L"true");
            Wh_SetIntValue(L"CollapsedLyrics", 0);
            LoadSettings();
            g_layoutDirty = true;
            TriggerNudge();
            break;
        }
        case 14: {
            Wh_SetIntValue(L"LyricsPinnedFirst", Wh_GetIntValue(L"LyricsPinnedFirst", 0) ? 0 : 1);
            LoadSettings();
            g_idleTab = 0;  // back to the (new) first tab
            g_layoutDirty = true;
            TriggerNudge();
            break;
        }
        case 15: {
            const int mediaTabLyricsSetting = Wh_GetIntSetting(L"Modules.MediaTabLyrics") != 0 ? 1 : 0;
            Wh_SetIntValue(L"MediaTabLyricsOverride", GetSettingsCopy().mediaTabLyrics ? 0 : 1);
            Wh_SetIntValue(L"MediaTabLyricsOverrideBase", mediaTabLyricsSetting);
            LoadSettings();
            g_layoutDirty = true;
            TriggerNudge();
            break;
        }
        case 30:
            StartFocusTimer(25, false);
            break;
        case 46:  // Bunbun: colour picker
            ColorPicker::Start();
            break;
        case 48:  // Bunbun: Classic layout (the default: remove the key)
            WriteBunbunSetting(L"Layout", L"-");
            break;
        case 49:  // Bunbun: Studio layout
            WriteBunbunSetting(L"Layout", L"studio");
            break;
        case 53:  // Bunbun: mascot when idle (the default)
            WriteBunbunSetting(L"IdleFace", L"-");
            break;
        case 54:  // Bunbun: eyes when idle
            WriteBunbunSetting(L"IdleFace", L"eyes");
            break;
        case 47:  // Bunbun: tuck away / bring back
            SetTucked(!g_tucked.load());
            break;
        case 31:
            StartFocusTimer(50, false);
            break;
        case 32:
            StartFocusTimer(5, true);
            break;
        case 33:
            ToggleTimerPause();
            break;
        case 34:
            StopFocusTimer();
            break;
        case 40: {
            // Only reachable while visible (a hidden window can't be
            // right-clicked), so in practice this toggles hidden -> the
            // hotkey is the only way back. That's intentional; see readme.
            const bool nowHidden = !g_manuallyHidden.load();
            g_manuallyHidden = nowHidden;
            Wh_SetIntValue(L"ManuallyHidden", nowHidden ? 1 : 0);
            if (nowHidden) {
                g_hotkeyUnhideUntil.store(0.0);
            }
            g_layoutDirty = true;
            break;
        }
        case kLyricsSyncMenuIdBase + 1:
        case kLyricsSyncMenuIdBase + 2:
        case kLyricsSyncMenuIdBase + 3:
        case kLyricsSyncMenuIdBase + 4:
        case kLyricsSyncMenuIdBase + 5:
        case kLyricsSyncMenuIdBase + 6: {
            static const int kLyricsSteps[] = {10, 50, 250, -10, -50, -250};
            AdjustLyricsNudge(kLyricsSteps[cmd - kLyricsSyncMenuIdBase - 1]);
            break;
        }
        case kLyricsSyncMenuIdBase + 7:
            ResetLyricsNudge();
            break;
        case 43:
            // Only sets a flag: the lyrics thread does the deletion (never the render thread)
            // and resets the counters. Lyrics currently on screen are left alone.
            g_lyricsCacheClearRequested.store(true);
            break;
        case 42:
            PasteIntoFileTray(hwnd);
            break;
        case 41: {
            // Clear the File Tray. Only the island's references are dropped --
            // nothing on disk is touched.
            {
                std::lock_guard lock(g_stateMutex);
                g_state.fileTrayItems.clear();
            }
            SaveFileTray();
            g_hoveredFileTrayRow = -1;
            g_layoutDirty = true;
            break;
        }
    }
}

// ── Weather iconography ──────────────────────────────────────────────────────
// The dashboard used colour emoji (☀️ ⛅ 🌡️) rendered with ENABLE_COLOR_FONT,
// which clashed badly with the island's monochrome, accent-tinted UI and varied
// in size and baseline between glyphs. These are drawn as vectors instead: they
// inherit the accent colour, scale cleanly, sit on a predictable baseline, and
// can never fall back to a missing-glyph box the way a font-dependent icon can.
