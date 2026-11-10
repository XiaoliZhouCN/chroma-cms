#pragma once

#include "render/IRenderer.hpp"
#include "render/IViewTransform.hpp"

#include <cstdint>
#include <memory>

namespace chroma::render
{
class MetalRenderer final : public IRenderer, public IViewTransform
{
public:
    MetalRenderer();
    ~MetalRenderer() override;

    // 不允许拷贝构造和拷贝赋值
    MetalRenderer(const MetalRenderer &) = delete;
    MetalRenderer &operator=(const MetalRenderer &) = delete;

    bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) override;
    void resize(std::uint32_t width, std::uint32_t height) override;
    void present() override;
    const char *backendName() const override;

    void setImage(std::uint32_t width, std::uint32_t height,
                  const std::uint16_t *rgba16) override;
    void setQuarterTurns(int quarterTurns) override;
    void setZoom(float zoom) override;

private:
    // Pimpl idiom: 将 MetalRenderer 的实现细节隐藏在 Impl 中，避免在头文件中暴露 Metal 相关的头文件，减少编译依赖。
    // Q: 和 D3D12Renderer 对应的地方在哪里？D3D12Common吗？
    // A: D3D12Renderer 中 的 那一堆 m_device / m_queue 成员
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace chroma::render
