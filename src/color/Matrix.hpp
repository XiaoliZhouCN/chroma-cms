#pragma once

#include "color/Primaries.hpp"
#include "color/Types.hpp"

namespace chroma::color
{
/// 3×3 矩阵（行优先存储：m[row][col]）。
struct Mat3
{
    double m[3][3] = {};
};

Mat3 identity();
Mat3 multiply(const Mat3 &a, const Mat3 &b);

/// RGB -> XYZ（矩阵应为 rgbToXyzMatrix 的结果）。
Xyz applyRgbToXyz(const Mat3 &rgbToXyz, const Rgb &rgb);

/// XYZ -> RGB（矩阵应为 xyzToRgbMatrix 的结果）。
Rgb applyXyzToRgb(const Mat3 &xyzToRgb, const Xyz &xyz);

/// 由原色/白点推导 RGB -> XYZ 矩阵（Bruce Lindbloom 方法）。
Mat3 rgbToXyzMatrix(const Primaries &primaries);

/// RGB -> XYZ 的逆矩阵。
Mat3 xyzToRgbMatrix(const Primaries &primaries);

Mat3 invert(const Mat3 &m);

/// 白点适应（Bradford）：返回作用在 XYZ 上的矩阵，使 srcWhite 映射到 dstWhite。
Mat3 bradfordAdaptation(const Xyz &srcWhite, const Xyz &dstWhite);

/// 对 XYZ 应用白点适应矩阵。
Xyz applyAdaptation(const Mat3 &adaptationXyz, const Xyz &xyz);
} // namespace chroma::color
