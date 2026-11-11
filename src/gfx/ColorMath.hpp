#pragma once

#include <algorithm>
#include <cmath>

namespace chroma::gfx
{
/// CPU 参考色彩数学：与 MSL / HLSL / SkSL 使用同一组常量。
/// 共享到 XYZ，最后一步矩阵按平台不同。
struct Rgb
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

inline constexpr double kPqPeakNits = 10000.0;
inline constexpr double kScRgbRefNits = 80.0;

/// ST.2084 PQ EOTF：归一化码值 -> 相对亮度（1.0 = 10000 nits）。
inline Rgb pqEotf(const Rgb &code)
{
    constexpr double m1 = 0.1593017578125;
    constexpr double m2 = 78.84375;
    constexpr double c1 = 0.8359375;
    constexpr double c2 = 18.8515625;
    constexpr double c3 = 18.6875;

    const auto channel = [](double value) {
        const double p = std::pow(std::max(value, 0.0), 1.0 / m2);
        const double numerator = std::max(p - c1, 0.0);
        const double denominator = std::max(c2 - c3 * p, 1e-6);
        return std::pow(numerator / denominator, 1.0 / m1);
    };

    return {channel(code.r), channel(code.g), channel(code.b)};
}

/// BT.2020 线性 RGB -> CIE XYZ (D65)。
inline Rgb rec2020ToXyz(const Rgb &v)
{
    return {
        0.636958 * v.r + 0.144617 * v.g + 0.168881 * v.b,
        0.262700 * v.r + 0.677998 * v.g + 0.059302 * v.b,
        0.000000 * v.r + 0.028073 * v.g + 1.060985 * v.b,
    };
}

/// CIE XYZ -> BT.709 线性 RGB (D65)，Windows / scRGB 路径。
inline Rgb xyzToRec709(const Rgb &v)
{
    return {
         3.240970 * v.r - 1.537383 * v.g - 0.498611 * v.b,
        -0.969244 * v.r + 1.875968 * v.g + 0.041555 * v.b,
         0.055630 * v.r - 0.203977 * v.g + 1.056972 * v.b,
    };
}

/// CIE XYZ -> Display-P3 线性 RGB (D65)，macOS 路径。
inline Rgb xyzToDisplayP3(const Rgb &v)
{
    return {
         2.4934969119 * v.r - 0.9313836179 * v.g - 0.4027107845 * v.b,
        -0.8294889696 * v.r + 1.7626640603 * v.g + 0.0236246858 * v.b,
         0.0358458302 * v.r - 0.0761723893 * v.g + 0.9568845240 * v.b,
    };
}

/// 相对亮度 -> 输出单位：/80 × outputScale。
inline Rgb linearToOutput(const Rgb &linear, double outputScale)
{
    const double scale = (kPqPeakNits / kScRgbRefNits) * outputScale;
    return {linear.r * scale, linear.g * scale, linear.b * scale};
}

/// 负值裁掉（与 shader 里的 max(value, 0) 对应）。
inline Rgb clampNonNegative(const Rgb &v)
{
    return {std::max(v.r, 0.0), std::max(v.g, 0.0), std::max(v.b, 0.0)};
}
} // namespace chroma::gfx