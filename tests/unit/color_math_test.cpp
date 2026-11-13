// CPU 色彩核心的"语义级"断言：白点、色域差、输出缩放、裁剪。
// 传递函数与矩阵的细节分别见 color_transfer_test.cpp / color_matrix_test.cpp。

#include "TestSupport.hpp"

#include "color/Matrix.hpp"
#include "color/Output.hpp"
#include "color/Primaries.hpp"
#include "color/Transfer.hpp"

#include <cmath>

using namespace chroma::color;
using namespace chroma::testing;

int main()
{
    // ---- 白点：BT.2020 的 (1,1,1) 必须是 D65 白点 XYZ ----
    // D65（x=0.3127, y=0.3290）推导：X = 0.3127/0.3290 = 0.9504559270516716，
    // Z = (1 - 0.3127 - 0.3290)/0.3290 = 1.0890577507598784。
    const Mat3 rec2020ToXyz = rgbToXyzMatrix(kRec2020Primaries);
    const Xyz xyzWhite = applyRgbToXyz(rec2020ToXyz, {1.0, 1.0, 1.0});
    expectNear("white.x", xyzWhite.x, 0.9504559270516716, 1e-9);
    expectNear("white.y", xyzWhite.y, 1.0, 1e-12);
    expectNear("white.z", xyzWhite.z, 1.0890577507598784, 1e-9);

    // 同一 XYZ 白点转到 BT.709 / P3，仍然是 (1,1,1)。
    expectRgbNear("rec709 white", applyXyzToRgb(xyzToRgbMatrix(kSrgbPrimaries), xyzWhite),
                  {1.0, 1.0, 1.0}, 1e-9);
    expectRgbNear("p3 white", applyXyzToRgb(xyzToRgbMatrix(kDisplayP3Primaries), xyzWhite),
                  {1.0, 1.0, 1.0}, 1e-9);

    // ---- 输出缩放：相对亮度 1.0（=10000 nits）-> scRGB 单位（1.0 = 80 nits）----
    expectRgbNear("scrgb scale 0.8", relativeToScRgb({1.0, 1.0, 1.0}, 0.8), {100.0, 100.0, 100.0},
                  1e-9);
    expectRgbNear("scrgb scale 1.0", relativeToScRgb({1.0, 1.0, 1.0}, 1.0), {125.0, 125.0, 125.0},
                  1e-9);
    // 逆变换往返
    expectRgbNear("scrgb roundtrip", scRgbToRelative(relativeToScRgb({0.25, 0.5, 1.0}, 0.8), 0.8),
                  {0.25, 0.5, 1.0}, 1e-12);

    // ---- 同一 XYZ 下，709 与 P3 的饱和红输出不同（证明最后一步矩阵按目标色域不同）----
    const Xyz xyzRed = applyRgbToXyz(rec2020ToXyz, {1.0, 0.0, 0.0});
    const Rgb red709 = clampNonNegative(applyXyzToRgb(xyzToRgbMatrix(kSrgbPrimaries), xyzRed));
    const Rgb redP3 = clampNonNegative(applyXyzToRgb(xyzToRgbMatrix(kDisplayP3Primaries), xyzRed));
    expectTrue("gamut.red.diff", std::fabs(red709.r - redP3.r) > 0.1);

    // ---- 负值裁剪 ----
    expectRgbNear("clampNonNegative", clampNonNegative({-1.0, 0.5, -0.25}), {0.0, 0.5, 0.0},
                  1e-12);

    return finish("unit.color_math");
}
