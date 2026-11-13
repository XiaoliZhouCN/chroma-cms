#include "color/Output.hpp"

namespace chroma::color
{
Rgb relativeToScRgb(const Rgb &linearRelative, double outputScale)
{
    const double scale = (kPqPeakNits / kScRgbRefNits) * outputScale;
    return {linearRelative.r * scale, linearRelative.g * scale, linearRelative.b * scale};
}

Rgb scRgbToRelative(const Rgb &scRgb, double outputScale)
{
    const double scale = (kScRgbRefNits / kPqPeakNits) / outputScale;
    return {scRgb.r * scale, scRgb.g * scale, scRgb.b * scale};
}
} // namespace chroma::color
