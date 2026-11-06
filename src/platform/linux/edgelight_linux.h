// The edge light on Linux: a light running along the island's outline
// (Bunbun.EdgeLight*), as on Windows (src/island/modules/edgelight_*.inc).
#pragma once

#include <cairo.h>

namespace bb::ledge {

void LoadSettings();

// What happened this frame (any of bb::edgelight::kOn* bits). Plays the light
// when one of them is among the chosen triggers.
void Note(unsigned events, double now);

// Whether the light is showing now (the island needs frames while it is).
bool Lit(double now);

// Draws the light along the outline of the island at (x, y, w, h) with corner
// radius `r`. `accent` is the island's accent (for the Dynamic colour).
void Draw(cairo_t* cr, double x, double y, double w, double h, double r, const double accent[3], double now);

}  // namespace bb::ledge
