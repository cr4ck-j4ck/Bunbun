// Screen Time on Linux: how long each app has been in front, as on Windows
// (src/island/modules/screentime_module.inc). The bookkeeping is shared
// (src/common/screentime.h). The app in front can only be read on X11; on a
// Wayland session the page says so instead of counting.
#pragma once

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <vector>

#include "../../common/screentime.h"

namespace bb::lscreen {

void LoadSettings();  // Bunbun.ScreenTime, Bunbun.ScreenTimeLimit
bool On();
int LimitHours();     // 0 = no limit
bool Supported();     // false on Wayland: no way to see the app in front

// Once a second, from the island's timer.
void Tick();

// Today, the week and the apps, with each app's icon (nullptr while unknown);
// the pixbufs belong to the module.
struct View {
    bb::screentime::Summary summary;
    std::vector<GdkPixbuf*> icons;  // one per summary.apps entry
};
View Get();

void Save();  // on the way out

}  // namespace bb::lscreen
