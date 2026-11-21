#include "render/metal/MetalRendererImpl.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace chroma::render
{
namespace
{
// 清屏色：与 Windows 第 2 步保持一致，线性 (0.02, 0.02, 0.02)。
constexpr float kClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

std::string nsErrorText(NSError *error)
{
    if (error == nil) {
        return "未知错误";
    }
    return std::string([[error localizedDescription] UTF8String]);
}

// 与 MSL 里的 ShaderParams 布局一致：8 个 float = 32 字节。
struct ShaderParams
{
    float outputScale;
    float aspect;      // imageWidth / imageHeight
    float m00;         // 采样逆变换矩阵，已经包含 1/zoom
    float m01;
    float m10;
    float m11;
    float reserved0;
    float reserved1;
};
} // namespace

struct MetalRenderer::Impl::ObjcState
{
    id<MTLLibrary>             library  = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLTexture>             texture  = nil;
    id<MTLSamplerState>        sampler  = nil;
};

MetalRenderer::Impl::~Impl()
{
    delete objc;
}

bool MetalRenderer::Impl::initializeObjc(void *nativeWindow,
                                         std::uint32_t width,
                                         std::uint32_t height)
{
    if (initialized) {
        return true;
    }
    if (nativeWindow == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("MetalRenderer::initialize 参数无效");
    }

    context = std::make_unique<gfx::MetalContext>();
    context->initialize(nativeWindow, width, height);

    id<MTLDevice> device = (__bridge id<MTLDevice>)context->device();
    if (device == nil) {
        throw std::runtime_error("MetalContext 没有 MTLDevice");
    }

    NSString *executablePath = [[NSBundle mainBundle] executablePath];
    if (executablePath == nil) {
        throw std::runtime_error("找不到可执行文件路径");
    }
    NSString *shaderPath = [[executablePath stringByDeletingLastPathComponent]
        stringByAppendingPathComponent:@"shaders/fullscreen.metal"];

    NSError *error = nil;
    NSString *source = [NSString stringWithContentsOfFile:shaderPath
                                                 encoding:NSUTF8StringEncoding
                                                    error:&error];
    if (source == nil) {
        throw std::runtime_error("读取 MSL 失败: " + std::string([shaderPath UTF8String]) +
                                 " : " + nsErrorText(error));
    }

    MTLCompileOptions *compileOptions = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (@available(macOS 15.0, *)) {
        compileOptions.mathMode = MTLMathModeSafe;
    }
    else {
        compileOptions.fastMathEnabled = NO;
    }
#pragma clang diagnostic pop

    error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source
                                                  options:compileOptions
                                                    error:&error];
    if (library == nil) {
        throw std::runtime_error("MSL 编译失败: " + nsErrorText(error));
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"VSMain"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"PSMain"];
    if (vertexFunction == nil || fragmentFunction == nil) {
        throw std::runtime_error("MSL 里找不到 VSMain / PSMain");
    }

    MTLRenderPipelineDescriptor *pipelineDescriptor = [MTLRenderPipelineDescriptor new];
    pipelineDescriptor.label = @"fullscreen";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
    pipelineDescriptor.colorAttachments[0].pixelFormat =
        static_cast<MTLPixelFormat>(context->colorFormat());

    error = nil;
    id<MTLRenderPipelineState> pipeline =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (pipeline == nil) {
        throw std::runtime_error("创建 Metal PSO 失败: " + nsErrorText(error));
    }

    MTLSamplerDescriptor *samplerDescriptor = [MTLSamplerDescriptor new];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDescriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
    samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;

    id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:samplerDescriptor];
    if (sampler == nil) {
        throw std::runtime_error("创建 Metal sampler 失败");
    }

    objc = new ObjcState();
    objc->library = library;
    objc->pipeline = pipeline;
    objc->sampler = sampler;
    initialized = true;

    std::fprintf(stderr, "[gfx] MSL compiled: VSMain / PSMain\n");
    return true;
}

void MetalRenderer::Impl::presentObjc()
{
    if (!initialized) {
        return;
    }

    if (!context->beginFrame()) {
        std::fprintf(stderr, "[gfx] nextDrawable 返回 nil，跳过这一帧\n");
        return;
    }

    id<CAMetalDrawable> drawable =
        (__bridge id<CAMetalDrawable>)context->currentDrawable();
    id<MTLCommandBuffer> commandBuffer =
        (__bridge id<MTLCommandBuffer>)context->commandBuffer();

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(
        kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]);

    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:pass];

    if (objc->texture == nil) {
        [encoder endEncoding];
        context->endFrame();
        return;
    }

    [encoder setRenderPipelineState:objc->pipeline];
    [encoder setFragmentTexture:objc->texture atIndex:0];
    [encoder setFragmentSamplerState:objc->sampler atIndex:0];

    const int q = quarterTurns & 3;
    const float c = (q == 0) ? 1.0f : (q == 2) ? -1.0f : 0.0f;
    const float s = (q == 1) ? 1.0f : (q == 3) ? -1.0f : 0.0f;
    const float invZoom = (zoom > 0.0f) ? (1.0f / zoom) : 1.0f;
    const float aspectForShader = (imageHeight > 0)
        ? static_cast<float>(imageWidth) / static_cast<float>(imageHeight)
        : 1.0f;

    ShaderParams params = {1.0f, aspectForShader,
                           c * invZoom, -s * invZoom,
                           s * invZoom, c * invZoom,
                           0.0f, 0.0f};
    [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];

    const double drawableWidth = static_cast<double>(drawable.texture.width);
    const double drawableHeight = static_cast<double>(drawable.texture.height);
    double viewWidth = drawableWidth;
    double viewHeight = drawableHeight;
    if (imageWidth > 0 && imageHeight > 0) {
        const double imageAspect = static_cast<double>(imageWidth) /
                                   static_cast<double>(imageHeight);
        const double effectiveAspect = ((quarterTurns & 1) != 0)
            ? (1.0 / imageAspect)
            : imageAspect;
        const double targetAspect = drawableWidth / drawableHeight;
        if (effectiveAspect > targetAspect) {
            viewHeight = drawableWidth / effectiveAspect;
        }
        else {
            viewWidth = drawableHeight * effectiveAspect;
        }
    }

    const double viewLeft = (drawableWidth - viewWidth) * 0.5;
    const double viewTop = (drawableHeight - viewHeight) * 0.5;

    const MTLViewport viewport = {
        viewLeft, viewTop,
        viewWidth, viewHeight,
        0.0, 1.0};
    [encoder setViewport:viewport];

    const MTLScissorRect scissor = {
        static_cast<NSUInteger>(viewLeft), static_cast<NSUInteger>(viewTop),
        static_cast<NSUInteger>(viewWidth), static_cast<NSUInteger>(viewHeight)};
    [encoder setScissorRect:scissor];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];

    context->endFrame();
}

void MetalRenderer::Impl::setImageObjc(std::uint32_t width,
                                       std::uint32_t height,
                                       const std::uint16_t *rgba16)
{
    if (!initialized || rgba16 == nullptr || width == 0 || height == 0) {
        return;
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)context->device();

    MTLTextureDescriptor *textureDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModePrivate;

    id<MTLTexture> texture = [device newTextureWithDescriptor:textureDescriptor];
    if (texture == nil) {
        throw std::runtime_error("创建 Metal 纹理失败");
    }

    const NSUInteger sourceRowBytes = static_cast<NSUInteger>(width) * 8;
    const NSUInteger bytesPerRow = ((sourceRowBytes + 255) / 256) * 256;

    std::vector<std::uint8_t> staging(static_cast<std::size_t>(bytesPerRow) * height);
    if (bytesPerRow == sourceRowBytes) {
        std::memcpy(staging.data(), rgba16,
                    static_cast<std::size_t>(sourceRowBytes) * height);
    }
    else {
        const auto *source = reinterpret_cast<const std::uint8_t *>(rgba16);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(staging.data() + static_cast<std::size_t>(row) * bytesPerRow,
                        source + static_cast<std::size_t>(row) * sourceRowBytes,
                        sourceRowBytes);
        }
    }

    id<MTLBuffer> stagingBuffer =
        [device newBufferWithBytes:staging.data()
                            length:staging.size()
                           options:MTLResourceStorageModeShared];
    if (stagingBuffer == nil) {
        throw std::runtime_error("创建 Metal 暂存缓冲失败");
    }

    context->beginCommandList();
    id<MTLCommandBuffer> commandBuffer =
        (__bridge id<MTLCommandBuffer>)context->commandBuffer();
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];

    [blit copyFromBuffer:stagingBuffer
            sourceOffset:0
       sourceBytesPerRow:bytesPerRow
     sourceBytesPerImage:bytesPerRow * height
              sourceSize:MTLSizeMake(width, height, 1)
               toTexture:texture
        destinationSlice:0
        destinationLevel:0
       destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];

    context->submitAndWait();

    objc->texture = texture;
    imageWidth = width;
    imageHeight = height;
    std::fprintf(stderr, "[gfx] texture uploaded = %ux%u (%lu B/row)\n", width, height,
                 static_cast<unsigned long>(bytesPerRow));
}
} // namespace chroma::render
