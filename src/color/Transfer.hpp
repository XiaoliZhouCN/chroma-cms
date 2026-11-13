#pragma once

#include "color/Types.hpp"

namespace chroma::color
{
/// 传递函数（Transfer Characteristics）：码值 <-> 线性光。
///
/// 约定：码值与线性值都归一化到 [0, 1]；PQ/HLG 的线性值以"1.0 = 10000 nits 的相对亮度"
/// 为基准（与现有 HLSL/MSL/SkSL 实现保持一致），绝对亮度换算见 Output.hpp。
enum class TransferFunction
{
    Linear,
    Srgb,
    Gamma22,
    Gamma24,
    Pq,
    Hlg,
};

/// 码值 -> 线性光（EOTF / 反 OETF）。
double decode(TransferFunction tf, double code);
Rgb decode(TransferFunction tf, const Rgb &code);

/// 线性光 -> 码值（OETF / 反 EOTF）。
double encode(TransferFunction tf, double linear);
Rgb encode(TransferFunction tf, const Rgb &linear);

const char *name(TransferFunction tf);
} // namespace chroma::color
