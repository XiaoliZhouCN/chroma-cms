# 01 · 项目现状（Project State）

> 快照时间：2026-11-11 Skia 重构（SkiaGpuBackend + MetalRenderer 拆分 + CMake 分层）。
> 分支 `practice`。**最新进度与下一步**以
> [`README.md`](README.md) 和 [`10-skia-refactor-cmake.md`](10-skia-refactor-cmake.md) 为准；
> [`08-current-progress.md`](08-current-progress.md) 保留为 MetalContext 重构的历史记录。

## 1. Git

| 项 | 值 |
|---|---|
| 工作分支 | `practice` |
| HEAD | 本次 Skia 重构提交（以 `git log -1` 为准） |
| 远端 | `git@github-shirley:XiaoliZhouCN/chroma-cms.git`（SSH 别名 `github-shirley`） |
| 参考实现分支 | `develop` @ `86f1ea2`（**与 practice 无共同祖先**，只能按文件借用） |
| 参考实现 tag | `ref-impl-m1`（本地存在；若 mac 上没有，用 `git show develop:<path>`） |
| 空分支 | `main` |

> **不要 merge `develop`**。`git merge-base practice develop` 返回非 0。

## 2. 代码地图（practice 分支）

```
CMakeLists.txt                 顶层：C++20、Qt6、测试入口
CMakePresets.json              windows-x64-* / macos-arm64-* / macos-x64-*
cmake/
  QtBootstrap.cmake            Qt 定位 + chroma_set_warnings()
  FindFFmpeg.cmake             定位 external/ffmpeg，产出 FFmpeg::FFmpeg
  FindSkia.cmake               定位预编译 Skia；Windows/D3D12 + macOS/Metal
src/gfx/CMakeLists.txt         创建 chroma_gfx；gfx/d3d12、gfx/metal 追加 sources
src/render/CMakeLists.txt      创建 chroma_render；render/d3d12、metal、skia 追加/创建 target
docs/third-party/skia.md       Skia M155 预编译包清单与消费约束
shaders/
  fullscreen.hlsl              Windows：VS(全屏三角形) + PS(PQ → 709 → scRGB) + 矩阵 UV
  fullscreen.metal             macOS：VS + PS(PQ → Display-P3)，运行时由 Metal 编译
src/
  core/Version.hpp             版本号（INTERFACE 库 chroma_core）
  gfx/D3D12Common.hpp          ComPtr / ThrowIfFailed
  gfx/d3d12/D3D12Context.{hpp,cpp}  Windows 原生上下文：设备/队列/交换链/RTV/命令录制/fence
  media/ImageLoader.{hpp,cpp}  FFmpeg 解码 → RGBA16；路径统一 UTF-8（chroma_media）
  render/IRenderer.hpp         平台无关渲染器接口（未改签名）
  render/IViewTransform.hpp    可选交互接口：setQuarterTurns / setZoom
  render/RendererFactory.{hpp,cpp} 按平台创建 D3D12 / Metal
  render/d3d12/D3D12Renderer.{hpp,cpp}  D3D12 后端（只画：根签名/PSO/纹理/常量/Draw）
  render/metal/MetalRenderer.{hpp,cpp,mm} + MetalRendererImpl.hpp  Metal 手写后端（接口 .cpp + ObjC .mm）
  render/skia/SkiaRenderer.{hpp,cpp}    Skia 平台无关绘制（SkSL/交互/图像）
  render/skia/SkiaGpuBackend.hpp       平台后端接口
  render/skia/SkiaGpuBackendD3D12.cpp  Windows D3D12 后端
  render/skia/SkiaGpuBackendMetal.mm   macOS Metal 后端
  app/main.cpp                 加载图片 → MainWindow
  app/MainWindow.{hpp,cpp}     窗口 + 状态栏
  app/ViewportWidget.{hpp,cpp} 原生表面宿主 + A/D/Q/E/R 交互
src/gfx/ColorMath.hpp          CPU 色彩参考（M5）
tests/unit/smoke_test.cpp      冒烟测试
tests/unit/color_math_test.cpp ColorMath 单元测试（ctest 共 2 项）
tools/
  gen_ptmr.py                  PTMR 测试图生成器（当前在仓库里）
  fetch-ffmpeg.ps1             Windows FFmpeg 下载脚本
```

## 3. 已实现的能力（Windows / D3D12）

| 步骤 | 内容 | 关键完成点 |
|---|---|---|
| 1 | Qt + CMake + 原生表面 | `WA_NativeWindow`，`winId()` 当 HWND，物理像素 = 逻辑 × dpr |
| 2 | 设备/队列/交换链/清屏 | `R16G16B16A16_FLOAT` + `RGB_FULL_G10_NONE_P709`（scRGB），flip-discard，2 帧 |
| 3 | 根签名 + PSO + 三角形 | 空→带 SRV 表；VS 用 `SV_VertexID` 现算全屏三角形；`DrawInstanced(3,1,0,0)` |
| 4 | 纹理上传 + SRV | 默认堆纹理 + UPLOAD 缓冲 + `GetCopyableFootprints` 行对齐 + `CopyTextureRegion` + 屏障 + SRV |
| 5 | FFmpeg 解码 | `avformat/avcodec/swscale` → `AV_PIX_FMT_RGBA64LE`；识别并还原 `value16 = code10 << 6` |
| 6.1 | 色彩管理（单 pass） | PQ EOTF → BT.2020→XYZ→BT.709 线性 → `/80` 得 scRGB 单位；**不做 OETF** |
| 6.2 | 根常量传参 | 根参数 1 = 8×DWORD 根常量（b0），`outputScale` 已实测生效（1.0 → 窗口均亮度 94.7，0.0 → 31.2） |
| 7 | 交互（Windows） | `IViewTransform` + 8×float（outputScale/aspect/m00..m11）+ 2×2 矩阵 UV + `discard` + 90° 交换宽高比；A/D/Q/E/R（`42518f8`） |

### 已实现的能力（macOS / Metal）

| 步骤 | 内容 | 关键完成点 |
|---|---|---|
| M1 | 设备 + CAMetalLayer + 清屏 | 子 NSView 承载 CAMetalLayer（不用 `host.layer`，Qt 会替换） |
| M2 | 全屏三角形 | 运行时 `newLibraryWithSource` + PSO；`fastMathEnabled=NO`/`MTLMathModeSafe` |
| M3 | 纹理上传 + 采样 | `RGBA16Unorm` + 256 字节行对齐 + blit + linear/clamp sampler |
| M4 | 色彩管线 | PQ EOTF → BT.2020→XYZ → **XYZ→Display-P3** → `/80`；`outputScale=1.0` |
| 交互 | 90° 旋转 / 缩放 | `IViewTransform` + 8×float root constants + 2×2 矩阵 UV；A/D/Q/E/R |

**macOS 当前显示配置：**

- `CAMetalLayer.colorspace = kCGColorSpaceExtendedLinearDisplayP3`；
- `wantsExtendedDynamicRangeContent = YES`；
- **暂不设置 `CAEDRMetadata`**；
- 因此 10,000 nits 内容在 8bpc/有限 headroom 屏上会逐通道裁剪（高光泛白/偏色），这是已知情况。

### 色彩数学（共享部分与平台差异）

```
code（归一化 PQ 码值）
  → PQ EOTF（ST.2084）→ 相对亮度 L（1.0 = 10000 nits）
  → BT.2020 线性 RGB → CIE XYZ（D65）
  → 平台最后矩阵：
       Windows: XYZ → BT.709 线性
       macOS:   XYZ → Display-P3 线性
  → max(·, 0) 裁负值
  → × 10000 得 nits
  → / 80
  → × outputScale：
       Windows: 0.8
       macOS:   1.0
输出（不做 OETF，因为两边 drawable 都是线性扩展色域）
```

**共享部分**：PQ EOTF、BT.2020→XYZ。  
**平台差异**：最后一步矩阵和 `outputScale`；两端**不要求逐像素一致**。

常量（HLSL 与 MSL 必须一致）：

| 名称 | 值 |
|---|---|
| PQ m1 | 0.1593017578125（2610/16384） |
| PQ m2 | 78.84375（2523/4096×128） |
| PQ c1 | 0.8359375（3424/4096） |
| PQ c2 | 18.8515625（2413/4096×32） |
| PQ c3 | 18.6875（2392/4096×32） |
| 峰值亮度 | 10000 nits |
| scRGB 参考 | 80 nits |
| BT.2020→XYZ 行 | `0.636958 0.144617 0.168881` / `0.262700 0.677998 0.059302` / `0.000000 0.028073 1.060985` |
| XYZ→BT.709 行（Windows） | `3.240970 -1.537383 -0.498611` / `-0.969244 1.875968 0.041555` / `0.055630 -0.203977 1.056972` |
| XYZ→Display-P3 行（macOS） | `2.4934969119 -0.9313836179 -0.4027107845` / `-0.8294889696 1.7626640603 0.0236246858` / `0.0358458302 -0.0761723893 0.9568845240` |

## 4. 同步模型（当前是"全同步"，故意简单）

> 重构后归 `gfx::D3D12Context` 管（`src/gfx/d3d12/D3D12Context.cpp`）。

- 单个 `ID3D12Fence` + 单个 `m_fenceValue`；
- `waitForGpu()` = `Signal` → `SetEventOnCompletion` → `WaitForSingleObjectEx(INFINITE)`；
- 调用点：`D3D12Context` 析构、`submitAndWait()`（纹理上传后）、`beginFrame()` 开头、`resize()` 开头；
- **没有** 双帧在飞（每帧一个 allocator + 每帧 fence 值）——这是已知的优化项。

## 5. Windows 环境（已实测）

| 项 | 值 |
|---|---|
| 编译器 | VS 2026 18.8（MSVC 19.51） |
| Windows SDK | 10.0.26100 / 28000 |
| CMake | 4.2.1，生成器 `Visual Studio 18 2026` |
| Qt | 6.11.1 `msvc2022_64` @ `D:\Applications\QtCreator\6.11.1\msvc2022_64` |
| GPU | Intel UHD Graphics，D3D12 特性等级 12_1 |
| FFmpeg | 预编译包放在 `external/ffmpeg`（116 MB，gitignore），DLL 由 POST_BUILD 拷贝 |
| 显示器 | 内屏 BOE（HDR 支持但未启用，8bpc）；外接 ASUS **VG32AQL1A**，HDMI 2560×1440@60，**HDR 已启用**，8bpc |

## 6. 资产与工具

| 路径 | 说明 | 是否入库 |
|---|---|---|
| `assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png` | 主测试图（16-bit PNG，cICP `[9,16,0,1]`） | ❌ `assets/` 被 ignore |
| `assets/test-images/*_preview8.png` | 8-bit 预览，用来对照观感 | ❌ |
| `assets/test-images/*.csv` | 每个色块的 code10 与 nits，**定量验收用** | ❌ |
| `external/ffmpeg/` | FFmpeg 头文件/导入库/DLL | ❌ |
| `temp_and_scratch/` | agent 验证用的临时目录，放截图 | ❌ |

生成测试图（任意平台，需 Python）：

```bash
python tools/gen_ptmr.py --out assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png --cicp-matrix 0 --preview
```

## 7. 已知缺口 / 下一步

| 项 | 说明 |
|---|---|
| Windows 交互 | ✅ 已完成（`42518f8`）；重构后行为不变，见 `08` §2 |
| macOS MetalContext | ✅ 已完成（`822b976`），`MetalRenderer` 已接 `gfx/metal/MetalContext` |
| Skia 第三后端 | ✅ mac Metal 基础适配完成；Windows `SkiaGpuBackendD3D12.cpp` 待实机编译/运行 |
| M5 一致性 | CPU ColorMath 基础测试已过；CSV nits 断言待补 |
| CMake 分层 | ✅ 每个构建目录一个 CMakeLists；规范见 `10` §3 |
| macOS EDR | `CAEDRMetadata` / tone mapping 未做；`outputScale=1.0` 高光逐通道裁剪 |
| 色调映射 | 两平台都还没有自己的 BT.2390 |
| 一致性测试 | 没有 CPU 参考实现与单元测试；目前靠离屏回读/截图人工对照 |
| 性能 | 全同步；Windows 单 allocator，Metal 每帧 `waitUntilCompleted` |
| 非方图旋转 | contain-fit 已按 90° 交换宽高比，但 PTMR 是方图，尚未用非方图实测 |
| 重构后小问题 | `D3D12Renderer::present()` 无图时也会 Draw、注释/命名空间小错等 5 条；见 `08` §3.7 |

## 8. 验证证据

| 阶段 | 截图 |
|---|---|
| 纹理采样 | `images/expected-step4-texture.png` |
| 色彩管线 | `images/expected-step6-scrgb.jpg` |
| 根常量 | `images/expected-rootconst.png` |
| macOS M4 Display-P3 | `images/macos-m4-p3.png`（高光裁剪是已知情况） |
| macOS 90° 交互 | `images/macos-90.png` |
| macOS 180° + 缩放 | `images/macos-180-zoom.png` |
| macOS R 复位 | `images/macos-reset.png` |
| 重构后 Windows 复核 | 构建/ctest/GUI/交互 diff 见 `08` §4（截图在 `temp_and_scratch/verify/`，未入库） |
