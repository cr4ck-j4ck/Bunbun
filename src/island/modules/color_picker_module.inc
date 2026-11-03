// Bunbun — screen colour picker.
//
// #included inside island_core.cpp's anonymous namespace. Started from the
// eyedropper in the Studio header or the island's right-click menu.
//
// It freezes the screen (one screenshot of the whole virtual desktop) under a
// full-screen window on its own thread, so the island keeps animating. A loupe
// follows the pointer: 15 x 15 pixels at 9x with the centre pixel outlined and its
// hex code underneath. A click picks that pixel, copies its hex code and shows a
// card with HEX, RGB and HSL; clicking a row copies that form. Arrow keys nudge the
// pointer by one pixel, Esc or a right-click closes, clicking off the card picks
// again. Copies go through the normal clipboard, so they show up in the island's
// clipboard pill and the Clipboard page's history.

namespace ColorPicker {

constexpr int kLoupeCells = 15;  // odd, so there is a centre pixel
constexpr int kLoupeZoom = 9;

struct State {
    HWND hwnd = nullptr;
    int x0 = 0, y0 = 0, w = 0, h = 0;  // virtual desktop, physical pixels
    HDC shotDc = nullptr;
    HBITMAP shotBmp = nullptr;
    const uint32_t* shotBits = nullptr;  // top-down BGRA
    HDC backDc = nullptr;
    HBITMAP backBmp = nullptr;
    HFONT font = nullptr, fontBold = nullptr, fontSmall = nullptr;
    float scale = 1.0f;
    POINT cursor{};       // client coordinates
    RECT lastDrawn{};     // overlay area painted last time, to invalidate on move
    bool picked = false;
    POINT pickedAt{};
    uint8_t r = 0, g = 0, b = 0;
    RECT card{}, rows[3]{};
    int copiedRow = -1;
    ULONGLONG openedAt = 0;  // focus changes right after opening (a menu closing) are ignored
};

std::atomic<bool> g_open{false};

int S(const State& s, float v) { return static_cast<int>(std::lround(v * s.scale)); }

uint32_t PixelAt(const State& s, int x, int y) {
    x = std::clamp(x, 0, s.w - 1);
    y = std::clamp(y, 0, s.h - 1);
    return s.shotBits[static_cast<size_t>(y) * s.w + x];
}

std::wstring Format(const State& s, int kind) {
    wchar_t buf[64];
    if (kind == 0) {
        swprintf_s(buf, L"#%02X%02X%02X", s.r, s.g, s.b);
    } else if (kind == 1) {
        swprintf_s(buf, L"rgb(%d, %d, %d)", s.r, s.g, s.b);
    } else {
        const float r = s.r / 255.0f, g = s.g / 255.0f, b = s.b / 255.0f;
        const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
        const float l = (mx + mn) * 0.5f;
        float h = 0.0f, sat = 0.0f;
        if (mx > mn) {
            const float d = mx - mn;
            sat = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
            if (mx == r) h = std::fmod((g - b) / d + 6.0f, 6.0f);
            else if (mx == g) h = (b - r) / d + 2.0f;
            else h = (r - g) / d + 4.0f;
            h *= 60.0f;
        }
        swprintf_s(buf, L"hsl(%d, %d%%, %d%%)", static_cast<int>(std::lround(h)) % 360,
                   static_cast<int>(std::lround(sat * 100)), static_cast<int>(std::lround(l * 100)));
    }
    return buf;
}

void Copy(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

void FillRound(HDC dc, RECT r, int radius, COLORREF fill, COLORREF edge) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

void Text(HDC dc, const std::wstring& t, RECT r, HFONT f, COLORREF c, UINT fmt) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &r, fmt | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
}

// The loupe and its label, placed beside the pointer and kept on screen.
RECT LoupeRect(const State& s, POINT c) {
    const int size = S(s, kLoupeCells * kLoupeZoom);
    const int labelH = S(s, 30);
    int x = c.x + S(s, 22), y = c.y + S(s, 22);
    if (x + size > s.w) x = c.x - S(s, 22) - size;
    if (y + size + labelH > s.h) y = c.y - S(s, 22) - size - labelH;
    return RECT{x, y, x + size, y + size + labelH};
}

RECT CardRect(const State& s, POINT c) {
    const int w = S(s, 236), h = S(s, 176);
    int x = c.x + S(s, 18), y = c.y + S(s, 18);
    if (x + w > s.w) x = c.x - S(s, 18) - w;
    if (y + h > s.h) y = c.y - S(s, 18) - h;
    return RECT{x, y, x + w, y + h};
}

void PaintOverlay(State& s, HDC dc) {
    if (!s.picked) {
        const RECT lr = LoupeRect(s, s.cursor);
        const int size = lr.right - lr.left;
        // Magnified pixels, nearest neighbour.
        SetStretchBltMode(dc, COLORONCOLOR);
        const int half = kLoupeCells / 2;
        StretchBlt(dc, lr.left, lr.top, size, size, s.shotDc, s.cursor.x - half, s.cursor.y - half, kLoupeCells,
                   kLoupeCells, SRCCOPY);
        // Faint grid.
        HPEN grid = CreatePen(PS_SOLID, 1, RGB(60, 60, 64));
        HGDIOBJ op = SelectObject(dc, grid);
        for (int i = 1; i < kLoupeCells; ++i) {
            const int p = lr.left + i * size / kLoupeCells;
            MoveToEx(dc, p, lr.top, nullptr); LineTo(dc, p, lr.top + size);
            const int q = lr.top + i * size / kLoupeCells;
            MoveToEx(dc, lr.left, q, nullptr); LineTo(dc, lr.left + size, q);
        }
        SelectObject(dc, op);
        DeleteObject(grid);
        // The centre pixel: white box with a dark outline, readable on any colour.
        const int cell = size / kLoupeCells;
        RECT c{lr.left + half * size / kLoupeCells, lr.top + half * size / kLoupeCells, 0, 0};
        c.right = c.left + cell;
        c.bottom = c.top + cell;
        HBRUSH none = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HGDIOBJ ob = SelectObject(dc, none);
        HPEN dark = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN light = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        op = SelectObject(dc, light);
        Rectangle(dc, c.left, c.top, c.right + 1, c.bottom + 1);
        SelectObject(dc, dark);
        Rectangle(dc, c.left - 2, c.top - 2, c.right + 3, c.bottom + 3);
        // Loupe border.
        SelectObject(dc, light);
        Rectangle(dc, lr.left - 1, lr.top - 1, lr.left + size + 1, lr.top + size + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(dark);
        DeleteObject(light);
        // Label: swatch and hex.
        const uint32_t px = PixelAt(s, s.cursor.x, s.cursor.y);
        s.r = (px >> 16) & 0xFF; s.g = (px >> 8) & 0xFF; s.b = px & 0xFF;
        const RECT label{lr.left, lr.top + size + S(s, 4), lr.right, lr.bottom};
        FillRound(dc, label, S(s, 10), RGB(28, 28, 30), RGB(58, 58, 60));
        const RECT sw{label.left + S(s, 8), label.top + S(s, 6), label.left + S(s, 26), label.bottom - S(s, 6)};
        FillRound(dc, sw, S(s, 6), RGB(s.r, s.g, s.b), RGB(255, 255, 255));
        Text(dc, Format(s, 0), RECT{sw.right + S(s, 8), label.top, label.right - S(s, 6), label.bottom}, s.fontBold,
             RGB(255, 255, 255), DT_LEFT | DT_VCENTER);
        s.lastDrawn = RECT{lr.left - 4, lr.top - 4, lr.right + 4, lr.bottom + 4};
        return;
    }

    // Picked: a card with the colour in three forms.
    s.card = CardRect(s, s.pickedAt);
    const RECT& cr = s.card;
    FillRound(dc, cr, S(s, 18), RGB(28, 28, 30), RGB(58, 58, 60));
    const RECT sw{cr.left + S(s, 14), cr.top + S(s, 14), cr.left + S(s, 54), cr.top + S(s, 54)};
    FillRound(dc, sw, S(s, 12), RGB(s.r, s.g, s.b), RGB(90, 90, 94));
    Text(dc, Format(s, 0), RECT{sw.right + S(s, 12), sw.top, cr.right - S(s, 12), sw.top + S(s, 22)}, s.fontBold,
         RGB(255, 255, 255), DT_LEFT | DT_VCENTER);
    Text(dc, L"Copied \x00B7 click a row to copy it",
         RECT{sw.right + S(s, 12), sw.top + S(s, 22), cr.right - S(s, 12), sw.bottom}, s.fontSmall, RGB(142, 142, 147),
         DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    static const wchar_t* kNames[] = {L"HEX", L"RGB", L"HSL"};
    for (int i = 0; i < 3; ++i) {
        RECT row{cr.left + S(s, 10), cr.top + S(s, 64) + i * S(s, 30), cr.right - S(s, 10), 0};
        row.bottom = row.top + S(s, 26);
        s.rows[i] = row;
        POINT p = s.cursor;
        const bool hover = PtInRect(&row, p) != FALSE;
        if (hover || s.copiedRow == i) FillRound(dc, row, S(s, 10), RGB(44, 44, 46), RGB(44, 44, 46));
        Text(dc, kNames[i], RECT{row.left + S(s, 8), row.top, row.left + S(s, 46), row.bottom}, s.fontSmall,
             RGB(142, 142, 147), DT_LEFT | DT_VCENTER);
        Text(dc, Format(s, i), RECT{row.left + S(s, 46), row.top, row.right - S(s, 60), row.bottom}, s.font,
             RGB(255, 255, 255), DT_LEFT | DT_VCENTER);
        Text(dc, s.copiedRow == i ? L"Copied" : (hover ? L"Copy" : L""),
             RECT{row.right - S(s, 64), row.top, row.right - S(s, 8), row.bottom}, s.fontSmall,
             s.copiedRow == i ? RGB(48, 209, 88) : RGB(100, 210, 255), DT_RIGHT | DT_VCENTER);
    }
    Text(dc, L"Esc closes \x00B7 click outside to pick again",
         RECT{cr.left + S(s, 14), cr.bottom - S(s, 22), cr.right - S(s, 14), cr.bottom - S(s, 6)}, s.fontSmall,
         RGB(99, 99, 102), DT_LEFT | DT_VCENTER);
    s.lastDrawn = RECT{cr.left - 4, cr.top - 4, cr.right + 4, cr.bottom + 4};
}

void Repaint(State& s) {
    RECT old = s.lastDrawn;
    InvalidateRect(s.hwnd, &old, FALSE);
    // The new overlay area: worked out by painting into the back buffer.
    RECT next = s.picked ? CardRect(s, s.pickedAt) : LoupeRect(s, s.cursor);
    InflateRect(&next, 6, 6);
    InvalidateRect(s.hwnd, &next, FALSE);
}

void Pick(State& s) {
    const uint32_t px = PixelAt(s, s.cursor.x, s.cursor.y);
    s.r = (px >> 16) & 0xFF; s.g = (px >> 8) & 0xFF; s.b = px & 0xFF;
    s.picked = true;
    s.pickedAt = s.cursor;
    s.copiedRow = 0;
    Copy(s.hwnd, Format(s, 0));
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_SETCURSOR:
            if (s && s->picked && PtInRect(&s->card, s->cursor)) SetCursor(LoadCursorW(nullptr, IDC_HAND));
            else SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_MOUSEMOVE:
            if (s) {
                Repaint(*s);
                s->cursor = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                Repaint(*s);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (!s) return 0;
            s->cursor = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (s->picked) {
                for (int i = 0; i < 3; ++i) {
                    if (PtInRect(&s->rows[i], s->cursor)) {
                        Copy(hwnd, Format(*s, i));
                        s->copiedRow = i;
                        Repaint(*s);
                        return 0;
                    }
                }
                if (PtInRect(&s->card, s->cursor)) return 0;
                Repaint(*s);
                s->picked = false;  // outside the card: pick again
                Repaint(*s);
                return 0;
            }
            Repaint(*s);
            Pick(*s);
            Repaint(*s);
            return 0;
        case WM_RBUTTONUP:
            DestroyWindow(hwnd);
            return 0;
        case WM_KEYDOWN: {
            if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
            if (!s) return 0;
            if (wp == VK_RETURN || wp == VK_SPACE) {
                Repaint(*s);
                if (s->picked) s->picked = false; else Pick(*s);
                Repaint(*s);
                return 0;
            }
            int dx = 0, dy = 0;
            if (wp == VK_LEFT) dx = -1; else if (wp == VK_RIGHT) dx = 1;
            else if (wp == VK_UP) dy = -1; else if (wp == VK_DOWN) dy = 1;
            if (dx || dy) {
                if (GetKeyState(VK_SHIFT) & 0x8000) { dx *= 10; dy *= 10; }
                POINT p{s->cursor.x + dx, s->cursor.y + dy};
                POINT screen{p.x + s->x0, p.y + s->y0};
                SetCursorPos(screen.x, screen.y);  // the WM_MOUSEMOVE that follows redraws
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (s) {
                const RECT& r = ps.rcPaint;
                BitBlt(s->backDc, r.left, r.top, r.right - r.left, r.bottom - r.top, s->shotDc, r.left, r.top, SRCCOPY);
                PaintOverlay(*s, s->backDc);
                BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, s->backDc, r.left, r.top, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && s && s->hwnd && GetTickCount64() - s->openedAt < 1000) {
                // Opened from the island's menu: the island takes the focus back as
                // the menu closes. Take it back once more instead of closing.
                SetTimer(hwnd, 1, 60, nullptr);
                return 0;
            }
            if (LOWORD(wp) == WA_INACTIVE && s && s->hwnd) {
                // Another window took the focus (Alt+Tab, a notification): close
                // rather than leave a frozen screen over everything.
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_TIMER:
            KillTimer(hwnd, 1);
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Run() {
    State s;
    s.x0 = GetSystemMetrics(SM_XVIRTUALSCREEN);
    s.y0 = GetSystemMetrics(SM_YVIRTUALSCREEN);
    s.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    s.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // Freeze the screen first, before our own window appears.
    HDC screen = GetDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = s.w;
    bi.bmiHeader.biHeight = -s.h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    s.shotDc = CreateCompatibleDC(screen);
    s.shotBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    s.backDc = CreateCompatibleDC(screen);
    s.backBmp = CreateCompatibleBitmap(screen, s.w, s.h);
    if (!s.shotBmp || !s.backBmp || !bits) {
        ReleaseDC(nullptr, screen);
        g_open = false;
        return;
    }
    s.shotBits = static_cast<const uint32_t*>(bits);
    SelectObject(s.shotDc, s.shotBmp);
    SelectObject(s.backDc, s.backBmp);
    BitBlt(s.shotDc, 0, 0, s.w, s.h, screen, s.x0, s.y0, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screen);

    POINT cur{};
    GetCursorPos(&cur);
    s.cursor = POINT{cur.x - s.x0, cur.y - s.y0};
    s.scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    auto font = [&](int px, int weight) {
        return CreateFontW(-S(s, static_cast<float>(px)), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           L"Segoe UI Variable Display");
    };
    s.font = font(14, FW_MEDIUM);
    s.fontBold = font(15, FW_SEMIBOLD);
    s.fontSmall = font(11, FW_MEDIUM);

    static ATOM cls = 0;
    if (!cls) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BunbunColorPicker";
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        cls = RegisterClassW(&wc);
    }
    s.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"BunbunColorPicker", L"Pick a colour", WS_POPUP,
                             s.x0, s.y0, s.w, s.h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (s.hwnd) {
        SetWindowLongPtrW(s.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        s.openedAt = GetTickCount64();
        ShowWindow(s.hwnd, SW_SHOW);
        SetForegroundWindow(s.hwnd);
        SetFocus(s.hwnd);
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    DeleteObject(s.font);
    DeleteObject(s.fontBold);
    DeleteObject(s.fontSmall);
    DeleteDC(s.shotDc);
    DeleteObject(s.shotBmp);
    DeleteDC(s.backDc);
    DeleteObject(s.backBmp);
    g_open = false;
}

// Opens the picker (once at a time). Any thread.
void Start() {
    if (g_open.exchange(true)) return;
    std::thread(Run).detach();
}

}  // namespace ColorPicker
