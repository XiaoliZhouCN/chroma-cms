#pragma once

#include "render/IRenderer.hpp"
#include "render/IViewTransform.hpp"

#include <cstdint>
#include <memory>

namespace chroma::render
{
/// 第三后端：Skia（Ganesh）桥接。
/// 目标是"两个平台共用同一套绘制逻辑"：本类只写 SkCanvas / SkSL / 图像，
/// 原生上下文（device/queue/交换链）由 gfx:: 层提供。
class SkiaRenderer final : public IRenderer, public IViewTransform
{
public:
    SkiaRenderer();
    ~SkiaRenderer() override;

    SkiaRenderer(const SkiaRenderer &) = delete;
    SkiaRenderer &operator=(const SkiaRenderer &) = delete;

    bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) override;
    void resize(std::uint32_t width, std::uint32_t height) override;
    void present() override;
    const char *backendName() const override;

    void setImage(std::uint32_t width, std::uint32_t height,
                  const std::uint16_t *rgba16) override;

    void setQuarterTurns(int quarterTurns) override;
    void setZoom(float zoom) override;

private:
    void createOffscreenSurface();
    void drawFrame();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace chroma::render