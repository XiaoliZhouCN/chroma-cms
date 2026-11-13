// 传递函数：已知值、端点、单调性、往返。

#include "TestSupport.hpp"

#include "color/Transfer.hpp"

#include <cmath>

using namespace chroma::color;
using namespace chroma::testing;

int main()
{
    // ---- sRGB（IEC 61966-2-1）----
    // 0.5 码值 -> 线性：((0.5+0.055)/1.055)^2.4 = 0.21404114048223255
    expectNear("srgb.decode(0.5)", decode(TransferFunction::Srgb, 0.5), 0.21404114048223255, 1e-12);
    expectNear("srgb.encode(0.214041...)", encode(TransferFunction::Srgb, 0.21404114048223255), 0.5,
               1e-12);
    expectNear("srgb.decode(0)", decode(TransferFunction::Srgb, 0.0), 0.0, 1e-15);
    expectNear("srgb.decode(1)", decode(TransferFunction::Srgb, 1.0), 1.0, 1e-12);
    for (const double code : {0.001, 0.2, 0.5, 0.8, 1.0}) {
        expectNear("srgb.roundtrip", encode(TransferFunction::Srgb,
                                            decode(TransferFunction::Srgb, code)),
                   code, 1e-12);
    }
    // 分段边界（0.04045 <-> 0.0031308）：规范给的是四舍五入后的阈值，边界处
    // 两段公式本身相差 ~3e-8，因此这里放宽到 1e-7（不是实现误差）。
    expectNear("srgb.boundary.roundtrip",
               encode(TransferFunction::Srgb, decode(TransferFunction::Srgb, 0.04045)), 0.04045,
               1e-7);

    // ---- gamma 2.2 / 2.4 ----
    expectNear("gamma22.decode(0.5)", decode(TransferFunction::Gamma22, 0.5),
               0.217637640824031, 1e-12);
    expectNear("gamma22.encode(0.2176...)", encode(TransferFunction::Gamma22, 0.217637640824031),
               0.5, 1e-12);
    for (const double code : {0.1, 0.5, 0.9, 1.0}) {
        expectNear("gamma22.roundtrip",
                   encode(TransferFunction::Gamma22, decode(TransferFunction::Gamma22, code)), code,
                   1e-12);
        expectNear("gamma24.roundtrip",
                   encode(TransferFunction::Gamma24, decode(TransferFunction::Gamma24, code)), code,
                   1e-12);
    }

    // ---- PQ（ST.2084）：码值 -> 相对亮度（1.0 = 10000 nits）----
    // 参考值由独立数值计算得出（见 docs/color/05）。
    expectNear("pq.decode(0)", decode(TransferFunction::Pq, 0.0), 0.0, 1e-15);
    expectNear("pq.decode(1)", decode(TransferFunction::Pq, 1.0), 1.0, 1e-9);
    expectNear("pq.0.25.nits", decode(TransferFunction::Pq, 0.25) * 10000.0, 5.154176009833007,
               1e-9);
    expectNear("pq.0.5.nits", decode(TransferFunction::Pq, 0.5) * 10000.0, 92.24570899406527,
               1e-9);
    expectNear("pq.0.75.nits", decode(TransferFunction::Pq, 0.75) * 10000.0, 983.3778555870275,
               1e-9);
    for (const double code : {0.1, 0.25, 0.5, 0.75, 1.0}) {
        expectNear("pq.roundtrip", encode(TransferFunction::Pq, decode(TransferFunction::Pq, code)),
                   code, 1e-9);
    }

    // ---- HLG（BT.2100 的 OETF / OETF^-1，场景线性归一化）----
    expectNear("hlg.decode(0)", decode(TransferFunction::Hlg, 0.0), 0.0, 1e-15);
    // 0.5 -> 1/12；1.0 -> 1.0（常量取 BT.2100 公布的 8 位小数，端点误差 ~2.4e-8）
    expectNear("hlg.decode(0.5)", decode(TransferFunction::Hlg, 0.5), 1.0 / 12.0, 1e-15);
    expectNear("hlg.decode(1)", decode(TransferFunction::Hlg, 1.0), 1.0, 1e-6);
    for (const double code : {0.1, 0.25, 0.5, 0.75, 1.0}) {
        expectNear("hlg.roundtrip", encode(TransferFunction::Hlg, decode(TransferFunction::Hlg, code)),
                   code, 1e-12);
    }

    // ---- 单调性（0->1 不应回退）----
    for (const TransferFunction tf : {TransferFunction::Srgb, TransferFunction::Gamma22,
                                      TransferFunction::Pq, TransferFunction::Hlg}) {
        double previous = -1.0;
        for (int i = 0; i <= 100; ++i) {
            const double code = static_cast<double>(i) / 100.0;
            const double value = decode(tf, code);
            expectTrue(name(tf), value > previous);
            previous = value;
        }
    }

    // ---- Linear 恒等 ----
    expectNear("linear.decode", decode(TransferFunction::Linear, 0.42), 0.42, 1e-15);
    expectNear("linear.encode", encode(TransferFunction::Linear, 0.42), 0.42, 1e-15);

    return finish("unit.color_transfer");
}
