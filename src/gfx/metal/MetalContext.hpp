#pragma once

#include <cstdint>
#include <memory>

namespace chroma::gfx
{
/// 原生 Metal 上下文：device / queue / CAMetalLayer / 子 NSView /
/// drawable / command buffer / present / resize。
///
/// 只负责"原生资源与帧生命周期"，不包含 PSO、纹理、clear、Draw。
/// 头文件保持纯 C++：ObjC 类型只出现在 .mm 里，对外用 void* 不透明句柄。
class MetalContext
{
public:
    MetalContext();
    ~MetalContext();

    MetalContext(const MetalContext &) = delete;
    MetalContext &operator=(const MetalContext &) = delete;

    void initialize(void *nativeView, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);
    void waitForGpu();

    /// 帧：取 drawable + 新建 command buffer；renderer 自己建 render pass/encoder。
    /// nextDrawable 返回 nil 时返回 false，调用方应跳过这一帧。
    bool beginFrame();

    /// presentDrawable + commit + waitUntilCompleted。
    void endFrame();

    /// 一次性命令（纹理上传）：新建 command buffer；renderer 自建 blit encoder。
    void beginCommandList();
    void submitAndWait();

    /// 不透明句柄：只在 .mm 里 __bridge 回 id<...> / CAMetalLayer*。
    void *currentDrawable() const;   // id<CAMetalDrawable>
    void *commandBuffer() const;     // id<MTLCommandBuffer>
    void *device() const;            // id<MTLDevice>
    void *queue() const;             // id<MTLCommandQueue>
    void *layer() const;             // CAMetalLayer*

    std::uint32_t colorFormat() const;   // MTLPixelFormat 原始值
    std::uint32_t width() const;
    std::uint32_t height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace chroma::gfx