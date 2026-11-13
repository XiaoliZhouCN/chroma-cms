#pragma once

#include "color/Types.hpp"

namespace chroma::color
{
/// PQ 编码的峰值亮度：线性相对亮度 1.0 对应 10000 nits。
inline constexpr double kPqPeakNits = 10000.0;

/// scRGB 参考亮度：1.0 scRGB 单位对应 80 nits。
inline constexpr double kScRgbRefNits = 80.0;

/// 相对亮度（1.0 = 10000 nits）-> scRGB 单位（1.0 = 80 nits）。
/// outputScale 是平台/显示配置的输出缩放（Windows 当前为 0.8，mac 为 1.0）。
Rgb relativeToScRgb(const Rgb &linearRelative, double outputScale);

/// scRGB 单位 -> 相对亮度（relativeToScRgb 的逆）。
Rgb scRgbToRelative(const Rgb &scRgb, double outputScale);
} // namespace chroma::color
