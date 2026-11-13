// Bunbun: Media dashboard hit-testing on content coordinates.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

// ── Media dashboard hit-testing (see the notes next to MediaLayout) ──────────

// Converts a client-area mouse position into the media dashboard's content
// space. Returns valid == false when DrawMedia has not painted recently, so a
// stale frame can never produce a phantom hit.
MediaContentPoint MediaContentFromClient(int clientX, int clientY) {
    MediaContentPoint out;

    // Seqlock read: retry until a write is not in progress and the counter has
    // not moved, so all six fields come from the same frame.
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f, scale = 1.0f;
    float trim = 0.0f;
    unsigned long long stamp = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const unsigned before = g_mediaHitSeq.load(std::memory_order_relaxed);
        if (before & 1u) {
            continue;  // write in progress
        }
        std::atomic_thread_fence(std::memory_order_acquire);

        left = g_mediaHitLeft.load(std::memory_order_relaxed);
        top = g_mediaHitTop.load(std::memory_order_relaxed);
        right = g_mediaHitRight.load(std::memory_order_relaxed);
        bottom = g_mediaHitBottom.load(std::memory_order_relaxed);
        scale = g_mediaHitScale.load(std::memory_order_relaxed);
        trim = g_mediaHitTrim.load(std::memory_order_relaxed);
        stamp = g_mediaHitStamp.load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_mediaHitSeq.load(std::memory_order_relaxed) == before) {
            break;
        }
        stamp = 0;  // torn; force another attempt (or bail out below)
    }

    if (stamp == 0 || GetTickCount64() - stamp > 500) {
        return out;
    }

    if (scale < 0.05f) {
        scale = 1.0f;
    }

    const float width = right - left - std::max(0.0f, trim);
    const float height = bottom - top;
    if (width <= 1.0f || height <= 1.0f) {
        return out;
    }

    // DrawPill scales content about the pill centre, so undo that about the
    // same point to get back into content space.
    const float centerX = (left + right) * 0.5f;
    const float centerY = (top + bottom) * 0.5f;
    const float contentX = (static_cast<float>(clientX) - centerX) / scale + centerX;
    const float contentY = (static_cast<float>(clientY) - centerY) / scale + centerY;

    out.valid = true;
    out.x = contentX - left;
    out.y = contentY - top;
    out.width = width;
    out.height = height;
    return out;
}

// The immersive lyrics view draws the transport block (scrubber, controls, dock,
// lyrics button) slightly smaller. This maps a content-space point back into that
// block's own design space, so the hit tests below keep using the MediaLayout
// constants. A no-op unless the immersive view is showing.
MediaContentPoint MediaPlayerZonePoint(const MediaContentPoint& pt) {
    const float mix = g_immersiveMixPub.load(std::memory_order_relaxed);
    if (!pt.valid || mix <= 0.001f) {
        return pt;
    }
    const float s = 1.0f + (ImmersiveLayout::kPlayerScale - 1.0f) * mix;
    const float cx = pt.width * 0.5f;
    MediaContentPoint out = pt;
    out.x = cx + (pt.x - cx) / s;
    out.y = ImmersiveLayout::kPlayerPivotY + (pt.y - ImmersiveLayout::kPlayerPivotY) / s;
    return out;
}

// Returns 0 = previous, 1 = play/pause, 2 = next, or -1 for no button.
int MediaTransportHitTest(const MediaContentPoint& rawPt) {
    const MediaContentPoint pt = MediaPlayerZonePoint(rawPt);
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) {
        return -1;
    }

    const float centerX = pt.width * 0.5f;
    const float dy = pt.y - MediaLayout::kControlsY;

    struct Target {
        float offsetX;
        float radius;
        int command;
    };
    const Target targets[] = {
        {-MediaLayout::kControlSpacing, MediaLayout::kNavButtonRadius, 0},
        {0.0f, MediaLayout::kPlayButtonRadius, 1},
        {MediaLayout::kControlSpacing, MediaLayout::kNavButtonRadius, 2},
    };

    for (const Target& t : targets) {
        const float reach = t.radius + MediaLayout::kControlHitPad;
        if (std::fabs(dy) <= reach && std::fabs(pt.x - (centerX + t.offsetX)) <= reach) {
            return t.command;
        }
    }
    return -1;
}

// Fraction along the scrubber bar (0..1) for a content-space point, or -1 when
// the point is not on the bar.
float MediaScrubFractionFromContent(const MediaContentPoint& rawPt) {
    const MediaContentPoint pt = MediaPlayerZonePoint(rawPt);
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) {
        return -1.0f;
    }

    const float barLeft = MediaLayout::kScrubInsetLeft;
    const float barRight = pt.width - MediaLayout::kScrubInsetRight;
    if (barRight - barLeft <= 1.0f) {
        return -1.0f;
    }

    if (std::fabs(pt.y - MediaLayout::kScrubberY) > MediaLayout::kScrubHitHalfHeight ||
        pt.x < barLeft - MediaLayout::kScrubHitPadX ||
        pt.x > barRight + MediaLayout::kScrubHitPadX) {
        return -1.0f;
    }

    return Clamp((Clamp(pt.x, barLeft, barRight) - barLeft) / (barRight - barLeft), 0.0f, 1.0f);
}

// Clamped fraction for an ongoing drag, ignoring the on-bar test so the scrub
// keeps tracking once the press has been captured.
float MediaScrubFractionUnbounded(const MediaContentPoint& rawPt) {
    const MediaContentPoint pt = MediaPlayerZonePoint(rawPt);
    if (!pt.valid) {
        return -1.0f;
    }
    const float barLeft = MediaLayout::kScrubInsetLeft;
    const float barRight = pt.width - MediaLayout::kScrubInsetRight;
    if (barRight - barLeft <= 1.0f) {
        return -1.0f;
    }
    return Clamp((Clamp(pt.x, barLeft, barRight) - barLeft) / (barRight - barLeft), 0.0f, 1.0f);
}

// Index of the File Tray row under a content-space point, or -1. Rows are drawn
// newest-first, so index 0 is the most recently dropped file.

int FileTrayRowAtContentPoint(const MediaContentPoint& pt, int itemCount) {
    if (!pt.valid || itemCount <= 0 || pt.height <= MediaLayout::kExpandedMinHeight) {
        return -1;
    }
    if (pt.x < FileTrayLayout::kPadX || pt.x > pt.width - FileTrayLayout::kPadX) {
        return -1;
    }

    const float listBottom = pt.height - FileTrayLayout::kListBottomInset;
    if (pt.y < FileTrayLayout::kListTop || pt.y > listBottom) {
        return -1;
    }

    const float stride = FileTrayLayout::kRowHeight + FileTrayLayout::kRowGap;
    const int index = static_cast<int>((pt.y - FileTrayLayout::kListTop) / stride);
    // Reject the gap between rows so hovering dead space highlights nothing.
    const float rowTop = FileTrayLayout::kListTop + index * stride;
    if (pt.y > rowTop + FileTrayLayout::kRowHeight) {
        return -1;
    }

    const int capacity = FileTrayLayout::VisibleRowCapacity(pt.height);
    if (index < 0 || index >= capacity || index >= itemCount) {
        return -1;
    }
    return index;
}

// True when the point is over the bin button in the File Tray header.
bool FileTrayClearHitTest(const MediaContentPoint& pt) {
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) {
        return false;
    }
    constexpr float kSlop = 3.0f;
    const float right = pt.width - FileTrayLayout::kPadX;
    const float left = right - FileTrayLayout::kClearBtnSize;
    const float top = FileTrayLayout::kClearBtnTop;
    const float bottom = top + FileTrayLayout::kClearBtnSize;
    return pt.x >= left - kSlop && pt.x <= right + kSlop &&
           pt.y >= top - kSlop && pt.y <= bottom + kSlop;
}

// True when the point is over the paste button (left of the bin) in the File Tray header.
bool FileTrayPasteHitTest(const MediaContentPoint& pt) {
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) {
        return false;
    }
    constexpr float kSlop = 3.0f;
    const float right = pt.width - FileTrayLayout::kPadX - FileTrayLayout::kClearBtnSize -
                        FileTrayLayout::kPasteBtnGap;
    const float left = right - FileTrayLayout::kClearBtnSize;
    const float top = FileTrayLayout::kClearBtnTop;
    const float bottom = top + FileTrayLayout::kClearBtnSize;
    return pt.x >= left - kSlop && pt.x <= right + kSlop &&
           pt.y >= top - kSlop && pt.y <= bottom + kSlop;
}

// True when the point is over the cut button of the given visible row.
bool FileTrayRemoveHitTest(const MediaContentPoint& pt, int row) {
    if (!pt.valid || row < 0 || pt.height <= MediaLayout::kExpandedMinHeight) {
        return false;
    }
    constexpr float kSlop = 3.0f;
    const float half = FileTrayLayout::kRemoveBtnSize * 0.5f + kSlop;
    const float centerX = pt.width - FileTrayLayout::kPadX - FileTrayLayout::kRemoveBtnMargin -
                          FileTrayLayout::kRemoveBtnSize * 0.5f;
    const float centerY = FileTrayLayout::kListTop +
                          static_cast<float>(row) * (FileTrayLayout::kRowHeight + FileTrayLayout::kRowGap) +
                          FileTrayLayout::kRowHeight * 0.5f;
    return std::fabs(pt.x - centerX) <= half && std::fabs(pt.y - centerY) <= half;
}

// True when the point is over the expanded layout's album art.
bool MediaArtHitTest(const MediaContentPoint& pt) {
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) {
        return false;
    }
    if (g_immersiveMixPub.load(std::memory_order_relaxed) > 0.5f) {
        return pt.x >= ImmersiveLayout::kArtLeft &&
               pt.x <= ImmersiveLayout::kArtLeft + ImmersiveLayout::kArtSize &&
               pt.y >= ImmersiveLayout::kArtTop &&
               pt.y <= ImmersiveLayout::kArtTop + ImmersiveLayout::kArtSize;
    }
    return pt.x >= MediaLayout::kArtInsetX &&
           pt.x <= MediaLayout::kArtInsetX + MediaLayout::kArtSize &&
           pt.y >= MediaLayout::kArtInsetY &&
           pt.y <= MediaLayout::kArtInsetY + MediaLayout::kArtSize;
}

// True when the point is over the immersive-lyrics toggle button.
bool MediaLyricsButtonHitTest(const MediaContentPoint& rawPt) {
    const MediaContentPoint pt = MediaPlayerZonePoint(rawPt);
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) {
        return false;
    }
    const float cx = pt.width - ImmersiveLayout::kBtnInsetRight - ImmersiveLayout::kBtnRadius;
    const float reach = ImmersiveLayout::kBtnRadius + ImmersiveLayout::kBtnHitPad;
    return std::fabs(pt.x - cx) <= reach &&
           std::fabs(pt.y - MediaLayout::kControlsY) <= reach;
}

