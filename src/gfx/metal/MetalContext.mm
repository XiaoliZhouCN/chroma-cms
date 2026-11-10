#include "gfx/metal/MetalContext.hpp"


#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace chroma::gfx
{
namespace
{
std::string nsErrorText(NSError *error)
{
    if (error == nil) {
        return "未知错误";
    }
    return std::string([[error localizedDescription] UTF8String]);
}
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
struct MetalContext::Impl
{
    id<MTLDevice>       device    = nil;
    id<MTLCommandQueue> queue     = nil;
    CAMetalLayer       *layer     = nil;
    NSView             *host      = nil;
    NSView             *metalView = nil;

    id<CAMetalDrawable>  drawable      = nil;
    id<MTLCommandBuffer> commandBuffer = nil;

    std::uint32_t width  = 0;
    std::uint32_t height = 0;

    bool initialized            = false;
    bool frameActive            = false;
    bool commandBufferCommitted = false;
};

MetalContext::MetalContext(): m_impl(std::make_unique<Impl>()) {}

MetalContext::~MetalContext()
{
    // 如果还有已提交但没等完的 command buffer，先等它结束再拆图层。
    if (m_impl->commandBuffer != nil && m_impl->commandBufferCommitted) {
        [m_impl->commandBuffer waitUntilCompleted];
    }
    if (m_impl->metalView != nil) {
        [m_impl->metalView removeFromSuperview];
    }
}

// Q: impl 是什么
// A: Impl implementation，pimpl（pointer to implementation), MetalRenderer 的嵌套结构体，定义在 .mm 里;
void MetalContext::initialize(void *nativeView, std::uint32_t width, std::uint32_t height)
{
    if (m_impl->initialized) { return; }

    if (nativeView == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("MetalContext::initialize 参数无效");
    }

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
        throw std::runtime_error("MTLCreateSystemDefaultDevice 返回 nil");
    }

    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (queue == nil) {
        throw std::runtime_error("newCommandQueue 返回 nil");
    }

    // Q: 句柄？相当于winId？
    // A: Yes. 平台原生窗口句柄,Windows 是 HWND，macOS 是 NSView*;
    NSView *host = (__bridge NSView *)nativeView;
    NSView *metalView = [[NSView alloc] initWithFrame:host.bounds];
    metalView.wantsLayer = YES;
    metalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    // CAMetalLayer 管理资源
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatRGBA16Float;      // 像素格式 16 float
    layer.framebufferOnly = YES;
    layer.maximumDrawableCount = 2;
    layer.displaySyncEnabled = YES;
    layer.contentsScale = host.window != nil ? host.window.backingScaleFactor : 1.0;
    layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));

    // set CG Color Space， 
    // win 中 在 createSwapchain中设置， Metal中没有 swapchain概念，由CAMetalLayer统一管理
    // 明确告诉系统：缓冲区是线性值，且允许超过 1.0 的 EDR 内容。
    // 先不设置 CAEDRMetadata；M4.2 再处理 EDR tone mapping。
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
    if (colorSpace == nullptr) {
        throw std::runtime_error("CGColorSpaceCreateWithName(ExtendedLinearDisplayP3) 返回 nil");
    }
    layer.colorspace = colorSpace;
    CGColorSpaceRelease(colorSpace);
    layer.wantsExtendedDynamicRangeContent = YES;       // EDR YES

    // Q: 为什么在这里赋值？
    // A: wantsLayer = YES 之后，AppKit 会给这个 view 准备一个 给它准备一个 CALayer, CALayer，这个 layer 就叫这个 view 的 backing layer。
    //    然后把它的 layer 替换成自己的 CAMetalLayer。
    //    在 addSubview 之前挂好，子 view 一进入视图树就已经有正确的 layer，不会先闪一下空背景。
    // Q: 什么是 backing layer？
    // A: 支撑一个 NSView 显示内容的那个 CALayer。
    // Q: layer 和 view 是什么关系？在win中的对应的术语叫什么？
    /*
    host：Qt 的原生 view，相当于 Windows 的 HWND；
    metalView：我们自己加的子 view，专门用来承载 Metal layer；
    CAMetalLayer：metalView 的 backing layer；
    metalView 本身不画东西，它只是 layer 的宿主；
    真正上屏的是 CAMetalLayer 里的 drawable。
    */
    metalView.layer = layer;
    [host addSubview:metalView];

    m_impl->device = device;
    m_impl->queue = queue;
    m_impl->host = host;
    m_impl->metalView = metalView;
    m_impl->layer = layer;
    m_impl->width = width;
    m_impl->height = height;
    m_impl->initialized = true;

    std::fprintf(stderr, "[gfx] MetalContext device = %s\n", [[device name] UTF8String]);
}

void MetalContext::resize(std::uint32_t width, std::uint32_t height)
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
    m_impl->metalView.frame = m_impl->host.bounds;      // bounds 是什么？
    m_impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width),
                                            static_cast<CGFloat>(height));

    // Q: 这一步在做什么？和 win 中什么对应？
    // A: 这个 layer 的 1 个 point 对应多少个像素，仍然在做窗口映射。
    //    单独的 contentsScale 跟  win 没有对应。
    if (m_impl->host.window != nil) {
        m_impl->layer.contentsScale = m_impl->host.window.backingScaleFactor;
    }
}

void MetalContext::waitForGpu()
{
    if (m_impl->commandBuffer != nil && m_impl->commandBufferCommitted) {
        [m_impl->commandBuffer waitUntilCompleted];
        m_impl->commandBuffer = nil;
        m_impl->commandBufferCommitted = false;
    }
}

// Q: 下面四个函数有什么对应关系？endFrame也执行了，和submitAndWait有什么区别？
// A：begin/endFrame：帧路径，上屏
//    begineCommandList / submitAndWait：一次性上传路径 
bool MetalContext::beginFrame()
{
    if (!m_impl->initialized || m_impl->layer == nil) { return false; }
    if (m_impl->commandBuffer != nil) {
        throw std::runtime_error("MetalContext::beginFrame: 上一个 command buffer 还没结束");
    }

    id<CAMetalDrawable> drawable = [m_impl->layer nextDrawable];
    if (drawable == nil) { return false; }

    id<MTLCommandBuffer> commandBuffer = [m_impl->queue commandBuffer];
    if (commandBuffer == nil) {
        throw std::runtime_error("MetalContext::beginFrame: new command buffer 返回 nil");
    }

    m_impl->drawable = drawable;
    m_impl->commandBuffer = commandBuffer;
    m_impl->frameActive = true;
    m_impl->commandBufferCommitted = false;
    return true;
}

void MetalContext::endFrame()
{
    if (!m_impl->frameActive || m_impl->drawable == nil || m_impl->commandBuffer == nil) {
        return;
    }

    id<MTLCommandBuffer> commandBuffer = m_impl->commandBuffer;
    [commandBuffer presentDrawable:m_impl->drawable];
    [commandBuffer commit];
    m_impl->commandBufferCommitted = true;
    [commandBuffer waitUntilCompleted];             // metal wait

    if (commandBuffer.status == MTLCommandBufferStatusError) {
        const std::string text = nsErrorText(commandBuffer.error);
        std::fprintf(stderr, "[gfx] MetalContext present error: %s\n", text.c_str());
    }

    m_impl->drawable = nil;
    m_impl->commandBuffer = nil;
    m_impl->frameActive = false;
    m_impl->commandBufferCommitted = false;
}

void MetalContext::beginCommandList()
{
    if (!m_impl->initialized) {
        throw std::runtime_error("MetalContext::beginCommandList: 未初始化");
    }
    if (m_impl->commandBuffer != nil) {
        throw std::runtime_error("MetalContext::beginCommandList: command buffer 正在使用");
    }

    id<MTLCommandBuffer> commandBuffer = [m_impl->queue commandBuffer];
    if (commandBuffer == nil) {
        throw std::runtime_error("MetalContext::beginCommandList: new command buffer 返回 nil");
    }

    m_impl->commandBuffer = commandBuffer;
    m_impl->frameActive = false;
    m_impl->commandBufferCommitted = false;
}

void MetalContext::submitAndWait()
{
    if (m_impl->commandBuffer == nil) { return; }

    id<MTLCommandBuffer> commandBuffer = m_impl->commandBuffer;
    [commandBuffer commit];
    m_impl->commandBufferCommitted = true;
    [commandBuffer waitUntilCompleted];

    const bool failed = commandBuffer.status == MTLCommandBufferStatusError;
    const std::string text = failed ? nsErrorText(commandBuffer.error) : std::string();

    m_impl->commandBuffer = nil;
    m_impl->commandBufferCommitted = false;

    if (failed) {
        throw std::runtime_error("MetalContext command buffer error: " + text);
    }
}

/* ==================== 访问器 ==================== */
void *MetalContext::currentDrawable() const     { return (__bridge void *)m_impl->drawable; }
void *MetalContext::commandBuffer() const       { return (__bridge void *)m_impl->commandBuffer; }
void *MetalContext::device() const              { return (__bridge void *)m_impl->device; }
void *MetalContext::queue() const               { return (__bridge void *)m_impl->queue; }
void *MetalContext::layer() const               { return (__bridge void *)m_impl->layer; }
std::uint32_t MetalContext::colorFormat() const { return static_cast<std::uint32_t>(MTLPixelFormatRGBA16Float); }
std::uint32_t MetalContext::width() const       { return m_impl->width; }
std::uint32_t MetalContext::height() const      { return m_impl->height; }

} // namespace chroma::gfx
