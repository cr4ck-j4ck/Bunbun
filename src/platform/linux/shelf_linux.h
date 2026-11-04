// Bunbun — the File shelf and the camera Mirror on Linux, the data side of the two
// Studio pages. Files dropped on the island are parked on the shelf
// (Modules.FileTrayModule, FileTrayMaxItems, FileTrayPersist: kept in
// $XDG_STATE_HOME/Bunbun/shelf.txt). The Mirror page (Bunbun.MirrorPage) shows the
// webcam, read straight from the kernel's video API (V4L2), only while you've
// turned it on.
#pragma once

#include <cairo.h>

#include <functional>
#include <string>
#include <vector>

namespace bb::lshelf {

void LoadSettings();
bool ShelfOn();
bool MirrorOn();

// ── The shelf ───────────────────────────────────────────────────────────────
struct Item {
    std::string uri;   // file:///…
    std::string name;  // what the page shows
    bool folder = false;
};
const std::vector<Item>& Items();
void Add(const std::vector<std::string>& uris);  // newest first, the oldest drop off past the limit
void Remove(int index);
void Clear();
void Open(int index);

// ── The camera ──────────────────────────────────────────────────────────────
// `frame` asks for a redraw when a new picture arrives (on the main loop).
bool CameraStart(std::function<void()> frame);
void CameraStop();
bool CameraRunning();
std::string CameraError();  // why it couldn't start, or ""
// The latest picture as a cairo surface (owned here; valid until the next call), or null.
cairo_surface_t* CameraFrame();

}  // namespace bb::lshelf
