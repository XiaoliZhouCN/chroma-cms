# Feature · Probe Card Capture

> 状态：[Implemented] Stage B / Skia 路径
> 目标：获取 app 自身色彩管理结果，用于验证 shader 输出的精确度。

## 1. 定位

Probe 读取的是 **renderer present 阶段** 的色块值，不是屏幕最终值。

```text
源图
  → 解码
  → 我们的 shader
  → Skia offscreen surface
  → blit / CopyResource
  → drawable / swapchain
  → Present
  → DWM / WindowServer
  → 显示色彩管理 / EDR mapping
  → 显示器
```

Probe 位于：

```text
Present 边界
读取 present 后的 renderer offscreen surface
```

因此它反映：

```text
我们的 shader 输出
+F16 存储
+GPU blit / CopyResource
+GPU → CPU readback
```

不包含：

```text
DWM / WindowServer 合成
系统色彩管理
显示器 ICC / EDR tone mapping
Digital Color Meter 的 display buffer
```

> 结论：Probe 值不是“DWM 融合后的值”，而是“提交给合成器之前的源像素值”。

## 2. 范围

当前实现：

```text
- SkiaRenderer 路径
- 单张测试图
- 136 个色块（8 ramps × 17）
- 输出 raw linear r/g/b
```

暂不包含：

```text
- Native Metal / D3D12 readback
- 双图同时采样多个 API
- DWM / WindowServer 阶段采样
```

## 3. 输入

配置：

```text
src/probe/card_config/17_ramp_config.json
```

字段：

```json
{
  "image": "/assets/test-images/PTMR_8ramps_17_2048x2048_10bit.png",
  "loc_config": "./assets/test-images/PTMR_8ramps_17_2048x2048_10bit.json"
}
```

约定：

```text
image      测试图路径
loc_config 色块坐标 JSON 路径
```

两者按仓库根解析；`/assets`、`./assets` 都按仓库相对路径处理。

坐标 JSON 使用：

```json
{
  "name": "gray-00",
  "image": { "x": 60.0, "y": 128.0 },
  "screen": { "x": 616.5, "y": 71.5 }
}
```

Probe 使用 `image` 坐标，不使用 `screen` 坐标。

## 4. 坐标映射

Probe 读取 renderer surface，因此需要：

```text
源图坐标
  → 当前 contain-fit / rotation / zoom
  → renderer physical pixel
```

映射由：

```text
CSMViewerWidget::imagePointToSurface()
```

完成，使用与 Skia shader 相同的 view transform 数学。

采样点取色块中心的物理像素；DPI 使用 renderer physical size，不使用 Qt 逻辑坐标。

## 5. 采样流程

```text
CSMViewerWidget::paintEvent()
  → renderer->present()
  → emit framePresented()
  → ProbeController::onFramePresented()
       → imagePointToSurface()
       → renderer->readPixels()
       → 写 CSV
```

`readPixels` 在读取前同步 GPU：

```text
SkiaRenderer:
  gr->flushAndSubmit(GrSyncCpu::kYes)
  surface->readPixels(...)
```

只在 probe 待处理时执行一次，不是每帧采样。

## 6. 触发方式

默认不启用。

```bash
CHROMA_PROBE=1 \
  build/macos-arm64-skia/ChromaCMS.app/Contents/MacOS/ChromaCMS
```

可选：

```text
CHROMA_PROBE_CONFIG  覆盖 card config 路径
CHROMA_PROBE_OUTPUT  覆盖 CSV 输出路径
```

捕获发生在当前激活的 Skia viewer 第一次 present 后；成功后停止。

## 7. 输出

```text
src/probe/output/card_color.csv
```

格式：

```text
block,r,g,b
gray-00,0,0,0
gray-01,0.00126075745,...
```

定义：

```text
r/g/b = SkiaRenderer offscreen surface 的 raw linear F16 值
        已经过我们的 shader，未经过 DWM/WindowServer/显示器映射
```

不是：

```text
0..255
sRGB 编码值
屏幕最终值
```

## 8. 验证

### 8.1 完整性

```bash
wc -l src/probe/output/card_color.csv
```

应为：

```text
137 行 = 1 header + 136 patch
```

并检查：

```text
- 所有 patch 名称存在
- block 顺序与坐标 JSON 一致
- 无 NaN / 空值
```

### 8.2 正确性

```bash
python3 src/probe/verify_probe.py
```

脚本使用测试图源像素和 shader 的 CPU 等价公式生成 oracle：

```text
source RGB
→ PQ EOTF
→ Rec.2020 → XYZ
→ XYZ → P3
→ nits / 80
```

与 CSV 比较。

当前结果：

```text
136 / 136 patch
max_abs_error ≈ 0.07
```

误差量级与 F16 存储/量化一致。

## 9. 实现文件

```text
src/probe/ProbeConfig.*       配置和坐标解析
src/probe/ProbeCsvWriter.*    CSV 输出
src/probe/verify_probe.py     完整性与 oracle 校验
src/render/IRendererReadback.hpp
src/render/skia/SkiaRenderer.* readPixels
src/app/CSMViewerWidget.*     imagePointToSurface / framePresented
src/app/MainWindow.*          probe 初始化和单次采样
```

## 10. 相关文档

- [Features 索引](README.md)
- [架构设计](../ARCHITECTURE_DESIGN.md)
- [架构实现说明](../ARCHITECTURE_IMPLEMENTATION.md)
- [Probe 模块说明](../../src/probe/README.md)
