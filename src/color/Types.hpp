#pragma once

#include <algorithm>

namespace chroma::color
{
/// 线性 RGB 值（double 精度；CPU 参考实现统一用 double）。
struct Rgb
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

/// CIE XYZ 值（Y = 1 为参考白）。
struct Xyz
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/// 裁掉负值：色域外转换会产生负值。
inline Rgb clampNonNegative(const Rgb &v)
{
    return {std::max(v.r, 0.0), std::max(v.g, 0.0), std::max(v.b, 0.0)};
}

/// 归一化裁剪到 [0, 1]。
inline Rgb clamp01(const Rgb &v)
{
    return {std::clamp(v.r, 0.0, 1.0), std::clamp(v.g, 0.0, 1.0), std::clamp(v.b, 0.0, 1.0)};
}
} // namespace chroma::color
