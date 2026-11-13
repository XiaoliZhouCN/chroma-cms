#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "color/Types.hpp"

namespace chroma::testing
{
inline int failures = 0;

inline void expectNear(const char *name, double actual, double expected, double eps = 1e-9)
{
    if (std::fabs(actual - expected) > eps) {
        std::fprintf(stderr, "FAIL %s: actual %.17g expected %.17g (eps %.1e)\n", name, actual,
                     expected, eps);
        ++failures;
    }
}

inline void expectRgbNear(const char *name, const chroma::color::Rgb &actual,
                          const chroma::color::Rgb &expected, double eps = 1e-9)
{
    expectNear((std::string(name) + ".r").c_str(), actual.r, expected.r, eps);
    expectNear((std::string(name) + ".g").c_str(), actual.g, expected.g, eps);
    expectNear((std::string(name) + ".b").c_str(), actual.b, expected.b, eps);
}

inline void expectTrue(const char *name, bool condition)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++failures;
    }
}

inline int finish(const char *suite)
{
    if (failures != 0) {
        std::fprintf(stderr, "%s: %d failure(s)\n", suite, failures);
        return 1;
    }
    std::printf("%s ok\n", suite);
    return 0;
}
} // namespace chroma::testing
