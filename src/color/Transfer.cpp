#include "color/Transfer.hpp"

#include <algorithm>
#include <cmath>

namespace chroma::color
{
namespace
{
// ---- ST.2084（PQ）常量 ----
constexpr double kPqM1 = 0.1593017578125;   // 2610/16384
constexpr double kPqM2 = 78.84375;          // 2523/4096 * 128
constexpr double kPqC1 = 0.8359375;         // 3424/4096
constexpr double kPqC2 = 18.8515625;        // 2413/4096 * 32
constexpr double kPqC3 = 18.6875;           // 2392/4096 * 32

// ---- ARIB STD-B67（HLG）常量 ----
constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 0.28466892;
constexpr double kHlgC = 0.55991073;

double pqEotf(double code)
{
    const double p = std::pow(std::max(code, 0.0), 1.0 / kPqM2);
    const double numerator = std::max(p - kPqC1, 0.0);
    const double denominator = std::max(kPqC2 - kPqC3 * p, 1e-6);
    return std::pow(numerator / denominator, 1.0 / kPqM1);
}

double pqInverseEotf(double linear)
{
    const double n = std::pow(std::max(linear, 0.0), kPqM1);
    return std::pow((kPqC1 + kPqC2 * n) / (1.0 + kPqC3 * n), kPqM2);
}

double hlgInverseOetf(double code)
{
    // HLG OETF^-1：码值 -> 场景线性（归一化）。
    if (code <= 0.5) {
        return code * code / 3.0;
    }
    return (std::exp((code - kHlgC) / kHlgA) + kHlgB) / 12.0;
}

double hlgOetf(double linear)
{
    if (linear <= 1.0 / 12.0) {
        return std::sqrt(3.0 * std::max(linear, 0.0));
    }
    return kHlgA * std::log(12.0 * linear - kHlgB) + kHlgC;
}
} // namespace

double decode(TransferFunction tf, double code)
{
    switch (tf) {
    case TransferFunction::Linear:
        return code;
    case TransferFunction::Srgb:
        return (code <= 0.04045) ? (code / 12.92) : std::pow((code + 0.055) / 1.055, 2.4);
    case TransferFunction::Gamma22:
        return std::pow(std::max(code, 0.0), 2.2);
    case TransferFunction::Gamma24:
        return std::pow(std::max(code, 0.0), 2.4);
    case TransferFunction::Pq:
        return pqEotf(code);
    case TransferFunction::Hlg:
        return hlgInverseOetf(code);
    }
    return code;
}

Rgb decode(TransferFunction tf, const Rgb &code)
{
    return {decode(tf, code.r), decode(tf, code.g), decode(tf, code.b)};
}

double encode(TransferFunction tf, double linear)
{
    switch (tf) {
    case TransferFunction::Linear:
        return linear;
    case TransferFunction::Srgb:
        return (linear <= 0.0031308) ? (12.92 * linear)
                                     : (1.055 * std::pow(linear, 1.0 / 2.4) - 0.055);
    case TransferFunction::Gamma22:
        return std::pow(std::max(linear, 0.0), 1.0 / 2.2);
    case TransferFunction::Gamma24:
        return std::pow(std::max(linear, 0.0), 1.0 / 2.4);
    case TransferFunction::Pq:
        return pqInverseEotf(linear);
    case TransferFunction::Hlg:
        return hlgOetf(linear);
    }
    return linear;
}

Rgb encode(TransferFunction tf, const Rgb &linear)
{
    return {encode(tf, linear.r), encode(tf, linear.g), encode(tf, linear.b)};
}

const char *name(TransferFunction tf)
{
    switch (tf) {
    case TransferFunction::Linear:
        return "linear";
    case TransferFunction::Srgb:
        return "sRGB";
    case TransferFunction::Gamma22:
        return "gamma 2.2";
    case TransferFunction::Gamma24:
        return "gamma 2.4";
    case TransferFunction::Pq:
        return "PQ (ST.2084)";
    case TransferFunction::Hlg:
        return "HLG (BT.2100)";
    }
    return "unknown";
}
} // namespace chroma::color
