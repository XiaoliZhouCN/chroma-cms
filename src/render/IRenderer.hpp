#pragma once

#include <cstdint>

namespace chroma::render {

/// 渲染后端接口。平台无关：这里不允许出现 D3D12 / HWND / Metal 的任何类型。
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// nativeWindow 是 HWND；width/height 为**物理像素**（已乘 devicePixelRatio）。
    virtual bool initialize(void* nativeWindow, std::uint32_t width, std::uint32_t height) = 0;

    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    virtual void present() = 0;

    virtual const char* backendName() const = 0;

    /// 上传一张 RGBA16_UNORM 图像（宽高 + 像素数据；调用后数据即可释放）
    virtual void setImage(std::uint32_t width, std::uint32_t height,
                          const std::uint16_t* rgba16) = 0;
};

} // namespace chroma::render