#pragma once

#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"

#include <cstdint>
#include <memory>
#include <string>

class GrDirectContext;

namespace chroma::render
{
/// Skia 平台后端：只负责原生 context / Ganesh / 离屏目标 / present。
/// SkSL、SkCanvas、色彩 uniform、交互矩阵都留在 SkiaRenderer。
class SkiaGpuBackend
{
public:
    virtual ~SkiaGpuBackend() = default;

    virtual bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) = 0;
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    virtual sk_sp<GrDirectContext> makeGaneshContext() = 0;
    virtual sk_sp<SkSurface> createSurface(int width, int height) = 0;

    virtual bool beginFrame() = 0;
    virtual void endFrame(SkSurface &surface) = 0;

    virtual std::string shaderPath(const char *name) = 0;
    virtual const char *backendName() const = 0;
};

std::unique_ptr<SkiaGpuBackend> createSkiaGpuBackend();
} // namespace chroma::render
