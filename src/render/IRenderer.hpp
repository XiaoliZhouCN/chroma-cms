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
};

} // namespace chroma::render