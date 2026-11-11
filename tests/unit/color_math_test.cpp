#include "gfx/ColorMath.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
using chroma::gfx::Rgb;

int failures = 0;

void expectNear(const char *name, double actual, double expected, double eps = 1e-6)
{
    if (std::fabs(actual - expected) > eps) {
        std::fprintf(stderr, "FAIL %s: actual %.12f expected %.12f\n", name, actual, expected);
        ++failures;
    }
}

void expectRgbNear(const char *name, const Rgb &actual, const Rgb &expected, double eps = 1e-5)
{
    expectNear((std::string(name) + ".r").c_str(), actual.r, expected.r, eps);
    expectNear((std::string(name) + ".g").c_str(), actual.g, expected.g, eps);
    expectNear((std::string(name) + ".b").c_str(), actual.b, expected.b, eps);
}
} // namespace

int main()
{
    using namespace chroma::gfx;

    // PQ 端点
    expectRgbNear("pqEotf(0)", pqEotf({0.0, 0.0, 0.0}), {0.0, 0.0, 0.0}, 1e-12);
    expectRgbNear("pqEotf(1)", pqEotf({1.0, 1.0, 1.0}), {1.0, 1.0, 1.0}, 1e-6);

    // PQ 单调
    const Rgb samples[] = {
        pqEotf({0.0, 0.0, 0.0}),
        pqEotf({0.25, 0.25, 0.25}),
        pqEotf({0.5, 0.5, 0.5}),
        pqEotf({0.75, 0.75, 0.75}),
        pqEotf({1.0, 1.0, 1.0}),
    };
    for (int i = 1; i < 5; ++i) {
        if (!(samples[i].r > samples[i - 1].r)) {
            std::fprintf(stderr, "FAIL pqEotf monotonic at %d\n", i);
            ++failures;
        }
    }

    // 2020 白点 -> XYZ -> 709 / P3 白点
    const Rgb xyzWhite = rec2020ToXyz({1.0, 1.0, 1.0});
    expectRgbNear("xyzWhite", xyzWhite, {0.950456, 1.0, 1.089058}, 1e-5);
    expectRgbNear("xyzToRec709(white)", xyzToRec709(xyzWhite), {1.0, 1.0, 1.0}, 1e-5);
    expectRgbNear("xyzToDisplayP3(white)", xyzToDisplayP3(xyzWhite), {1.0, 1.0, 1.0}, 1e-5);

    // 输出缩放
    expectRgbNear("linearToOutput(1, 0.8)",
                  linearToOutput({1.0, 1.0, 1.0}, 0.8), {100.0, 100.0, 100.0}, 1e-9);
    expectRgbNear("linearToOutput(1, 1.0)",
                  linearToOutput({1.0, 1.0, 1.0}, 1.0), {125.0, 125.0, 125.0}, 1e-9);

    // 同一 XYZ 下，709 与 P3 的饱和红输出不同（证明最后矩阵按平台不同）
    const Rgb xyzRed = rec2020ToXyz({1.0, 0.0, 0.0});
    const Rgb red709 = clampNonNegative(xyzToRec709(xyzRed));
    const Rgb redP3 = clampNonNegative(xyzToDisplayP3(xyzRed));
    if (std::fabs(red709.r - redP3.r) < 0.1) {
        std::fprintf(stderr, "FAIL gamut matrix: 709/P3 red should differ\n");
        ++failures;
    }

    // 负值裁剪
    expectRgbNear("clampNonNegative",
                  clampNonNegative({-1.0, 0.5, -0.25}), {0.0, 0.5, 0.0}, 1e-12);

    if (failures != 0) {
        std::fprintf(stderr, "unit.color_math: %d failure(s)\n", failures);
        return 1;
    }

    std::printf("unit.color_math ok\n");
    return 0;
}