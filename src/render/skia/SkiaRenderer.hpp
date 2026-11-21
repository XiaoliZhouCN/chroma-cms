#pragma once

#include "render/IRenderer.hpp"
#include "render/IRendererReadback.hpp"
#include "render/IViewTransform.hpp"

#include <cstdint>
#include <memory>

namespace chroma::render
{
/// 第三后端：Skia（Ganesh）。
/// 本类负责平台无关的 SkSL / SkCanvas / 图像 / 交互；
/// 原生 context、Ganesh、离屏目标与 present 由 SkiaGpuBackend 负责。
class SkiaRenderer final : public IRenderer,
                      public IViewTransform,
                      public IRendererReadback
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

    bool readPixels(const std::vector<PixelPoint>& points,
                    std::vector<RgbaF32>& out) override;

private:
    void createOffscreenSurface();
    void drawFrame();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace chroma::render