# Architecture Design

> Agents Read-Only

> 人工维护，记录架构决策和边界；
> 细节见：`docs/ARCHITECTURE_IMPLEMENTATION.md`；
> 第2~6 章记录当前状态，第7章记录决策过程，

## 1. 文档规范

### 命名规范

1. 文件夹：小写。需要多个按此拼接使用小写+下划线；
2. 文件：
    2.1 特殊文件全大写：AGENTS、README、docs/目录下的部分文档；
    2.2 cpp/hpp/mm等程序文件：大驼峰（main.cpp除外）；
    2.3 py脚本：小写+下划线；
    2.4 其他：按平台规范（如git/clang-format等）

## 2. Renderer双平台适配

### 2.1 Interface

IRenderer：renderer接口，不同平台/框架renderer 接入，实现renderer 功能

IViewTransform：view 变换接口，实现图片旋转缩放；该接口暂时保留，未来可能修改；

### Renderer Factory

RendererFactory： 创建renderer，获取renderer唯一入口，应用层不需要知道后端具体是谁

### 2.2 D3D12

#### D3D12 Renderer

win 平台原生图形API

D3D12Renderer.cpp: renderer 接口实现
D3D12Renderer.hpp: include 对应的 context

#### D3D12 context

win 平台原生图形API 上下文

D3D12Context.cpp:
D3D12Context.hpp:

### 2.3 Metal

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

### 2.4 Skia

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

## 3. App 架构总览
``` bash
MainWindow
  ├── ViewportWidget(Metal)
  │     ├── CSMViewerWidget(Native/Metal)
  │     ├── CSMViewerWidget(Skia)
  │     └── AssistInfoWidget
  └── ViewportWidget(Skia)
        ├── CSMViewerWidget(Native/Metal)
        ├── CSMViewerWidget(Skia)
        └── AssistInfoWidget
```

### 3.1 双 ViewportWidget 规划方案 （记得提醒我写完）
- 为什么是两个 ViewportWidget
  - 两个 API的对比
- 为什么每个 ViewportWidget 内部保留两个 CSMViewerWidget
  - 不知道，AI自行决策的
- 哪些是当前实现，哪些是未来计划
  - 当前实现
    - MainWindow 持有两个 ViewportWidget ，每个 ViewportWidget 中保留两个 CSMViewerWidget 
  - 未来计划（暂定，需要讨论可行性）
    - ViewportWidget 等文件目录需要修改，放在子目录下；
    - MainWindow 持有 1 个 ViewportWidget；
    - ViewportWidget 持有 两个 CSMViewerWidget， transform状态；
    - 每个 CSMViewerWidget 持有 1个 AssistInfo 和 对应API tag 和 renderer；
    - ViewportWidget 处理交互逻辑，当前显示哪个 CSMViewerWidget，是否update、reset、resize等等；
    - CSMViewerWidget 只承担渲染显示职责。
- 当前 Renderer 总数量：2 个 ViewportWidget × 2 API = 4 个常驻 renderer
  - 框架需要优化

## 4. 应用层对象模型

各模块职责划分

### 4.1 MainWindow
职责：

创建两个 ViewportWidget
维护显示模式：
SkiaOnly
NativeOnly
Both
维护全局旋转/缩放状态
处理 Enter / Tab / 方向变换 / Esc
根据模式设置 viewport 几何和层级
更新 AssistInfo

### 4.2 ViewportWidget
职责：

持有一个用于显示的面板
内部持有两个常驻 CSMViewerWidget：
Native/Metal
Skia
当前 API 通过 raise/lower 切换
持有 AssistInfoWidget
负责把当前 API、变换状态同步给内部 viewer

### 4.3 CSMViewerWidget
职责：

拥有单个 renderer
管理 native surface
图像上传
view transform 应用
present
renderer 生命周期随程序

### 4.4 AssistInfoWidget
职责：

左上角半透明信息层
只显示当前 API
固定宽度
每次绘制先清空上一帧，避免长文本残留
单图模式只显示当前 viewport 的 info
建议新增这几类的接口表和所有权表。

### 4.5 Probe

程序目录：`src/probe`

功能说明见 [`features/probe_card_capture.md`](features/probe_card_capture.md)。

核心决策：

- Probe 采样 **Stage B**：`present` 后的 renderer offscreen surface。
- 它不是 DWM / WindowServer / Digital Color Meter 的 display buffer。
- 读取值 = 我们 shader 输出 + F16 存储 + GPU 拷贝 + CPU readback；
  不包含系统合成、显示色彩管理、显示器映射。
- 使用源图 `image` 坐标，经当前 view transform 映射到 renderer 物理像素。
- 输入：`src/probe/card_config/17_ramp_config.json`
- 输出：`src/probe/output/card_color.csv`
- 输出格式：`block, r, g, b`
- 当前实现：Skia 路径；Native Metal / D3D12 readback 暂不在范围内。
- 校验：[`src/probe/verify_probe.py`](../src/probe/verify_probe.py)

## 5. 交互设计
应当写明两部分：  
1. 用户如何体验；
2. 对应技术如何实现；

这部分先简写，记得之后有空要详述

- tab：不同API结果对比；技术上需要清屏重绘（update+renderRequest）
- enter：切换显示状态 skia/native/dual；技术上也需要清屏重绘（update+renderRequest）
- Q/E，A/D：zoom and roate（update+renderRequest）
- R：reset transform state （update+renderRequest）
- ESC：exit app

 ### 5.1 Starch
- 技术上是否应该新增onUpdate， onReset和对应系列函数？未来可能reset 不同模块信息，上层只调用onUpdate或onReset，函数内部包含每次update/reset需要进行的操作，如onReset里包含resetTransform等？

## 6. 测试工具

### 6.1 Color Picker
`_shirley/tools/colorpicker`

#### Win

1. 取色使用API：
2. 取色阶段：DWM 融合后 
3. 取色格式：F16（/F8，不同显示器支持情况不同）
4. 取色色域：scRGB，linear

#### Mac

1. 取色使用API：
2. 取色阶段：
3. 取色格式：xf44
4. 取色色域：kCGColorSpaceITUR_2100_PQ; Mac上API不公开获取色值色域，而是指定捕获格式，获取对应色域的信息;

## 7. 重要决策记录

###  7.1 重要决策总览表

| ID  | 决策 | 原因 | 替代方案 | 状态 |
| --- | --- | --- | --- | --- |
| 。 | 。 | 。 | 。 | 。 |

### 7.2 重要决策详述


