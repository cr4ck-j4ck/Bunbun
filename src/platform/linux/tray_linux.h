// Bunbun — the tray icon on Linux: the mascot in the panel with the island's menu
// (Settings, the colour picker, Connect Claude Code, start at login, Quit…).
//   - StatusNotifierItem through AppIndicator (KDE, Ubuntu's GNOME, XFCE, Cinnamon,
//     MATE, Budgie), loaded at run time from libayatana-appindicator3 or
//     libappindicator3, so it's no dependency of ours;
//   - else the classic X11 tray icon (GtkStatusIcon), for panels with a system tray.
// On a desktop with neither (plain GNOME without the AppIndicator extension) there's
// no icon, and the right-click menu on the island does the same.
#pragma once

#include <gtk/gtk.h>

#include <functional>

namespace bb::ltray {

// `menu` builds a fresh menu each time it's needed (its items follow the state).
void Start(std::function<GtkWidget*()> menu);
// The settings or the state behind the menu changed: rebuild it, redraw the icon.
void Refresh();
bool Shown();

}  // namespace bb::ltray
