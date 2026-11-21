#pragma once

#include <cstdint>
#include <vector>

namespace chroma::render {

struct PixelPoint {
    std::uint32_t x{};
    std::uint32_t y{};
};

struct RgbaF32 {
    float r{};
    float g{};
    float b{};
    float a{};
};

/// 可选能力：在 present 完成后读取 renderer 的最终 offscreen 输出。
/// 当前用于 Probe 的 Stage B 采样，不属于正常渲染路径。
class IRendererReadback {
public:
    virtual ~IRendererReadback() = default;

    virtual bool readPixels(const std::vector<PixelPoint>& points,
                            std::vector<RgbaF32>& out) = 0;
};

} // namespace chroma::render
