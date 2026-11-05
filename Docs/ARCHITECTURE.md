---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 架构

## 1. 唯一不变量

**颜色数学只有一个真值来源（[COLOR_SPEC.md](COLOR_SPEC.md)），但有两个执行后端（HLSL / SKSL）。**

所有设计服从三个目的：

1. 亲手掌控完整呈现链路，不借渲染框架代劳；
2. 亲手实现色彩管理，不借任何框架的色彩转换；
3. 用同一套数学，在两条后端上产出可验证的等价结果。

判据：任何结构提案，如果让"两条后端的一致性"更难验证，就不采纳。

## 2. 分层

```
┌──────────────────────────────────────────────────────────────┐
│ L5  应用外壳 (Qt Widgets)                                     │
│     MainWindow · ViewportWidget(原生表面宿主) · 状态栏(色彩元数据) │
├──────────────────────────────────────────────────────────────┤
│ L4  呈现控制                                                  │
│     PlaybackClock · FrameScheduler · PresentPolicy            │
├──────────────────────────────────────────────────────────────┤
│ L3  渲染后端 (IRenderer)                                      │
│     D3D12Renderer ─┐                                          │
│     SkiaD3D12Renderer ─┴─→ 共享同一个 ID3D12Device / Queue     │
├──────────────────────────────────────────────────────────────┤
│ L2  色彩管线（自研）                                          │
│     HLSL 实现 ←→ COLOR_SPEC ←→ SkSL 实现                      │
│     CPU 双精度参考实现（oracle，仅测试与生成常量表）            │
├──────────────────────────────────────────────────────────────┤
│ L1  帧源                                                      │
│     ImageLoader / VideoSource（均为 FFmpeg）→ FrameData        │
│     + ColorMetadata(primaries/transfer/matrix/range, H.273)    │
├──────────────────────────────────────────────────────────────┤
│ L0  平台                                                      │
│     DXGI 交换链 · D3D12 设备 · HWND · SKIA_ROOT               │
│     （macOS：CAMetalLayer · MTLDevice · NSView，待实现）       │
└──────────────────────────────────────────────────────────────┘
```

依赖方向**只允许向下**。L3 不得包含颜色数学，L2 不得触碰窗口/交换链。

## 3. 模块职责

| 目录 | 职责 | 对外契约 | 明确不做 |
|---|---|---|---|
| `src/app/` | Qt 外壳、事件循环、文件打开、状态栏 | 持有 `IRenderer`，提供 HWND | 不碰 D3D12 细节 |
| `src/render/` | 实现 `IRenderer`，管理两遍渲染与呈现 | `initialize/resize/present/shutdown` | 不做解码、不做颜色数学 |
| `src/gfx/` | D3D12 薄封装：Device、Queue、SwapChain、Fence、PSO、DescriptorHeap | RAII 类，无业务语义 | 不做场景管理 |
| `src/color/` | CPU 双精度参考实现、矩阵推导、常量化导出、ICC/显示配置读取 | 纯函数，可单测 | **不参与运行时呈现** |
| `src/media/` | FFmpeg 解码（图像 + 视频），产出 `ImageData` / `FrameData` + `ColorMetadata` | `loadImage` / `IVideoSource` | 不做颜色转换 |
| `shaders/` | HLSL 与 SkSL 两套实现 | 严格对齐 COLOR_SPEC | 不发明数学 |
| `tests/` | 单测、黄金图、双后端一致性 | ctest | 不测 UI |
| `scripts/` | 文档校验、常量生成、构建辅助 | 退出码即门禁 | — |

## 4. 关键决策：两条后端共享一个 D3D12 设备

Skia 的 D3D12 后端通过 `GrD3DBackendContext` 接受**已有的** `ID3D12Device` / `ID3D12CommandQueue` / 适配器 LUID，不需要独占设备。

因此：

- 切换后端**不重建设备**，只切换命令录制方式；
- 两条后端写同一张交换链的后台缓冲，比较是"同设备、同格式、同帧"的苹果对苹果；
- 双后端一致性测试可以在一次运行内完成。

细节与替代方案见 [ADR-0003](adr/0003-dual-render-backend.md)。

## 5. 一帧的数据流

```
文件
 └─(L1) 解封装/解码 → YUV 平面 或 RGB 像素
      └ ColorMetadata { primaries, transfer, matrix, range, bitdepth }
 └─(L2 Pass 1) 采样 + 范围归一化 + YUV→RGB + EOTF 解码
      + 色域矩阵（源 → 线性工作空间）+ 色调/色域映射
      → 线性工作空间 R16G16B16A16_FLOAT 渲染目标
 └─(L2 Pass 2) 目标色域矩阵（工作空间 → 显示）
      + 显示传输函数编码 + 抖动
      → 交换链后台缓冲（10-bit 或 8-bit，按 ADR-0007）
 └─(L3) 命令列表提交 → fence 等待 → Present(1)
```

**两遍渲染的理由**：把"线性工作空间"变成显式的物理中间产物，既能用 GPU 捕获逐级比对，也让 HDR 扩展只是在 Pass 2 前插入一步色调映射。

## 6. 核心接口（意向，随实现演进）

```cpp
// src/media/Frame.h
struct ColorMetadata {
    Primaries primaries;      // BT709 / P3_D65 / BT2020
    Transfer  transfer;       // SRGB / GAMMA22 / PQ / HLG
    Matrix    matrix;         // BT601 / BT709 / BT2020 (YUV)
    Range     range;          // FULL / LIMITED
    uint32_t  bitDepth;
};

struct FrameData {
    const uint8_t* planes[3];
    uint32_t strides[3];
    PixelFormat format;       // NV12 / P010 / RGBA8 / RGBA16F
    ColorMetadata color;
    int64_t ptsUs;
};

// src/render/IRenderer.h  （M1 实际形态）
class IRenderer {
public:
    virtual ~IRenderer() = default;
    // nativeWindow 是 HWND；width/height 为物理像素（已乘 devicePixelRatio）
    virtual bool initialize(void* nativeWindow, uint32_t width, uint32_t height) = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void setImage(const media::ImageData& image) = 0;  // M1：静态图
    virtual void present() = 0;
    virtual const char* backendName() const = 0;
};
```

约束：`IRenderer` 不暴露任何 D3D12 类型，Qt 层不得直接 `#include <d3d12.h>`。

**演进**：M4 引入视频后，`setImage` 会被 `submitFrame(const FrameData&)` 取代，`present()` 保持无参。
接口变更必须同步 ADR 与 [COLOR_SPEC.md](COLOR_SPEC.md) 的调用顺序约定。

## 7. 线程模型

| 线程 | 职责 |
|---|---|
| UI 线程（Qt） | 窗口消息、`present()` 调用、状态栏更新 |
| 解码线程 | `IVideoSource::nextFrame()` 阻塞取帧，双缓冲交接 |
| GPU | 命令队列 + fence，最多 2 帧在飞 |

规则：**跨线程只传 `FrameData` 的值拷贝或所有权转移**，不共享可变状态。

## 8. 明确不做

- 不做图像编辑与导出（只读显示）；
- 不做多窗口 / 多视口；
- 不做音频播放与音画同步；
- 不做通用 effect graph 或节点编辑器。

## 9. 平台矩阵

多平台从 [ADR-0009](adr/0009-multi-platform-support.md) 起成为需求（早期版本写的是"D3D12 硬约束，Windows only"，已作废）。

| 层 | Windows | macOS |
|---|---|---|
| 解码 | FFmpeg | FFmpeg（相同） |
| 原生渲染 | D3D12 | **Metal（待实现）** |
| Skia 渲染 | Ganesh D3D12（M3） | Ganesh/Graphite Metal（M3 之后） |
| 窗口宿主 | `QWidget` + HWND | `QWidget` + `NSView` / `CAMetalLayer` |
| 显示配置 | WCS / ICC（M5） | ColorSync / `NSColorSpace`（M5） |
| 一致性门禁 | 双后端像素比对 | 与黄金图比对（跨设备比对无意义） |

约束：**`IRenderer` 不得出现平台图形 API 类型**；平台实现由 CMake 按 `CMAKE_SYSTEM_NAME` 选择源文件。
