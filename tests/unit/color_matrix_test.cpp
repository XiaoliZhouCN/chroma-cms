// 矩阵：由 primaries+白点推导的矩阵 vs 现有 shader 常量、求逆、Bradford 白点适应。

#include "TestSupport.hpp"

#include "color/Matrix.hpp"
#include "color/Primaries.hpp"

#include <cmath>

using namespace chroma::color;
using namespace chroma::testing;

namespace
{
// 现有 HLSL/MSL/SkSL 中手写的常量矩阵（来自 shaders/fullscreen.*）。
// 它们是按标准色度值推导后四舍五入的结果，因此与"由 primaries 推导"的矩阵
// 只保证到 1e-4 量级一致——这条断言本身就是"推导实现正确"的交叉校验。
constexpr double kRec2020ToXyzRows[3][3] = {
    {0.636958, 0.144617, 0.168881},
    {0.262700, 0.677998, 0.059302},
    {0.000000, 0.028073, 1.060985},
};

constexpr double kXyzToRec709Rows[3][3] = {
    {3.240970, -1.537383, -0.498611},
    {-0.969244, 1.875968, 0.041555},
    {0.055630, -0.203977, 1.056972},
};

constexpr double kXyzToDisplayP3Rows[3][3] = {
    {2.4934969119, -0.9313836179, -0.4027107845},
    {-0.8294889696, 1.7626640603, 0.0236246858},
    {0.0358458302, -0.0761723893, 0.9568845240},
};

void expectMatrixNear(const char *name, const Mat3 &actual, const double expected[3][3],
                      double eps)
{
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const std::string label = std::string(name) + "[" + std::to_string(row) + "][" +
                                      std::to_string(col) + "]";
            expectNear(label.c_str(), actual.m[row][col], expected[row][col], eps);
        }
    }
}
} // namespace

int main()
{
    // ---- 推导矩阵 vs 手写常量 ----
    expectMatrixNear("rec2020ToXyz", rgbToXyzMatrix(kRec2020Primaries), kRec2020ToXyzRows, 1e-4);
    expectMatrixNear("xyzToRec709", xyzToRgbMatrix(kSrgbPrimaries), kXyzToRec709Rows, 1e-4);
    expectMatrixNear("xyzToDisplayP3", xyzToRgbMatrix(kDisplayP3Primaries), kXyzToDisplayP3Rows,
                     1e-4);

    // ---- 互为逆矩阵：M * M^-1 = I ----
    const Mat3 product = multiply(rgbToXyzMatrix(kRec2020Primaries),
                                  xyzToRgbMatrix(kRec2020Primaries));
    const Mat3 identityExpected = identity();
    expectMatrixNear("inverse", product, identityExpected.m, 1e-12);

    // ---- 白点适应：D65 -> D50 应该把 D65 白点映射到 D50 白点 ----
    const Xyz d65 = whitePoint(kSrgbPrimaries);   // (0.9504559, 1, 1.0890578)
    Primaries d50 = kSrgbPrimaries;               // 同原色，D50 白点
    d50.wx = 0.3457;
    d50.wy = 0.3585;
    const Xyz d50White = whitePoint(d50);

    const Mat3 d65ToD50 = bradfordAdaptation(d65, d50White);
    const Xyz adapted = applyAdaptation(d65ToD50, d65);
    expectNear("bradford.x", adapted.x, d50White.x, 1e-9);
    expectNear("bradford.y", adapted.y, d50White.y, 1e-9);
    expectNear("bradford.z", adapted.z, d50White.z, 1e-9);

    // 往返：D65 -> D50 -> D65
    const Mat3 d50ToD65 = bradfordAdaptation(d50White, d65);
    const Xyz roundtrip = applyAdaptation(d50ToD65, adapted);
    expectNear("bradford.roundtrip.x", roundtrip.x, d65.x, 1e-12);
    expectNear("bradford.roundtrip.y", roundtrip.y, d65.y, 1e-12);
    expectNear("bradford.roundtrip.z", roundtrip.z, d65.z, 1e-12);

    return finish("unit.color_matrix");
}
