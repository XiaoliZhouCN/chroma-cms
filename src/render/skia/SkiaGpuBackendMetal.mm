#include "render/skia/SkiaGpuBackend.hpp"

#if defined(__APPLE__)

#include "gfx/metal/MetalContext.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurfaceProps.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlTypes.h"

#include <stdexcept>
#include <string>

namespace chroma::render
{
class SkiaGpuBackendMetal final : public SkiaGpuBackend
{
public:
    bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) override;
    void resize(std::uint32_t width, std::uint32_t height) override;

    sk_sp<GrDirectContext> makeGaneshContext() override;
    sk_sp<SkSurface> createSurface(int width, int height) override;

    bool beginFrame() override;
    void endFrame(SkSurface &surface) override;

    std::string shaderPath(const char *name) override;
    const char *backendName() const override;

private:
    std::unique_ptr<gfx::MetalContext> context;
    sk_sp<GrDirectContext> gr;
    id<MTLTexture> offscreenTexture = nil;
    GrBackendRenderTarget backendRT;

    std::uint32_t imageWidth = 0;
    std::uint32_t imageHeight = 0;

    bool initialized = false;
};

bool SkiaGpuBackendMetal::initialize(void *nativeWindow,
                                     std::uint32_t width,
                                     std::uint32_t height)
{
    if (initialized) {
        return true;
    }
    if (nativeWindow == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("SkiaGpuBackendMetal::initialize 参数无效");
    }

    imageWidth = width;
    imageHeight = height;

    context = std::make_unique<gfx::MetalContext>();
    context->initialize(nativeWindow, width, height);

    id<MTLDevice> device = (__bridge id<MTLDevice>)context->device();
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)context->queue();
    if (device == nil || queue == nil) {
        throw std::runtime_error("SkiaGpuBackendMetal: MetalContext 没有 device/queue");
    }

    GrMtlBackendContext backend;
    backend.fDevice.retain((__bridge GrMTLHandle)device);
    backend.fQueue.retain((__bridge GrMTLHandle)queue);

    gr = GrDirectContexts::MakeMetal(backend);
    if (!gr) {
        throw std::runtime_error("GrDirectContexts::MakeMetal 失败");
    }

    initialized = true;
    return true;
}

void SkiaGpuBackendMetal::resize(std::uint32_t width, std::uint32_t height)
{
    if (!initialized || width == 0 || height == 0) {
        return;
    }

    context->resize(width, height);
    offscreenTexture = nil;
    backendRT = GrBackendRenderTarget();
    imageWidth = width;
    imageHeight = height;
}

sk_sp<GrDirectContext> SkiaGpuBackendMetal::makeGaneshContext()
{
    return gr;
}

sk_sp<SkSurface> SkiaGpuBackendMetal::createSurface(int width, int height)
{
    if (!gr || !context || width <= 0 || height <= 0) {
        return nullptr;
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)context->device();
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;

    offscreenTexture = [device newTextureWithDescriptor:descriptor];
    if (offscreenTexture == nil) {
        throw std::runtime_error("SkiaGpuBackendMetal: 创建离屏 MTLTexture 失败");
    }

    GrMtlTextureInfo info;
    info.fTexture.retain((__bridge GrMTLHandle)offscreenTexture);
    GrBackendRenderTarget rt = GrBackendRenderTargets::MakeMtl(width, height, info);
    backendRT = rt;

    SkSurfaceProps props;
    sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
        gr.get(), rt, kTopLeft_GrSurfaceOrigin, kRGBA_F16_SkColorType, nullptr, &props);
    if (!surface) {
        throw std::runtime_error("SkSurfaces::WrapBackendRenderTarget 失败（Metal）");
    }
    return surface;
}

bool SkiaGpuBackendMetal::beginFrame()
{
    return context ? context->beginFrame() : false;
}

void SkiaGpuBackendMetal::endFrame(SkSurface &surface)
{
    (void)surface;
    if (!context || offscreenTexture == nil) {
        return;
    }

    id<CAMetalDrawable> drawable =
        (__bridge id<CAMetalDrawable>)context->currentDrawable();
    id<MTLCommandBuffer> commandBuffer =
        (__bridge id<MTLCommandBuffer>)context->commandBuffer();
    if (drawable == nil || commandBuffer == nil) {
        return;
    }

    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:offscreenTexture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(imageWidth, imageHeight, 1)
                 toTexture:drawable.texture
          destinationSlice:0
          destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];

    context->endFrame();
}

std::string SkiaGpuBackendMetal::shaderPath(const char *name)
{
    NSString *executable = [[NSBundle mainBundle] executablePath];
    if (executable == nil) {
        throw std::runtime_error("SkiaGpuBackendMetal: 找不到 executablePath");
    }
    NSString *directory = [executable stringByDeletingLastPathComponent];
    NSString *path = [directory stringByAppendingPathComponent:
        [NSString stringWithFormat:@"shaders/%s", name]];
    return std::string([path UTF8String]);
}

const char *SkiaGpuBackendMetal::backendName() const
{
    return "Skia (Ganesh Metal)";
}

std::unique_ptr<SkiaGpuBackend> createSkiaGpuBackend()
{
    return std::make_unique<SkiaGpuBackendMetal>();
}

} // namespace chroma::render

#endif // __APPLE__
