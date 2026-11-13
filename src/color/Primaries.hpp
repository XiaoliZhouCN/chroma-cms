#pragma once

#include "color/Transfer.hpp"
#include "color/Types.hpp"

namespace chroma::color
{
/// CIE 1931 xy 色度坐标下的 RGB 原色 + 白点。
struct Primaries
{
    double rx = 0.0;
    double ry = 0.0;
    double gx = 0.0;
    double gy = 0.0;
    double bx = 0.0;
    double by = 0.0;
    double wx = 0.0;
    double wy = 0.0;
};

// 标准色域（CIE 1931 xy，D65 白点）。
inline constexpr Primaries kSrgbPrimaries{0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290};
inline constexpr Primaries kDisplayP3Primaries{0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290};
inline constexpr Primaries kRec2020Primaries{0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290};

/// 白点 xy -> XYZ（Y = 1）。
Xyz whitePoint(const Primaries &primaries);

/// 色域 + 传递函数的组合（描述源/目标色彩空间）。
struct ColorSpace
{
    Primaries primaries;
    TransferFunction transfer;
};

inline constexpr ColorSpace kSrgbColorSpace{kSrgbPrimaries, TransferFunction::Srgb};
inline constexpr ColorSpace kDisplayP3ColorSpace{kDisplayP3Primaries, TransferFunction::Srgb};
inline constexpr ColorSpace kRec2020ColorSpace{kRec2020Primaries, TransferFunction::Pq};
} // namespace chroma::color
