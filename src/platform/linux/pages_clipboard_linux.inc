// ── Clipboard page: the list on the left, the selected copy on the right ────
// #included inside studio_linux.cpp's anonymous namespace. The same two panels
// as Windows (studio_draw.inc's DrawStudioClipboardPage). Linux keeps texts and
// links (no images yet), so the Images filter counts none.

int g_clipFilter = 0;    // 0 all, 1 text, 2 images, 3 links
int g_clipSelected = 0;  // in the filtered list

bool IsLink(const std::string& t) { return t.rfind("http://", 0) == 0 || t.rfind("https://", 0) == 0 || t.rfind("www.", 0) == 0; }

std::string Ago(double seconds) {
    if (seconds >= 86400) return std::to_string(static_cast<int>(seconds / 86400)) + "d ago";
    if (seconds >= 3600) return std::to_string(static_cast<int>(seconds / 3600)) + "h ago";
    if (seconds >= 60) return std::to_string(static_cast<int>(seconds / 60)) + "m ago";
    return "Just now";
}

void PaintClipboardPanels(cairo_t* cr, const Rect& r) {
    const auto& items = lsrc::ClipHistory();
    const auto& times = lsrc::ClipTimes();
    const Col white = W(1), grey = kGrey, cardBg = C(0x131315), accent = Accent();
    const double leftW = 212, rightX = r.x + leftW, now = Now();
    int textN = 0, linkN = 0;
    for (const auto& t : items) (IsLink(t) ? linkN : textN)++;

    // ── Filter tabs ──
    const struct { int id; const char* name; int count; } tabs[4] = {
        {0, "All", static_cast<int>(items.size())}, {1, "Text", textN}, {2, "Images", 0}, {3, "Links", linkN}};
    double x = r.x + 4;
    for (const auto& tab : tabs) {
        const std::string label = std::string(tab.name) + " " + std::to_string(tab.count);
        const double tw = Measure(cr, label, FLabel) + 2;
        const bool on = g_clipFilter == tab.id;
        SText(cr, label, x, r.y + 2, tw, 16, FLabel, on ? white : grey);
        if (on) Fill(cr, x, r.y + 18, tw, 2, 1, white);
        g_hits.push_back({{x - 2, r.y, tw + 4, 22}, Action::ClipFilter, tab.id});
        x += tw + 10;
    }
    SText(cr, "Today", r.x + 6, r.y + 24, 74, 14, FSmall, grey);

    // ── The list ──
    std::vector<int> shown;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const bool link = IsLink(items[static_cast<size_t>(i)]);
        if ((g_clipFilter == 1 && link) || g_clipFilter == 2 || (g_clipFilter == 3 && !link)) continue;
        shown.push_back(i);
    }
    g_clipSelected = std::clamp(g_clipSelected, 0, std::max(0, static_cast<int>(shown.size()) - 1));
    const double listTop = r.y + 40, rowH = 34;
    for (size_t k = 0; k < shown.size() && listTop + k * (rowH + 2) + rowH <= r.y + r.h; ++k) {
        const int i = shown[k];
        const std::string& text = items[static_cast<size_t>(i)];
        const Rect row{r.x + 4, listTop + k * (rowH + 2), leftW - 10, rowH};
        const bool sel = static_cast<int>(k) == g_clipSelected, hover = NextHitHovered();
        if (sel) Fill(cr, row.x, row.y, row.w, row.h, 8, C(0x28282a));
        else if (hover) Fill(cr, row.x, row.y, row.w, row.h, 8, C(0x1a1a1c));
        Fill(cr, row.x + 4, row.y + 3, 28, 28, 5, C(0x222224));
        const double tcx = row.x + 18, tcy = row.y + 17;
        Source(cr, grey);
        cairo_set_line_width(cr, 1.2);
        if (IsLink(text)) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, tcx, tcy, 5, 0, 2 * kPi);
            cairo_move_to(cr, tcx - 4, tcy); cairo_line_to(cr, tcx + 4, tcy);
        } else {
            cairo_rectangle(cr, tcx - 4.5, tcy - 6, 9, 12);
            cairo_move_to(cr, tcx - 2.5, tcy - 2); cairo_line_to(cr, tcx + 2.5, tcy - 2);
            cairo_move_to(cr, tcx - 2.5, tcy + 1.5); cairo_line_to(cr, tcx + 2.5, tcy + 1.5);
        }
        cairo_stroke(cr);
        std::string title = text.substr(0, std::min<size_t>(text.find('\n'), 60));
        SText(cr, title, row.x + 38, row.y + 2, row.w - 42, 14, FLabel, white);
        const double at = static_cast<size_t>(i) < times.size() ? times[static_cast<size_t>(i)] : now;
        SText(cr, std::to_string(bb::plat::Wide(text).size()) + " chars" + kDot + Ago(now - at), row.x + 38, row.y + 17, row.w - 42, 16, FSmall, grey);
        g_hits.push_back({row, Action::ClipSelect, static_cast<int>(k)});
    }

    // ── The divider and the selected copy ──
    Fill(cr, rightX - 0.5, r.y + 4, 1, r.h - 8, 0, C(0x28282a));
    const double prL = rightX + 12, prR = r.x + r.w - 4;
    if (shown.empty()) {
        if (items.empty()) {
            SText(cr, "Nothing copied yet", prL, r.y + 56, prR - prL, 20, FBody, white, 1);
            SText(cr, "Text and links you copy show up here.", prL, r.y + 78, prR - prL, 18, FSmall, grey, 1);
        } else {
            SText(cr, "No items match filter", prL, r.y + 60, prR - prL, 30, FBody, grey, 1);
        }
        return;
    }
    const int cur = shown[static_cast<size_t>(g_clipSelected)];
    const std::string& text = items[static_cast<size_t>(cur)];
    const double at = static_cast<size_t>(cur) < times.size() ? times[static_cast<size_t>(cur)] : now;
    SText(cr, std::string(IsLink(text) ? "Link" : "Text") + kDot + Ago(now - at), prL, r.y + 2, prR - prL - 30, 16, FSmall, grey);
    {  // delete
        const Rect del{prR - 20, r.y, 20, 18};
        const bool hover = NextHitHovered();
        const double cx = del.x + 10, cy = del.y + 9;
        Source(cr, hover ? white : grey);
        cairo_set_line_width(cr, 1.2);
        cairo_rectangle(cr, cx - 3.5, cy - 3, 7, 8);
        cairo_move_to(cr, cx - 5, cy - 3); cairo_line_to(cr, cx + 5, cy - 3);
        cairo_move_to(cr, cx - 1.5, cy - 4.5); cairo_line_to(cr, cx + 1.5, cy - 4.5);
        cairo_stroke(cr);
        g_hits.push_back({del, Action::ClipDelete, cur});
    }
    const Rect box{prL, r.y + 22, prR - prL, r.h - 58};
    Fill(cr, box.x, box.y, box.w, box.h, 10, cardBg);
    cairo_save(cr);
    cairo_rectangle(cr, box.x + 12, box.y + 10, box.w - 24, box.h - 20);
    cairo_clip(cr);
    WrapText(cr, text, box.x + 12, box.y + 10, box.w - 24, FBody, white);
    cairo_restore(cr);
    SText(cr, std::to_string(bb::plat::Wide(text).size()) + " chars", prL, r.y + r.h - 28, prR - prL - 85, 24, FSmall, grey);
    const Rect copy{prR - 64, r.y + r.h - 30, 64, 24};
    Fill(cr, copy.x, copy.y, copy.w, copy.h, 12, NextHitHovered() ? W(1) : W(0.92));
    SText(cr, "Copy", copy.x + 4, copy.y, copy.w - 8, copy.h, FLabel, C(0x000000), 1);
    g_hits.push_back({copy, Action::ClipCopy, cur});
    (void)accent;
}
