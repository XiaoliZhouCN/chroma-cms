#include "render/metal/MetalRenderer.hpp"

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
constexpr float kClearColor[4] = {0.02f, 0.02f, 0.02f, 1.0f};

std::string nsErrorText(NSError *error)
{
    if (error == nil) {
        return "未知错误";
    }
    return std::string([[error localizedDescription] UTF8String]);
}
// 与 MSL 里的 ShaderParams 布局一致：4 个 float = 16 字节。
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

// Q: 为什么 win 不用 struct 存放这些内容？
// A: 编译需要，win 的 ComPtr<ID3D12Device> 是 纯C++，.hpp 里不会引入非 C++ 语法，可以直接放成员；
//    id<MTLDevice>、CAMetalLayer*、NSView* 是 Objective-C 类型，在 cpp 文件中编译不通过；
// Q: IMPL是什么？NSView是什么？
// A: Impl implementation，pimpl（pointer to implementation), MetalRenderer 的嵌套结构体，定义在 .mm 里;
//    NSView 是 macOS AppKit 的视图基类；Qt 在 macOS 上的原生视图 QNSView 就是 NSView 的子类。
/*
命名前缀：
NS = NeXTSTEP（Foundation/AppKit）
MTL = Metal
CA = Core Animation
CG = Core Graphics
*/
// Q: 什么是ARC？
// A: ARC = Automatic Reference Counting，自动引用计数
struct MetalRenderer::Impl
{
    id<MTLDevice>              device     = nil;
    id<MTLCommandQueue>        queue      = nil;
    id<MTLLibrary>             library    = nil;        // Q: 什么是library？作用是什么？
                                                        // A: 运行时编译出来的 Metal 库, 相当于ID3DBlob（编译后的字节码容器）
                                                        //    CreateGraphicsPipelineState / newRenderPipelineState 只接受“已经编译好的着色器产物”，不接受 HLSL/MSL 源码。ID3DBlob 和 MTLLibrary 就是这个中间产物。
    id<MTLRenderPipelineState> pipeline   = nil;        // Pipline State， win 里也有
    id<MTLTexture>             texture    = nil;
    id<MTLSamplerState>        sampler    = nil;
    CAMetalLayer               *layer     = nil;
    NSView                     *host      = nil;
    NSView                     *metalView = nil;

    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::uint32_t imageWidth  = 0;
    std::uint32_t imageHeight = 0;
    int   quarterTurns = 0;
    float zoom         = 1.0f;

    bool initialized = false;
};

MetalRenderer::MetalRenderer(): m_impl(std::make_unique<Impl>()){}

MetalRenderer::~MetalRenderer()
{
    if (m_impl->metalView != nil) {
        [m_impl->metalView removeFromSuperview];
    }
}

bool MetalRenderer::initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height)
{
     if (m_impl->initialized) {
        return true;
    }
    if (nativeWindow == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("MetalRenderer::initialize 参数无效");
    }

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
        throw std::runtime_error("MTLCreateSystemDefaultDevice 返回 nil");
    }

    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (queue == nil) {
        throw std::runtime_error("newCommandQueue 返回 nil");
    }

    // ---- M2: 运行时编译 MSL + 建 PSO ----
    // 读取 MSL
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

    // Q: 这里在干什么？
    // A: 它是在给 MSL 运行时编译器设置编译选项
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

    // new library
    error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source
                                                  options:compileOptions
                                                    error:&error];
    if (library == nil) {
        throw std::runtime_error("MSL 编译失败: " + nsErrorText(error));
    }
    
    // 构建shader
    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"VSMain"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"PSMain"];
    if (vertexFunction == nil || fragmentFunction == nil) {
        throw std::runtime_error("MSL 里找不到 VSMain / PSMain");
    }

    // create pipeline descriptor
    MTLRenderPipelineDescriptor *pipelineDescriptor = [MTLRenderPipelineDescriptor new];
    pipelineDescriptor.label                            = @"fullscreen";                        // 全屏
    pipelineDescriptor.vertexFunction                   = vertexFunction;                       // 设置 vertex shader
    pipelineDescriptor.fragmentFunction                 = fragmentFunction;                     // 设置 fragment shader
    pipelineDescriptor.inputPrimitiveTopology           = MTLPrimitiveTopologyClassTriangle;    // 三角形
    pipelineDescriptor.colorAttachments[0].pixelFormat  = MTLPixelFormatRGBA16Float;            // 像素格式为f16 float

    // create pipline
    // Q: 哪里是PSO？
    // A: 就是 pipline，作为返回信息
    // 创建 PSO
    error = nil;
    id<MTLRenderPipelineState> pipeline =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (pipeline == nil) {
        throw std::runtime_error("创建 Metal PSO 失败: " + nsErrorText(error));
    }

    // 创建采样器（线性过滤 + clamp）
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

    NSView *host = (__bridge NSView *)nativeWindow;     // Q: 句柄？相当于winId？
                                                        // A: Yes. 平台原生窗口句柄,Windows 是 HWND，macOS 是 NSView*;
    NSView *metalView = [[NSView alloc] initWithFrame:host.bounds];
    metalView.wantsLayer = YES;
    metalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;


    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatRGBA16Float;
    layer.framebufferOnly = YES;
    layer.maximumDrawableCount = 2;
    layer.displaySyncEnabled = YES;
    layer.contentsScale = host.window != nil ? host.window.backingScaleFactor : 1.0;
    layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));

    // set CG Color Space， win 中 在 createSwapchain中设置， Metal中没有 swapchain概念，由CAMetalLayer统一管理
    // 明确告诉系统：缓冲区是线性值，且允许超过 1.0 的 EDR 内容。
    // 先不设置 CAEDRMetadata；M4.2 再处理 EDR tone mapping。
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
    if (colorSpace == nullptr) {
        throw std::runtime_error("CGColorSpaceCreateWithName(ExtendedLinearDisplayP3) 返回 nil");
    }
    layer.colorspace = colorSpace;
    CGColorSpaceRelease(colorSpace);

    // M1 先打开 EDR；M4 再补 CAEDRMetadata 做绝对亮度映射。
    layer.wantsExtendedDynamicRangeContent = YES;

    // Q: 为什么在这里赋值？
    // A: wantsLayer = YES 之后，AppKit 会给这个 view 准备一个 backing layer。
    //    然后把它的 layer 替换成自己的 CAMetalLayer。
    //    在 addSubview 之前挂好，子 view 一进入视图树就已经有正确的 layer，不会先闪一下空背景。
    metalView.layer = layer;
    [host addSubview:metalView];

    m_impl->device = device;
    m_impl->queue = queue;
    m_impl->library = library;
    m_impl->pipeline = pipeline;
    m_impl->sampler = sampler;
    m_impl->host = host;
    m_impl->metalView = metalView;
    m_impl->layer = layer;
    m_impl->width = width;
    m_impl->height = height;
    m_impl->initialized = true;

    std::fprintf(stderr, "[gfx] Metal device = %s\n", [[device name] UTF8String]);
    std::fprintf(stderr, "[gfx] MSL compiled: VSMain / PSMain\n");
    return true;
}

void MetalRenderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (!m_impl->initialized || width == 0 || height == 0) {
        return;
    }

    m_impl->width = width;
    m_impl->height = height;

    // subview 的 frame 用 point，drawableSize 用物理像素。
    // Q: 什么是 points？ 这里的 frame 和 win 中的什么概念对应？
    // A: macOS 的坐标默认用 point（逻辑单位），对应的物理单位是pixel。
    //    frame：这个 view 在父视图坐标系里的矩形（位置 + 大小）。
    //    bounds：这个 view 在自己坐标系里的矩形（通常是 (0,0,width,height)）。
    m_impl->metalView.frame = m_impl->host.bounds;
    m_impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width),
                                            static_cast<CGFloat>(height));
    // Q: 这一步在做什么？和 win 中什么对应？
    // A: 这个 layer 的 1 个 point 对应多少个像素，仍然在做窗口映射。
    //    单独的 contentsScale 跟  win 没有对应。
    if (m_impl->host.window != nil) {
        m_impl->layer.contentsScale = m_impl->host.window.backingScaleFactor;
    }
}

void MetalRenderer::present()
{
    if (!m_impl->initialized) {
        return;
    }

    // 1. get next drawable
    id<CAMetalDrawable> drawable = [m_impl->layer nextDrawable];        // Q: 这相当于 swapchain吗？
                                                                        // A: 概念上类似，实际逻辑更像swap chain + 呈现管理
    if (drawable == nil) {
        // drawable 池暂时耗尽：跳过这一帧，不要崩。
        std::fprintf(stderr, "[gfx] nextDrawable 返回 nil，跳过这一帧\n");
        return;
    }

    // 2. clear screen
    // Q: 相当于 win 的 Descriptor Heaps？
    // A: 不是，是描述这一趟 render pass 怎么开始/结束
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(                // clear screen
        kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]);

    
    // 3. create command buffer and command encoder
    // Q: 相当于 win 的 command list 和 command allocator？
    // A: MTLCommandBuffer ≈ command list + allocator + 提交单元
    //    MTLRenderCommandEncoder ≈ 真正录制绘制命令的 command list
    id<MTLCommandBuffer> commandBuffer = 
    [m_impl->queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:pass];
    // >> 开始录制命令
    // 图片为空
    if (m_impl->texture == nil) {
        // 图片还没准备好：只清屏。
        [encoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        return;
    }

    [encoder setRenderPipelineState:m_impl->pipeline];              // 设置 pipline status
    [encoder setFragmentTexture:m_impl->texture atIndex:0];         // 设置纹理
    [encoder setFragmentSamplerState:m_impl->sampler atIndex:0];    // 设置采样器

    // 对应 D3D12 的根常量 b0：outputScale / rotation / zoom / aspect， 并设置到shader
    // 90° 步进：CPU 直接合成旋转 + 缩放的 2×2 矩阵，shader 不再算 cos/sin。
    const int q = m_impl->quarterTurns & 3;
    const float c = (q == 0) ? 1.0f : (q == 2) ? -1.0f : 0.0f;
    const float s = (q == 1) ? 1.0f : (q == 3) ? -1.0f : 0.0f;
    const float invZoom = (m_impl->zoom > 0.0f) ? (1.0f / m_impl->zoom) : 1.0f;
    const float aspectForShader = (m_impl->imageHeight > 0)
        ? static_cast<float>(m_impl->imageWidth) / static_cast<float>(m_impl->imageHeight)
        : 1.0f;

    ShaderParams params = {1.0f, aspectForShader,
                           c * invZoom, -s * invZoom,
                           s * invZoom, c * invZoom,
                           0.0f, 0.0f};
    [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];

    // contain-fit：图片长边贴满，短边居中留边。
    const double drawableWidth = static_cast<double>(drawable.texture.width);
    const double drawableHeight = static_cast<double>(drawable.texture.height);
    double viewWidth = drawableWidth;
    double viewHeight = drawableHeight;
    if (m_impl->imageWidth > 0 && m_impl->imageHeight > 0) {
        const double imageAspect = static_cast<double>(m_impl->imageWidth) /
                                   static_cast<double>(m_impl->imageHeight);
        // 90°/270° 时图片的显示宽高比会翻转。
        const double effectiveAspect = ((m_impl->quarterTurns & 1) != 0)
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


    // Q: scissor 是什么？ 作用是什么？
    // A: 限制光栅化落到哪些像素；这里限制到 contain-fit 后的图片区域。
    const MTLScissorRect scissor = {
        static_cast<NSUInteger>(viewLeft), static_cast<NSUInteger>(viewTop),
        static_cast<NSUInteger>(viewWidth), static_cast<NSUInteger>(viewHeight)};
    [encoder setScissorRect:scissor];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

    [encoder endEncoding];
    // << 结束录制


    // 4. 录制命令
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    if (commandBuffer.status == MTLCommandBufferStatusError) {
        const std::string text = nsErrorText(commandBuffer.error);
        std::fprintf(stderr, "[gfx] Metal command buffer error: %s\n", text.c_str());
    }
}

const char *MetalRenderer::backendName() const
{
    return "Metal";
}

void MetalRenderer::setQuarterTurns(int quarterTurns)
{
    m_impl->quarterTurns = ((quarterTurns % 4) + 4) % 4;
}

void MetalRenderer::setZoom(float zoom)
{
    m_impl->zoom = (zoom > 0.0f) ? zoom : 1.0f;
}

// 上传纹理
void MetalRenderer::setImage(std::uint32_t width, std::uint32_t height,
                             const std::uint16_t *rgba16)
{
    if (!m_impl->initialized || rgba16 == nullptr || width == 0 || height == 0) {
        return;
    }

    // 1. 创建纹理
    // 创建 texture descriptor， 类似win中 resource 和 heap
    MTLTextureDescriptor *textureDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModePrivate;

    id<MTLTexture> texture = [m_impl->device newTextureWithDescriptor:textureDescriptor];
    if (texture == nil) {
        throw std::runtime_error("创建 Metal 纹理失败");
    }

    // 2. 计算上传布局
    // macOS 要求 buffer→texture 的 sourceBytesPerRow 按 256 字节对齐。
    // 和 win 相同
    const NSUInteger sourceRowBytes = static_cast<NSUInteger>(width) * 8;
    const NSUInteger bytesPerRow = ((sourceRowBytes + 255) / 256) * 256;
    
    // 3. 构造 staging 数据
    // Q: staging 是什么？和 win 的什么对应？
    // A: staging 是 CPU 侧的临时缓冲，用来把“紧排的源数据”整理成“GPU 要求的 256 对齐行宽”。
    //    staging 是辅助，stagingBuffer 是真正的上传缓存。
    std::vector<std::uint8_t> staging(static_cast<std::size_t>(bytesPerRow) * height);
    if (bytesPerRow == sourceRowBytes) {        // 2048 快速路径，直接整块 memcpy
        std::memcpy(staging.data(), rgba16,
                    static_cast<std::size_t>(sourceRowBytes) * height);
    }
    else {                                      // 宽度不是32倍数
        const auto *source = reinterpret_cast<const std::uint8_t *>(rgba16);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(staging.data() + static_cast<std::size_t>(row) * bytesPerRow,
                        source + static_cast<std::size_t>(row) * sourceRowBytes,
                        sourceRowBytes);
        }
    }

    
    // 4. 创建 staging buffer
    id<MTLBuffer> stagingBuffer =
        [m_impl->device newBufferWithBytes:staging.data()
                                    length:staging.size()
                                   options:MTLResourceStorageModeShared];
    if (stagingBuffer == nil) {
        throw std::runtime_error("创建 Metal 暂存缓冲失败");
    }

    // 5. 创建 command buffer 和 blit encoder， 录制命令
    // Q: 这里的command list/allocator 为什么叫blit？
    // A: 这里的 encoder 不是 对应 command 系列，而是对应 CopyTextureRegion
    //    blit 是历史术语，意思是 block image transfer：块拷贝/块填充。
    //    MTLBlitCommandEncoder 是专门录制“拷贝类命令”的 encoder。
    id<MTLCommandBuffer> commandBuffer = [m_impl->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];

    // buffer → texture 拷贝， 相当于录制拷贝命令
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
    [commandBuffer commit];

    // 6. wait for gpu
    [commandBuffer waitUntilCompleted];

    if (commandBuffer.status == MTLCommandBufferStatusError) {
        throw std::runtime_error("Metal 纹理上传失败: " + nsErrorText(commandBuffer.error));
    }

    // 7. 保存状态
    // 把“当前渲染器要显示哪张图”这个状态更新掉
    m_impl->texture = texture;          // 是 ARC 强引用，赋值后旧纹理会自动释放
    m_impl->imageWidth = width;
    m_impl->imageHeight = height;
    std::fprintf(stderr, "[gfx] texture uploaded = %ux%u (%lu B/row)\n", width, height,
                 static_cast<unsigned long>(bytesPerRow));
}

} // namespace chroma::render