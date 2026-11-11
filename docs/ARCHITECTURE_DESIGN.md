# Architecture Design

> Agents Read-Only

## 双平台适配

### Interface

IRenderer：renderer接口，不同平台/框架renderer 接入，实现renderer 功能

IViewTransform：view 变换接口，实现图片旋转缩放；该接口暂时保留，未来可能修改；

### Renderer Factory

RendererFactory： 创建renderer，获取renderer唯一入口，应用层不需要知道后端具体是谁

### D3D12

#### D3D12 Renderer

win 平台原生图形API

D3D12Renderer.cpp: renderer 接口实现
D3D12Renderer.hpp: include 对应的 context

#### D3D12 context

win 平台原生图形API 上下文

D3D12Context.cpp:
D3D12Context.hpp:

### Metal

#### Metal Renderer

Mac 平台原生图形API 渲染器

MetalRenderer.cpp: renderer 接口实现， 接口对齐D3D12Renderer，实现转发给mm文件
MetalRenderer.hpp

- 保持纯 C++;
- 只前置声明 `MetalContext`
- 完整 `MetalContext` 在 `MetalRendererImpl.hpp` include

MetalRendererImpl.hpp: 私有：C++ 状态 + ObjcState 前置声明

- Impl 必须放在私有头

``` C++
// MetalRendererImpl.hpp
struct MetalRenderer::Impl
{ 
    bool initializeObjc(void *nativeWindow, std::uint32_t width, std::uint32_t height);
    void presentObjc();
    void setImageObjc(std::uint32_t width, std::uint32_t height,
                    const std::uint16_t *rgba16);
    ~Impl();

};
```

MetalRenderer.mm: 具体函数实现，objective-c

``` Objective-C
struct MetalRenderer::Impl::ObjcState{ ... };
```

- MetalRenderer.cpp: 接口实现 + 转发到 Impl 的 Objc 入口
- MetalRenderer.mm: 实现 initializeObjc / presentObjc / setImageObjc / ~Impl

#### Metal context

Mac 平台原生图形API 上下文

MetalContext.hpp
MetalContext.mm

### Skia

- SkiaRenderer.cpp 平台无关
- SkiaGpuBackendD3D12.cpp / SkiaGpuBackendMetal.mm 由平台条件选择
- APPLE 的 Metal.mm 开 -fobjc-arc

#### Skia Renderer

用于跨平台的框架，不是真实硬件API 渲染器

管理结构体：struct SkiaRenderer::Impl

- 持有 SkiaGpuBackend
- 持有 GrDirectContext / SkSurface
- 持有 SkRuntimeEffect / SkImage
- 持有 width/height、交互状态

SkiaRenderer.cpp: 公共逻辑 + IRenderer；需要后端的函数转发给 SkiaGpuBackend
SkiaRenderer.hpp: 纯 C++ pimpl

SkiaGpuBackend.hpp: 平台接口

- initialize / resize
- makeGaneshContext / createSurface
- beginFrame / endFrame
- shaderPath / backendName
- createSkiaGpuBackend()

SkiaGpuBackendD3D12.cpp: include SkiaGpuBackend.hpp；D3D12Context + Ganesh + 离屏 + CopyResource
SkiaGpuBackendMetal.mm: include SkiaGpuBackend.hpp；MetalContext + Ganesh + 离屏 + blit

所有权：

- SkiaGpuBackend 持有原生 context 和离屏资源
- SkiaRenderer 持有 GrDirectContext / SkSurface
