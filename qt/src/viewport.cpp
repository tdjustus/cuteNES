#include <cutenes/qt/viewport.hpp>

#include <algorithm>
#include <cmath>

namespace cutenes::qt {

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 240;

int clampCrop(int value) { return std::clamp(value, 0, 16); }

QRect sourceRect(const Overscan& o)
{
    const int left = clampCrop(o.left);
    const int top = clampCrop(o.top);
    return {left, top, kWidth - left - clampCrop(o.right), kHeight - top - clampCrop(o.bottom)};
}

} // namespace

double pixelAspect(Aspect aspect)
{
    switch (aspect) {
    case Aspect::Square: return 1.0;
    case Aspect::EightToSeven: return 8.0 / 7.0;
    // 4:3 for the whole 256x240 picture: (4/3) / (256/240).
    case Aspect::FourThree: return (4.0 / 3.0) * kHeight / kWidth;
    }
    return 1.0;
}

Viewport computeViewport(const QSizeF& area, const DisplayOptions& options)
{
    Viewport v;
    v.source = sourceRect(options.overscan);
    const double par = pixelAspect(options.aspect);
    const double contentWidth = v.source.width() * par;
    const double contentHeight = v.source.height();
    if (area.isEmpty()) {
        return v;
    }
    double scale = std::min(area.width() / contentWidth, area.height() / contentHeight);
    if (options.integerScaling && scale >= 1.0) {
        // A hair of tolerance, so a window sized to exactly Nx is Nx.
        scale = std::floor(scale + 1e-6);
    }
    const double w = contentWidth * scale;
    const double h = contentHeight * scale;
    v.target = QRectF((area.width() - w) / 2.0, (area.height() - h) / 2.0, w, h);
    return v;
}

QSizeF scaledSize(int scale, const DisplayOptions& options)
{
    const QRect source = sourceRect(options.overscan);
    return {source.width() * pixelAspect(options.aspect) * scale,
        static_cast<double>(source.height() * scale)};
}

} // namespace cutenes::qt
