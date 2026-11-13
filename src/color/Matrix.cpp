#include "color/Matrix.hpp"

#include <cmath>

namespace chroma::color
{
namespace
{
/// xy 色度 + 亮度 Y=1 -> XYZ。
Xyz xyzFromXy(double x, double y)
{
    return {x / y, 1.0, (1.0 - x - y) / y};
}
} // namespace

Mat3 identity()
{
    Mat3 result;
    result.m[0][0] = 1.0;
    result.m[1][1] = 1.0;
    result.m[2][2] = 1.0;
    return result;
}

Mat3 multiply(const Mat3 &a, const Mat3 &b)
{
    Mat3 result;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result.m[row][col] = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col] +
                                 a.m[row][2] * b.m[2][col];
        }
    }
    return result;
}

Xyz applyRgbToXyz(const Mat3 &rgbToXyz, const Rgb &rgb)
{
    return {rgbToXyz.m[0][0] * rgb.r + rgbToXyz.m[0][1] * rgb.g + rgbToXyz.m[0][2] * rgb.b,
            rgbToXyz.m[1][0] * rgb.r + rgbToXyz.m[1][1] * rgb.g + rgbToXyz.m[1][2] * rgb.b,
            rgbToXyz.m[2][0] * rgb.r + rgbToXyz.m[2][1] * rgb.g + rgbToXyz.m[2][2] * rgb.b};
}

Rgb applyXyzToRgb(const Mat3 &xyzToRgb, const Xyz &xyz)
{
    return {xyzToRgb.m[0][0] * xyz.x + xyzToRgb.m[0][1] * xyz.y + xyzToRgb.m[0][2] * xyz.z,
            xyzToRgb.m[1][0] * xyz.x + xyzToRgb.m[1][1] * xyz.y + xyzToRgb.m[1][2] * xyz.z,
            xyzToRgb.m[2][0] * xyz.x + xyzToRgb.m[2][1] * xyz.y + xyzToRgb.m[2][2] * xyz.z};
}

Mat3 invert(const Mat3 &m)
{
    // 伴随矩阵 / 行列式。
    const double c00 = m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1];
    const double c01 = m.m[1][2] * m.m[2][0] - m.m[1][0] * m.m[2][2];
    const double c02 = m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0];

    const double det = m.m[0][0] * c00 + m.m[0][1] * c01 + m.m[0][2] * c02;
    const double invDet = 1.0 / det;

    Mat3 result;
    result.m[0][0] = c00 * invDet;
    result.m[0][1] = (m.m[0][2] * m.m[2][1] - m.m[0][1] * m.m[2][2]) * invDet;
    result.m[0][2] = (m.m[0][1] * m.m[1][2] - m.m[0][2] * m.m[1][1]) * invDet;
    result.m[1][0] = c01 * invDet;
    result.m[1][1] = (m.m[0][0] * m.m[2][2] - m.m[0][2] * m.m[2][0]) * invDet;
    result.m[1][2] = (m.m[0][2] * m.m[1][0] - m.m[0][0] * m.m[1][2]) * invDet;
    result.m[2][0] = c02 * invDet;
    result.m[2][1] = (m.m[0][1] * m.m[2][0] - m.m[0][0] * m.m[2][1]) * invDet;
    result.m[2][2] = (m.m[0][0] * m.m[1][1] - m.m[0][1] * m.m[1][0]) * invDet;
    return result;
}

Mat3 rgbToXyzMatrix(const Primaries &primaries)
{
    // 1) 三个原色的"未缩放"XYZ：每列一个原色，Y=1。
    const Xyz red = xyzFromXy(primaries.rx, primaries.ry);
    const Xyz green = xyzFromXy(primaries.gx, primaries.gy);
    const Xyz blue = xyzFromXy(primaries.bx, primaries.by);

    Mat3 unscaled;
    unscaled.m[0][0] = red.x;
    unscaled.m[1][0] = red.y;
    unscaled.m[2][0] = red.z;
    unscaled.m[0][1] = green.x;
    unscaled.m[1][1] = green.y;
    unscaled.m[2][1] = green.z;
    unscaled.m[0][2] = blue.x;
    unscaled.m[1][2] = blue.y;
    unscaled.m[2][2] = blue.z;

    // 2) 求缩放系数 S，使 unscaled * S = 白点 XYZ。
    const Xyz white = whitePoint(primaries);
    const Mat3 inverse = invert(unscaled);
    const double sr = inverse.m[0][0] * white.x + inverse.m[0][1] * white.y + inverse.m[0][2] * white.z;
    const double sg = inverse.m[1][0] * white.x + inverse.m[1][1] * white.y + inverse.m[1][2] * white.z;
    const double sb = inverse.m[2][0] * white.x + inverse.m[2][1] * white.y + inverse.m[2][2] * white.z;

    // 3) 每列乘以对应缩放系数。
    Mat3 result = unscaled;
    for (int row = 0; row < 3; ++row) {
        result.m[row][0] *= sr;
        result.m[row][1] *= sg;
        result.m[row][2] *= sb;
    }
    return result;
}

Mat3 xyzToRgbMatrix(const Primaries &primaries)
{
    return invert(rgbToXyzMatrix(primaries));
}

Mat3 bradfordAdaptation(const Xyz &srcWhite, const Xyz &dstWhite)
{
    // Bradford 锥体响应矩阵。
    Mat3 cone;
    cone.m[0][0] = 0.8951;
    cone.m[0][1] = 0.2664;
    cone.m[0][2] = -0.1614;
    cone.m[1][0] = -0.7502;
    cone.m[1][1] = 1.7135;
    cone.m[1][2] = 0.0367;
    cone.m[2][0] = 0.0389;
    cone.m[2][1] = -0.0685;
    cone.m[2][2] = 1.0296;

    const Mat3 inverseCone = invert(cone);

    const auto lms = [&cone](const Xyz &c) {
        return Xyz{cone.m[0][0] * c.x + cone.m[0][1] * c.y + cone.m[0][2] * c.z,
                   cone.m[1][0] * c.x + cone.m[1][1] * c.y + cone.m[1][2] * c.z,
                   cone.m[2][0] * c.x + cone.m[2][1] * c.y + cone.m[2][2] * c.z};
    };

    const Xyz srcLms = lms(srcWhite);
    const Xyz dstLms = lms(dstWhite);

    Mat3 scale;
    scale.m[0][0] = dstLms.x / srcLms.x;
    scale.m[1][1] = dstLms.y / srcLms.y;
    scale.m[2][2] = dstLms.z / srcLms.z;

    return multiply(inverseCone, multiply(scale, cone));
}

Xyz applyAdaptation(const Mat3 &adaptationXyz, const Xyz &xyz)
{
    return {adaptationXyz.m[0][0] * xyz.x + adaptationXyz.m[0][1] * xyz.y + adaptationXyz.m[0][2] * xyz.z,
            adaptationXyz.m[1][0] * xyz.x + adaptationXyz.m[1][1] * xyz.y + adaptationXyz.m[1][2] * xyz.z,
            adaptationXyz.m[2][0] * xyz.x + adaptationXyz.m[2][1] * xyz.y + adaptationXyz.m[2][2] * xyz.z};
}
} // namespace chroma::color
