// Bunbun — now playing on Linux: MPRIS players on the session D-Bus
// (org.mpris.MediaPlayer2.*). The Linux counterpart of Windows' media session
// (GSMTC). Runs on the GTK main loop; no threads.
#pragma once

#include <functional>
#include <string>

namespace bb::mpris {

struct Track {
    bool active = false;   // a player has something loaded
    bool playing = false;
    std::string title, artist, album;
    std::string artUrl;    // mpris:artUrl (file:// or http(s)://), may be empty
    std::string player;    // the app's name ("Spotify", "Firefox")
    double length = 0;     // seconds, 0 when unknown
};

// Calls onChange (on the main loop) whenever the current track or state changes.
void Start(std::function<void()> onChange);
void Stop();

// The player worth showing: the one playing most recently, else a paused one.
Track Current();

void PlayPause();
void Next();
void Previous();

}  // namespace bb::mpris
