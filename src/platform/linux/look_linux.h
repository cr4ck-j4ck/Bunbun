// Bunbun — the Linux island's look, sounds and privacy dots: the Windows
// settings for shape, theme, colours, font and text size, the UI chimes, and the
// microphone / camera dots, read and applied here so island_linux.cpp and
// studio_linux.cpp only call in.
#pragma once

#include <cairo.h>

#include <functional>
#include <string>

#include "../../common/chime_wav.h"

namespace bb::llook {

enum class Shape { Pill, W11, Notch };

struct Look {
    Shape shape = Shape::Pill;
    double fill[3] = {0.031, 0.031, 0.039};  // the surface (theme, custom or Fill = Opaque)
    double opacity = 0.96;                   // Themes.PillOpacity (1 when Opaque)
    double ink[3] = {1, 1, 1};               // primary text and glyphs
    double sec[3] = {0.608, 0.608, 0.647};   // secondary text (Themes.TextSecondaryColor)
    double border[3] = {0.118, 0.118, 0.133};
    bool borderOn = true;                    // Themes.ContourBorderMode != borderless
    bool shadow = true;                      // Themes.DropShadow
    bool depth = true;                       // Themes.MaterialDepth: shading downwards
    double bloom = 1.0;                      // Themes.AccentBloom: the accent wash from the top, 0..2
    double tint = 0.72;                      // Themes.TintIntensity: the dark scrim under a see-through fill
    std::string font = "Sans";               // Themes.FontFamily
    double textScale = 1.0;                  // Themes.TextScale
    bool sounds = true, soundOpenClose = false;
    int soundVolume = 40;
    bool dots = true, dotMic = true, dotCam = true, dotScreen = true;
    bool liveMood = true;  // Bunbun.LiveMood: watch the screen for the Live mood even with the dots off
    double micColor[3] = {1.0, 0.584, 0.0}, camColor[3] = {0.204, 0.780, 0.349}, screenColor[3] = {0.31, 0.55, 1.0};
};

void LoadSettings();
const Look& Get();

// The island's outline for its shape: a capsule-ish rounded box, a Windows 11
// rectangle, or a notch (square top, flush with the screen edge).
double Radius(double h);  // corner radius for a surface this tall
void SurfacePath(cairo_t* cr, double x, double y, double w, double h, double radius);
// The notch's ear size for a w x h island (0 for other shapes).
double NotchEar(double w, double h);
// The visible outline as the current path (for the edge light); true when it's closed.
bool OutlinePath(cairo_t* cr, double x, double y, double w, double h, double radius);
// Separate displays: the shape of the display the island is on (BaseShape = Appearance.ShapeStyle).
Shape BaseShape();
void SetShape(Shape s);
// The surface path and corner radius in a given shape (a mirror drawn in its display's own look).
void SurfacePathAs(cairo_t* cr, double x, double y, double w, double h, double radius, Shape shape, bool ears);
double RadiusAs(double h, Shape shape);
double TopGap(double normalGap);  // 0 for the notch and Border-merged mode
// Which screen edge the island hangs from (Appearance.Position, BorderMergedMode):
// the notch and the merged island are square on that side.
void SetEdge(bool bottom, bool merged);

// Text and glyphs are drawn "white" throughout; this turns white into the
// theme's ink (dark on Porcelain): faint white is the secondary text colour, the
// rest the primary one, keeping the alpha. Other colours pass through.
void Ink(double* r, double* g, double* b, double* a);
const char* Font();
double TextSize(double size);

// The chimes (Bunbun.Sounds), played with paplay, pw-play or aplay.
void PlayChime(bb::chime::Chime c);

// Microphone / camera / screen in use (polled every 2 s off the main thread). The
// screen is one dot for sharing and recording alike.
void StartPrivacy(std::function<void()> changed);
bool MicInUse();
bool CamInUse();
bool ScreenInUse();   // being streamed, shared or recorded (watched for the dot or the Live mood)
bool ScreenDotOn();   // ... and the blue dot is on (Privacy dots and their Screen dot)
// A browser's "is sharing your screen" window is up (X11; set by features_linux).
void SetSharingWindow(bool shown);
// Who uses the screen and how, e.g. "OBS Studio is streaming your screen"; empty
// when nothing does.
std::string ScreenText();

}  // namespace bb::llook
