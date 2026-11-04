// Bunbun — the screen colour picker on Linux, as on Windows (color_picker_module.inc):
// started from the eyedropper in the Studio header, the Colour tile in All apps, or
// the right-click and tray menus.
//   - X11: the screen is frozen under a full-screen window with a loupe that follows
//     the pointer (15 x 15 pixels at 9x, the centre one outlined, its hex below).
//     A click picks; arrow keys nudge by a pixel; Esc or a right-click cancels.
//   - Wayland (and GNOME through XWayland, which can't see other apps' windows): the
//     desktop's own picker, through the screenshot portal's PickColor.
// The picked colour comes back through `picked`; the island shows it as a card.
#pragma once

#include <cstdint>
#include <functional>

namespace bb::lpick {

void Start(std::function<void(uint8_t r, uint8_t g, uint8_t b)> picked);
bool Open();

}  // namespace bb::lpick
