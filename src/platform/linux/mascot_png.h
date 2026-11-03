// Bunbun — one mascot frame as PNG bytes (settings previews, the app icon).
#pragma once

#include <d2d1.h>

#include <string>

#include "../../mascot/mascot.h"

namespace bb::plat {

// Draws `style` at `px` pixels square, as the settings app's preview does on Windows.
std::string RenderMascotPng(const mascot::Style& style, mascot::Mood mood, int px, double t, double since,
                            D2D1_POINT_2F look);

std::string Base64(const std::string& bytes);

}  // namespace bb::plat
