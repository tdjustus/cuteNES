#pragma once

// Where the NES picture goes in a view (spec FR-040; research R2): the
// source rectangle after overscan cropping, and the target rectangle after
// pixel-aspect correction, fitting and optional integer scaling.

#include <QRect>
#include <QRectF>
#include <QSizeF>

namespace cutenes::qt {

enum class Aspect {
    Square,      // 1:1 pixels
    EightToSeven, // the NTSC pixel aspect ratio, 8:7
    FourThree,   // the full 256x240 picture stretched to 4:3
};

// Pixels hidden at each edge of the 256x240 picture, 0-16 each.
struct Overscan {
    int top = 8;
    int bottom = 8;
    int left = 0;
    int right = 0;
};

// How the NES pixels reach the screen: each one a flat block, blended with
// its neighbors, or with the lower part of each NES line darkened as on a
// CRT.
enum class Filter { Nearest, Smooth, Scanlines };

struct DisplayOptions {
    Aspect aspect = Aspect::EightToSeven;
    Overscan overscan;
    bool integerScaling = true;
    Filter filter = Filter::Nearest;
};

struct Viewport {
    QRect source;  // in NES pixels
    QRectF target; // in the area's units (device pixels for crisp output)
};

// Width of one NES pixel relative to its height.
double pixelAspect(Aspect aspect);

// Fits the cropped picture into `area`, centred. With integer scaling the
// vertical scale is a whole number (each NES line is the same number of
// screen lines), as long as at least 1x fits; otherwise it fits exactly.
Viewport computeViewport(const QSizeF& area, const DisplayOptions& options);

// The view's preferred size at `scale` (FR-040 window scale).
QSizeF scaledSize(int scale, const DisplayOptions& options);

} // namespace cutenes::qt
