// Bunbun — synced lyrics on Linux: the same lookups as the Windows island
// (src/island/modules/lyrics_module.inc): LRCLIB first (exact, cleaned, then
// searches), NetEase as a strict fallback, and a per-song cache in memory and in
// $XDG_CACHE_HOME/Bunbun/lyrics. Requests go through libsoup on the GTK main loop.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mpris.h"

namespace bb::llyrics {

struct Line {
    int64_t timeMs = -1;  // -1 for unsynced (plain) lines
    std::wstring text;
};

struct State {
    bool hasData = false, synced = false;
    bool notFound = false, instrumental = false;
    bool fetching = false;
    std::string title, artist;  // the track these belong to
    std::vector<Line> lines;
};

// `changed` asks for a redraw when lyrics arrive.
void Start(std::function<void()> changed);
// Modules.Lyrics, Bunbun.PillLyrics, Modules.MediaTabLyrics, Modules.Media,
// Modules.LyricsNetEase, Modules.LyricsCache, Modules.LyricsCacheMaxMB, Modules.LyricsOffsetMs.
void LoadSettings();
// The player's track changed (or its details filled in).
void OnTrack(const bb::mpris::Track& track);

const State& Current();
// True when these lyrics belong to `track` and are synced.
bool Usable(const bb::mpris::Track& track);
// The line playing at `positionSec` (the lookahead and your offset included):
// -1 before the first line. *posMs gets the adjusted position.
int LineAt(double positionSec, bool playing, int64_t* posMs);
// A long instrumental break fades the line to a note: 0 (line) .. 1 (note).
float GapNoteMix(int index, int64_t posMs);

bool PillOn();  // Bunbun.PillLyrics: lyrics on the small island
bool PageOn();  // Modules.Lyrics: the Lyrics page

}  // namespace bb::llyrics
