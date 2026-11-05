#pragma once

#include <cstdint>

namespace chroma::media {
struct ImageData;
}

namespace chroma::render {

/// 渲染后端接口。
///
/// 当前（M1）只承担"显示一张静态图"：initialize → setImage → present。
/// M4 引入视频后，present 会改为接收帧数据；接口变更时同步 Docs/ARCHITECTURE.md §6。
///
/// 约束：接口里不出现任何 D3D12 类型（见 Docs/ARCHITECTURE.md §6）。
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// nativeWindow 是 HWND；width/height 为**物理像素**（已乘以 devicePixelRatio）。
    virtual bool initialize(void* nativeWindow, uint32_t width, uint32_t height) = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void setImage(const media::ImageData& image) = 0;
    virtual void present() = 0;
    virtual const char* backendName() const = 0;
};

} // namespace chroma::render
