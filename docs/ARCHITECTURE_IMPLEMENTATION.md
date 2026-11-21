# Architecture Implementation

> AI 根据代码与人工架构决策维护；决策以 [`ARCHITECTURE_DESIGN.md`](ARCHITECTURE_DESIGN.md) 为准。

## 1. 当前应用组成

```text
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

- `MainWindow`：显示模式、全局 transform、Enter/Tab/Esc、Probe 触发。
- `ViewportWidget`：一个显示面板，持有两个常驻 CSMViewerWidget 和 info。
- `CSMViewerWidget`：单 renderer 生命周期、surface、present、image→surface 映射。
- `AssistInfoWidget`：只显示当前 API；单图模式隐藏非当前 viewport 的 info。

## 2. 当前 renderer 生命周期

```text
4 个常驻 renderer：
  2 个 ViewportWidget × 2 个 API
```

切换 API 通过 raise/lower 和 `showBackend()` 完成，不销毁 renderer。

## 3. 清屏与 Present

每次 renderer present 都先清纯黑：

- Metal：`MTLLoadActionClear` + black clearColor
- Skia：`canvas->clear(black)`；drawable 先 clear；CAMetalLayer opaque + black
- D3D12：`ClearRenderTargetView(black)`

不使用 QWidget 黑遮罩或固定延时等待帧。

## 4. Probe Stage B

Probe 读取 `present()` 后的 SkiaRenderer offscreen surface。

```text
CSMViewerWidget::paintEvent()
  → renderer->present()
  → emit framePresented()
  → MainWindow::onFramePresented()
  → CSMViewerWidget::readPixels()
  → ProbeCsvWriter
```

数据含义：

```text
我们的 shader 输出
+ F16 存储
+ GPU blit / CopyResource
+ GPU → CPU readback
```

不包含：

```text
DWM / WindowServer 合成
系统色彩管理
显示器 ICC / EDR mapping
Digital Color Meter 的 display buffer
```

## 5. Readback 接口

[`src/render/IRendererReadback.hpp`](../src/render/IRendererReadback.hpp)：

```cpp
struct PixelPoint { std::uint32_t x; std::uint32_t y; };
struct RgbaF32 { float r, g, b, a; };

class IRendererReadback {
public:
    virtual bool readPixels(const std::vector<PixelPoint>& points,
                            std::vector<RgbaF32>& out) = 0;
};
```

当前实现：`SkiaRenderer`。

`readPixels()` 读取前显式 flush：

```cpp
m_impl->gr->flushAndSubmit(GrSyncCpu::kYes);
m_impl->surface->readPixels(...);
```

## 6. 坐标映射

Probe 配置使用源图坐标：

```text
patches[].image
```

`CSMViewerWidget::imagePointToSurface()` 使用与 Skia shader 相同的
contain-fit / quarterTurns / zoom 数学，映射到 renderer 物理像素。

不使用 `patches[].screen`；screen 坐标属于窗口/显示器阶段。

## 7. Probe 输入输出

输入：

```text
src/probe/card_config/17_ramp_config.json
  image
  loc_config
```

输出：

```text
src/probe/output/card_color.csv
block,r,g,b
```

`r/g/b` 为 raw linear F16 值，不是 sRGB 0..255，也不是屏幕最终值。

## 8. 验证

```bash
CHROMA_PROBE=1 \
  build/macos-arm64-skia/ChromaCMS.app/Contents/MacOS/ChromaCMS

python3 src/probe/verify_probe.py
```

校验脚本使用源图和 shader CPU oracle：

```text
source RGB → PQ EOTF → Rec.2020 → XYZ → P3 → nits / 80
```

检查：

- 136 个 patch 完整；
- block 顺序一致；
- 最大绝对误差在 F16 量化范围内。

当前验证结果：

```text
patches: 136
max_abs_error: ≈ 0.07
status: PASS
```

## 9. 已知限制

- 当前仅 SkiaRenderer 实现 readback；
- Native Metal / D3D12 尚未实现 Probe readback；
- 读取 offscreen surface，不是 DWM / WindowServer 的 display buffer；
- 双图模式只采样当前激活 view；
- 4 个常驻 renderer 的 GPU 资源开销仍需优化。

## 10. 相关文档

- [架构设计](ARCHITECTURE_DESIGN.md)
- [Probe 功能说明](features/probe_card_capture.md)
- [Probe 模块说明](../src/probe/README.md)
- [Probe 校验脚本](../src/probe/verify_probe.py)
