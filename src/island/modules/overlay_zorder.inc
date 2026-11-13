// Bunbun: Overlay z-order, topmost handling and the dock rect.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

void ApplyOverlayZOrder(HWND hwnd, int width, int height) {
    HWND zOrder = g_settings.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;

    // Yield to the screen-clip overlay.
    const bool yieldToClipUi = Backdrop::ClipUiActiveCached();
    SetWindowPos(hwnd, yieldToClipUi ? nullptr : zOrder, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                     (yieldToClipUi ? SWP_NOZORDER : SWP_SHOWWINDOW));
    if (!yieldToClipUi) {
        DwmBackdrop::EnsureBehind(hwnd);
    }
}

// Windows' own shell surfaces are all WS_EX_TOPMOST, and they are *supposed* to
// sit above the island. Raising ourselves over the Start menu, the notification
// centre, the volume OSD, Task View, a popup menu or a tooltip would be a bug,
// not a fix, so these classes are never treated as something to reclaim from.
bool IsShellOwnedWindow(HWND hwnd) {
    wchar_t className[128] = {};
    if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) <= 0) {
        // Unknown, so treat it as shell-owned and leave the z-order alone.
        return true;
    }

    static const wchar_t* const kShellClasses[] = {
        L"Windows.UI.Core.CoreWindow",              // Start, Action Center, Search
        L"Xaml_WindowedPopupClass",                 // WinUI flyouts and popups
        L"Shell_TrayWnd",                           // taskbar
        L"Shell_SecondaryTrayWnd",                  // taskbar on other monitors
        L"TopLevelWindowForOverflowXamlIsland",     // taskbar overflow
        L"MultitaskingViewFrame",                   // Task View
        L"ForegroundStaging",                       // Task View staging
        L"#32768",                                  // popup menus
        L"tooltips_class32",                        // tooltips
        L"NarratorHelperWindow",
    };

    for (const wchar_t* candidate : kShellClasses) {
        if (_wcsicmp(className, candidate) == 0) {
            return true;
        }
    }
    return false;
}

// Re-asserts the island's place in the topmost band when another always-on-top
// window has been raised over it (PowerToys' bar, taskbar mods, other
// overlays). PositionOverlayWindow only runs on a resize or an explicit layout
// change, so without this the island stayed buried until the next one.
//
// Deliberately conservative: shell surfaces are ignored, a slight clip does not
// count, and we only act when we are still in the topmost band ourselves. That
// keeps this from turning into a z-order tug-of-war or from stealing focus
// surfaces away from Windows.
void EnsureTopmost(HWND hwnd, const RECT* pillScreenRect = nullptr) {
    if (!g_settings.alwaysOnTop || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return;
    }
    if (Backdrop::ClipUiActiveCached()) {
        return;
    }

    // If we are not topmost at all, PositionOverlayWindow owns fixing that;
    // walking the whole z-order from a demoted position would be expensive.
    if ((GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
        return;
    }

    RECT islandRect = {};
    if (pillScreenRect && !IsRectEmpty(pillScreenRect)) {
        islandRect = *pillScreenRect;  // the stage is mostly empty, so test the pill only
    } else if (!GetWindowRect(hwnd, &islandRect)) {
        return;
    }

    const long islandArea = std::max(1L, (islandRect.right - islandRect.left) *
                                             (islandRect.bottom - islandRect.top));
    // Require a real overlap, not a one-pixel graze.
    const long minOverlapArea = std::max(256L, islandArea / 8);

    bool covered = false;
    for (HWND above = GetWindow(hwnd, GW_HWNDPREV); above != nullptr;
         above = GetWindow(above, GW_HWNDPREV)) {
        if (above == hwnd) {
            continue;
        }
        // Everything above a topmost window is itself topmost, so reaching a
        // non-topmost window means we are already at the top of that band.
        if ((GetWindowLongW(above, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
            break;
        }
        if (!IsWindowVisible(above) || IsIconic(above) || IsShellOwnedWindow(above)) {
            continue;
        }

        RECT otherRect = {};
        RECT intersection = {};
        if (GetWindowRect(above, &otherRect) &&
            IntersectRect(&intersection, &islandRect, &otherRect)) {
            const long overlap = (intersection.right - intersection.left) *
                                 (intersection.bottom - intersection.top);
            if (overlap >= minOverlapArea) {
                covered = true;
                break;
            }
        }
    }

    if (covered) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        DwmBackdrop::EnsureBehind(hwnd);
    }
}

RECT GetIslandDockRect() {
    RECT work = GetAnchorWorkRect();
    const float scale = g_settings.sizeScale;
    const int dockWidth = static_cast<int>(std::max(280.0f, 340.0f * scale));
    const int dockHeight = static_cast<int>(std::max(48.0f, 56.0f * scale));
    const bool flushTop = g_settings.notchStyle || g_settings.borderMergedMode;

    int x = work.left + (work.right - work.left - dockWidth) / 2;
    int y = flushTop ? work.top : (work.top + 8);

    switch (g_settings.position) {
        case Position::TopLeft:
            x = work.left + 16;
            y = flushTop ? work.top : (work.top + 8);
            break;
        case Position::TopRight:
            x = work.right - dockWidth - 16;
            y = flushTop ? work.top : (work.top + 8);
            break;
        case Position::BottomCenter:
            x = work.left + (work.right - work.left - dockWidth) / 2;
            y = work.bottom - dockHeight - 40;
            break;
        case Position::BottomLeft:
            x = work.left + 16;
            y = work.bottom - dockHeight - 40;
            break;
        case Position::BottomRight:
            x = work.right - dockWidth - 16;
            y = work.bottom - dockHeight - 40;
            break;
        case Position::TopCenter:
        default:
            break;
    }

    x += g_settings.offsetX;
    // The dock rect is the hover target for a hidden island, which is always in
    // its collapsed state, so it follows the collapsed offset.
    y += g_settings.offsetY;

    RECT r;
    r.left = x;
    r.right = x + dockWidth;
    if (IsBottomPosition(g_settings.position)) {
        r.top = y;
        r.bottom = std::max(static_cast<int>(work.bottom), y + dockHeight + 20);
    } else {
        r.top = std::min(y, static_cast<int>(work.top));
        r.bottom = y + dockHeight;
    }
    return r;
}

