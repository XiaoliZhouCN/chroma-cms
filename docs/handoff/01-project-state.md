# 01 · 项目现状（Project State）

> 快照时间：Windows 侧完成第 6.1 步（色彩管线单 pass）之后。
> 分支 `practice` @ `da89fd6`，与 `origin/practice` 同步。

## 1. Git

| 项 | 值 |
|---|---|
| 工作分支 | `practice` |
| HEAD | `da89fd6 [color] update color management` |
| 远端 | `git@github-shirley:XiaoliZhouCN/chroma-cms.git`（SSH 别名 `github-shirley`） |
| 参考实现分支 | `develop` @ `86f1ea2`（**与 practice 无共同祖先**，只能按文件借用） |
| 参考实现 tag | `ref-impl-m1`（本地存在；若 mac 上没有，用 `git show develop:<path>`） |
| 空分支 | `main` |

> **不要 merge `develop`**。`git merge-base practice develop` 返回非 0。

## 2. 代码地图（practice 分支）

```
CMakeLists.txt                 顶层：C++20、Qt6(Core/Gui/Widgets)、include 两个 cmake 模块
CMakePresets.json              windows-x64-debug / release（本次交接补了 macos-*）
cmake/
  QtBootstrap.cmake            实为 chroma_set_warnings()：MSVC /W4 /utf-8，GCC/Clang -Wall
  CompilerWarnings.cmake       （历史遗留拆分）
  FindFFmpeg.cmake             定位 external/ffmpeg，产出 FFmpeg::FFmpeg
shaders/
  fullscreen.hlsl              VS(全屏三角形) + PS(PQ → 线性 → scRGB)，运行时由 D3DCompile 编译
src/
  core/Version.hpp             版本号（INTERFACE 库 chroma_core）
  gfx/D3D12Common.hpp          ComPtr / ThrowIfFailed（INTERFACE 库 chroma_gfx）
  media/ImageLoader.{hpp,cpp}  FFmpeg 解码 → RGBA16（chroma_media）
  render/IRenderer.hpp         平台无关渲染器接口
  render/D3D12Renderer.{hpp,cpp} 手写 D3D12 后端（chroma_render）
  app/main.cpp                 加载图片 → MainWindow
  app/MainWindow.{hpp,cpp}     窗口 + 状态栏
  app/ViewportWidget.{hpp,cpp} 原生表面宿主：show/resize/paint → renderer
tests/unit/smoke_test.cpp      冒烟测试（ctest 2 项）
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
| 6.2 | 根常量传参 | 根参数 1 = 4×DWORD 根常量（b0），`outputScale` 已实测生效（1.0 → 窗口均亮度 94.7，0.0 → 31.2） |

### 色彩数学（双平台必须完全一致）

```
code（归一化 PQ 码值）
  → PQ EOTF（ST.2084）→ 相对亮度 L（1.0 = 10000 nits）
  → BT.2020 线性 RGB → CIE XYZ（D65）
  → 线性 BT.709 RGB
  → max(·, 0) 裁负值
  → × 10000 得 nits
  → / 80 得 scRGB 单位（1.0 = 80 nits）
输出（不做 OETF，因为交换链是线性 scRGB）
```

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
| XYZ→BT.709 行 | `3.240970 -1.537383 -0.498611` / `-0.969244 1.875968 0.041555` / `0.055630 -0.203977 1.056972` |

## 4. 同步模型（当前是"全同步"，故意简单）

- 单个 `ID3D12Fence` + 单个 `m_fenceValue`；
- `waitForGpu()` = `Signal` → `SetEventOnCompletion` → `WaitForSingleObjectEx(INFINITE)`；
- 调用点：析构、纹理上传后、`present()` 开头、`resize()` 开头；
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
| Metal 后端 | 完全未开始 → 见 `03-metal-plan.md` |
| 渲染器工厂 | `ViewportWidget` 里还硬编码 `D3D12Renderer` → 见 `04-dual-platform.md` |
| 色调映射 | 目前交给 Windows DWM 兜底；自己的 BT.2390 还没写 |
| HDR 输出 | 外接屏 HDR 已启用，但还没做跨屏/EDR 自适应 |
| 一致性测试 | 没有 CPU 参考实现与单元测试，双平台数学靠人工对照 |
| 性能 | 全同步、单 allocator，CPU/GPU 串行 |

## 8. 验证证据

| 阶段 | 截图 |
|---|---|
| 纹理采样 | `images/expected-step4-texture.png` |
| 色彩管线 | `images/expected-step6-scrgb.jpg` |
| 根常量 | `images/expected-rootconst.png` |
