#pragma once

#include "render/metal/MetalRenderer.hpp"

#include "gfx/metal/MetalContext.hpp"

#include <cstdint>
#include <memory>

namespace chroma::render
{
/// MetalRenderer 的私有实现状态。
/// C++ 部分（context / 图像尺寸 / 交互）放这里；
/// ObjC 资源只前置声明，完整定义在 MetalRenderer.mm。
struct MetalRenderer::Impl
{
    // 原生资源由 MetalContext 持有；放在最前面，保证最后析构。
    std::unique_ptr<gfx::MetalContext> context;

    struct ObjcState;
    ObjcState *objc = nullptr;  // 由 MetalRenderer.mm 创建/释放

    std::uint32_t imageWidth  = 0;
    std::uint32_t imageHeight = 0;
    int   quarterTurns = 0;
    float zoom         = 1.0f;
    bool  initialized  = false;

    // MetalRenderer.cpp -> MetalRenderer.mm 的转发入口。
    bool initializeObjc(void *nativeWindow, std::uint32_t width, std::uint32_t height);
    void presentObjc();
    void setImageObjc(std::uint32_t width, std::uint32_t height,
                      const std::uint16_t *rgba16);

    ~Impl();
};
} // namespace chroma::render
