# 09 · Skia 后端的 Windows 实现与 macOS 适配方案（2026-11-10）

> 读者：**macOS 侧 agent**（以及用户本人）。
> 目标：把 Windows 上已经跑通的 Skia（Ganesh）第三后端，按"统一桥接"结构镜像到 macOS/Metal。
> 前置阅读：`AGENTS.md`、`README.md`、`01`、`02`，以及 `08` §3（`gfx` 上下文层）。
> 基线：`practice` @ **`5ebdfa7`**（Skia 后端已提交）。

---

## 0. 一句话

Windows 侧的 `SkiaRenderer` 已经用 **Ganesh D3D12** 跑通：Skia 画到自己的 RGBA16F 离屏纹理，再 `CopyResource` 到交换链；SkSL 外置为 `shaders/fullscreen.sksl`（完整 PQ 色彩链 + 交互），与手写 D3D12 后端**逐像素一致**。

**mac 侧要做的不是重写渲染逻辑**，而是把 Windows 代码里 3 块 D3D12 专属逻辑抽成 `SkiaNativeBridge`，再写一个 Metal 实现；SkSL、色彩链、交互矩阵全部复用。

---

## 1. 已提交状态

| 项 | 值 |
|---|---|
| HEAD | `5ebdfa7 chore: add clang-format config, legacy snapshots and notes` |
| Skia 后端提交 | `ff89ba9 feat(skia): add Skia (Ganesh D3D12) third backend` |
| 其他 | `23c82e3 fix(d3d12): restore no-image guard in present()` |
| 分支 | `practice`（`origin/practice` 还停在 `822b976`，本地领先 3 个 commit，**未 push**） |
| 工作区 | 干净 |

### 1.1 目录与职责

```
CMakeLists.txt                     option(CHROMA_WITH_SKIA OFF) + CHROMA_DEFAULT_BACKEND 缓存变量
CMakePresets.json                  windows-x64-skia（Release-only，CHROMA_WITH_SKIA=ON，默认后端 skia）
cmake/FindSkia.cmake               定位 external/skia → Skia::Skia（目前只有 Windows 分支）
shaders/fullscreen.sksl            SkSL：图像采样 + 完整色彩链（运行时读取，随 exe 发布）
src/render/skia/
  SkiaRenderer.hpp                 pimpl，纯 C++（IViewTransform 已实现）
  SkiaRenderer.cpp                 Ganesh D3D12 + 离屏纹理 + 帧流程 + 交互矩阵（382 行）
src/render/RendererFactory.cpp     CHROMA_BACKEND 环境变量 / CHROMA_DEFAULT_BACKEND 编译期默认
src/gfx/d3d12/D3D12Context.hpp/.cpp  新增 currentBackBuffer()（给 Skia 复制用）
```

### 1.2 构建与运行（Windows，现状）

```powershell
cmake --preset windows-x64-skia -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build --preset windows-x64-skia
.\build\windows-x64-skia\Release\ChromaCMS.exe            # 默认 skia（预设注入 CHROMA_DEFAULT_BACKEND=skia）
$env:CHROMA_BACKEND="d3d12"; .\build\windows-x64-skia\Release\ChromaCMS.exe   # 同一 exe 切回手写后端
```

- **只有 Release 可用**：Skia 预编译包是 `/MD`，Debug 会 `LNK2038: _ITERATOR_DEBUG_LEVEL`。
- 断言日志：`[skia] backend = Skia (Ganesh D3D12), milestone = 155`、`[gfx] backend = Skia (Ganesh D3D12)`。

---

## 2. Windows 实现（mac 要复用的部分）

### 2.1 数据流

```
initialize(hwnd, w, h)
  ├─ gfx::D3D12Context（device / queue / swapchain / RTV / fence）
  ├─ Ganesh: GrDirectContexts::MakeD3D(adapter, device, queue)
  ├─ 离屏纹理: ID3D12Resource RGBA16F（DEFAULT 堆，ALLOW_RENDER_TARGET，初始 RENDER_TARGET）
  │    └─ GrBackendRenderTargets::MakeD3D(w, h, GrD3DTextureResourceInfo)
  │    └─ SkSurfaces::WrapBackendRenderTarget(..., kRGBA_F16_SkColorType, nullptr)
  └─ SkSL: <exe目录>/shaders/fullscreen.sksl → SkRuntimeEffect::MakeForShader

present() 每帧
  ├─ context->beginFrame()                       # 录制 PRESENT → RENDER_TARGET（未提交）
  ├─ drawFrame()                                 # SkCanvas：clear + drawRect(shader + localMatrix)
  ├─ gr->flushAndSubmit()                        # Skia 提交自己的 command buffer 到同一队列
  ├─ 同一条 command list：RT→COPY_SOURCE / RT→COPY_DEST
  │    CopyResource(backBuffer, offscreen)        # 整块拷贝
  │    COPY_SOURCE→RT / COPY_DEST→RT（还原）
  ├─ GrBackendRenderTargets::SetD3DResourceState(&backendRT, RENDER_TARGET)   # 通知 Skia
  └─ context->endFrame()                         # RT→PRESENT + Close/Execute + Present
```

关键点：**Skia 不直接碰交换链**，只画离屏纹理；状态的"入口/出口"由我们的 context 命令列表负责，所以不需要和 Skia 抢后台缓冲的状态。

### 2.2 SkSL 与 uniform 约定（mac 完全复用）

`shaders/fullscreen.sksl` 只有 3 个 uniform，共 **48 字节**：

```sksl
uniform shader image;         // 子 shader（图像）
uniform float2 imageSize;     // 8 B：源图尺寸(px)
uniform float  outputScale;   // 4 B：Windows 0.8 / mac 1.0
uniform float3x3 xyzToTarget; // 36 B：XYZ -> 目标线性 RGB（列主序）
```

C++ 结构体必须严格对应（**不能加 padding**）：

```cpp
struct ImageUniforms
{
    float imageSize[2];
    float outputScale;
    float xyzToTarget[9];
};
```

**SkSL 的 `float3x3` 是 9 个 float 紧凑排列（36 B），不是 std140 的 48 B。**
验证方法：`effect->uniformSize()` 必须等于 `sizeof(ImageUniforms)`（Windows 实测 48 / 48）。

色彩链（与 `fullscreen.hlsl` / `fullscreen.metal` 一致）：

```
code(PQ) → PqEotf → linear2020 → kRec2020ToXYZ → xyz → xyzToTarget * xyz
         → max(·,0) → ×10000/80 × outputScale → scRGB 单位
```

平台差异只在 `xyzToTarget` 和 `outputScale`（C++ 侧 `#if defined(_WIN32) / #elif defined(__APPLE__)`）：

| 平台 | xyzToTarget | outputScale |
|---|---|---|
| Windows | XYZ→BT.709（行矩阵） | 0.8 |
| macOS | XYZ→Display-P3（行矩阵，已在 `SkiaRenderer.cpp` 写好分支） | 1.0 |

矩阵按**行**给、填 uniform 时转**列**（`xyzToTarget[col*3 + row]`），因为 SkSL 矩阵是列主序。

另：Skia 侧不设置 `SkColorSpace`（`WrapBackendRenderTarget` 传 `nullptr`），色彩管理完全由 SkSL 负责；mac 上也不要开 `CAEDRMetadata`（与手写 Metal 后端现状一致，后续任务）。

### 2.3 几何：contain-fit + 旋转/缩放走 `makeShader` 的 localMatrix

CPU 侧做两件事，SkSL 里不再逐像素算 UV：

1. **contain-fit 视口**：旋转时宽高比翻转
   `effectiveAspect = (q & 1) ? 1/imageAspect : imageAspect`，再算 `viewLeft/viewTop/viewWidth/viewHeight`。
2. **仿射矩阵**：设备像素 → 图片像素（含旋转 `[c,-s;s,c]`、`1/zoom`、视口映射），
   然后 **取逆** 传给 `makeShader(..., &localMatrix)`：

```cpp
SkMatrix localMatrix;
if (!deviceToImage.invert(&localMatrix)) { return; }
shader = effect->makeShader(std::move(data), SkSpan<const ChildPtr>(children, 1), &localMatrix);
```

- **必须传逆矩阵**：Skia 的 localMatrix 语义是"shader 局部空间 → 绘制局部空间"，直接传正向会把图缩成一小块（实测）。
- **必须 `canvas->clipRect(viewport)`**：否则放大后图像会溢出到左右留边区域；D3D12 是靠 viewport/scissor 裁的，两边要对齐（实测加了 clip 后 zoom 与 D3D12 diff = 0）。

SkSL 里只剩边界判断 + 色彩链：

```sksl
half4 main(float2 coord) {
    // coord 已被 localMatrix 变成图片像素坐标
    if (coord.x < 0.0 || coord.x > imageSize.x ||
        coord.y < 0.0 || coord.y > imageSize.y) {
        return half4(0.02, 0.02, 0.02, 1.0);
    }
    ... 色彩链 ...
}
```

### 2.4 图像上传

```cpp
const SkImageInfo info = SkImageInfo::Make(w, h,
    kR16G16B16A16_unorm_SkColorType, kOpaque_SkAlphaType);
const SkPixmap pixmap(info, rgba16, info.minRowBytes());   // UNORM16 直接当 16-bit unorm 用，采样自动归一化
sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixmap);
```

> M155 里颜色类型名是 `kR16G16B16A16_unorm_SkColorType`；旧名 `kRGBA_16161616_SkColorType` 已不存在。

### 2.5 验证证据（Windows，2026-11-10）

| 检查 | 结果 |
|---|---|
| `windows-x64-skia` Release 构建 | exit 0，无警告 |
| `uniformSize` / `sizeof` | 48 / 48 |
| Skia q0 vs 手写 D3D12 q0 | **0**（逐像素一致） |
| Skia zoom vs D3D12 zoom | **0** |
| A 旋转 q0→q90 / R 复位 | 237.9 / 0 |

---

## 3. 已知坑（Windows 踩过，mac 很可能复现）

| 现象 | 原因 | 处理 |
|---|---|---|
| SkSL 编译失败：`no match for pow(float3, float)` | SkSL 不允许向量/标量混参 | 标量包成 `float3(...)`；`max()` 同理 |
| SkSL 编译失败：`'const' variable initializer must be a constant expression` | 局部变量加了 `const` | 局部变量去掉 `const`（只有字面量常量保留） |
| 黑屏、无报错 | `makeShader` 返回 null（uniform 大小/顺序不匹配），`setShader(nullptr)` 退化成默认黑色 | 打印 `uniformSize()` / `sizeof`；失败时写日志，不要静默 |
| 图缩成一小块 / 位置错 | localMatrix 传了正向矩阵 | 传 `invert()` 后的矩阵 |
| 放大后内容溢出留边 | 没 clip 到视口 | `canvas->clipRect(viewport)` |
| `float3x3` uniform 对不上 | 按 std140 算了 48 B | SkSL 是 36 B 紧凑排列 |
| Debug 链接失败 LNK2038 | 预编译 Skia 是 `/MD` | 只用 Release（mac 同理，见 §4.2） |
| 改 `.sksl` 不生效 | `POST_BUILD` 拷贝只在链接阶段执行 | 重新 build；或直接改输出目录 `shaders/` 里的副本再启动 |

---

## 4. macOS 适配方案

### 4.1 目标结构：抽 `SkiaNativeBridge`

现在 `SkiaRenderer.cpp` 里混着 D3D12 专属逻辑（`gfx::D3D12Context`、`GrDirectContexts::MakeD3D`、D3D 离屏纹理、`CopyResource`）。mac 上要做的第一步是把它们抽出来：

```
src/render/skia/
  SkiaRenderer.hpp/.cpp           平台无关：SkSL 加载、SkSurface 绘制、色彩 uniform、交互矩阵、setImage
  SkiaNativeBridge.hpp            纯 C++ 抽象接口（只被 chroma_render_skia 包含）
  SkiaNativeBridgeD3D12.cpp       Windows：D3D12Context + 离屏 D3D 纹理 + CopyResource
  SkiaNativeBridgeMetal.mm        macOS：MetalContext + 离屏 MTLTexture + blit
```

建议接口（`SkiaNativeBridge.hpp`，mac 侧可先照这个写）：

```cpp
#pragma once

#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"

#include <cstdint>

class GrDirectContext;

namespace chroma::render
{
class SkiaNativeBridge
{
public:
    virtual ~SkiaNativeBridge() = default;

    virtual bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) = 0;
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;
    virtual void waitForGpu() = 0;

    // 帧生命周期
    virtual bool beginFrame() = 0;     // 取 drawable；失败（nil）返回 false，跳过这一帧
    virtual void endFrame() = 0;       // present + commit + wait（与 MetalContext 一致）

    // Ganesh 上下文（Windows: MakeD3D / macOS: MakeMetal）
    virtual sk_sp<GrDirectContext> makeGaneshContext() = 0;

    // 离屏可渲染 surface（RGBA16F；Windows 用 WrapBackendRenderTarget 包 D3D 纹理）
    virtual sk_sp<SkSurface> createRenderSurface(int width, int height) = 0;

    // 把离屏 surface 的内容拷贝到后台缓冲（Windows: CopyResource / macOS: blit）
    virtual void copySurfaceToScreen(SkSurface &surface) = 0;

    virtual std::uint32_t width() const = 0;
    virtual std::uint32_t height() const = 0;
};
} // namespace chroma::render
```

`SkiaRenderer.cpp` 保留的部分：`setImage`（SkImage 创建）、`drawFrame`（SkSL + localMatrix + clipRect）、色彩 uniform 平台分支、`setQuarterTurns/setZoom`、`resize`（调 bridge + 重建 surface）。

### 4.2 前置：mac 需要一份 `skia_use_metal=true` 的 Skia 预编译包

当前 `external/skia` 是 Windows 包（`args.gn` 里 `skia_use_direct3d=true`），**没有 Metal 后端实现**（头文件有 `include/gpu/ganesh/mtl/`，但 `.a` 里没有对应符号）。

mac 上的构建建议（参照 `docs/third-party/skia.md` §2/§6）：

```gn
target_os = "mac"
target_cpu = "arm64"           # 或 "x64"
is_debug = false               # 只做 Release（与现有流程一致）
skia_use_metal = true
skia_use_gl = false
skia_use_vulkan = false
skia_enable_graphite = false
```

产物布局与 Windows 一致：`include/`、`modules/skcms/`、`lib/libskia.a` 等，另外需要链接系统框架（`Metal`、`MetalKit`、`QuartzCore`、`CoreGraphics`、`CoreText`、`Foundation`、`AppKit`、`IOSurface` —— 以 ninja 产物实际依赖为准）。

`cmake/FindSkia.cmake` 需要加 **APPLE 分支**：找 `libskia.a`、define `SK_GANESH;SK_METAL`（按 Skia 的 GN public_defines）、链接 frameworks；Windows 分支保持现状。

### 4.3 Metal 实现要点

- **Ganesh 上下文**（`include/gpu/ganesh/mtl/`）：

```cpp
GrMtlBackendContext backend;
backend.fDevice = /* sk_cfp<GrMTLHandle>(id<MTLDevice>，adopt：先 retain 一次) */;
backend.fQueue  = /* 同上，id<MTLCommandQueue> */;
sk_sp<GrDirectContext> gr = GrDirectContexts::MakeMetal(backend, GrContextOptions{});
```

- **离屏纹理**：`MTLTextureDescriptor` 设 `MTLPixelFormatRGBA16Float`、
  `usage = MTLTextureUsageRenderTarget`、`storageMode = MTLStorageModePrivate`；
  包成 `GrMtlTextureInfo{ sk_cfp<GrMTLHandle>(texture) }`
  → `GrBackendRenderTargets::MakeMtl(w, h, info)`
  → `SkSurfaces::WrapBackendRenderTarget(gr, rt, kTopLeft_GrSurfaceOrigin, kRGBA_F16_SkColorType, nullptr, &props)`。

- **Present（第一版按"离屏 + blit"写，和 Windows 对称）**：
  1. `MetalContext::beginFrame()`（取 drawable + command buffer；nil 时跳过该帧）；
  2. Skia `drawFrame()` + `gr->flushAndSubmit()`；
  3. 用当前 frame 的 `MTLCommandBuffer`（`MetalContext::commandBuffer()`）建 `MTLBlitCommandEncoder`，
     `copyFromTexture:离屏纹理 toTexture:drawable.texture`（同尺寸、同格式，`MetalContext` 里 layer 已是 `MTLPixelFormatRGBA16Float`）；
  4. `MetalContext::endFrame()`（presentDrawable + commit + wait）。
  - 备选：`SkSurfaces::WrapCAMetalLayer(...)` 直接画 drawable、省一次拷贝；但会绕过 `MetalContext` 的帧生命周期，建议第二版再考虑。

- **shader 路径**：`SkiaRenderer::shaderPath()` 目前是 Windows 实现（`GetModuleFileNameW`）。mac 要在 bridge 或 `#if defined(__APPLE__)` 分支里换成 `NSBundle`：
  `[[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent] + "/shaders/fullscreen.sksl"`（即 `.app/Contents/MacOS/shaders/`）。
  为了不在 `.cpp` 里出现 ObjC，建议把"取 shader 路径"放进 bridge 接口（`virtual std::string shaderPath(const char* name) = 0;`）。

- **色彩**：同一份 `fullscreen.sksl`；C++ 侧 mac 分支给 **XYZ→Display-P3 行矩阵 + outputScale=1.0**（`SkiaRenderer.cpp` 已有 `#elif defined(__APPLE__)` 内容，抽 bridge 时保留在 renderer 侧即可）。
  与手写 Metal 后端的"PQ→XYZ→P3 线性"一致；**两端都还没做 EDR 元数据**，先不要开 `CAEDRMetadata`。

### 4.4 CMake（mac 分支）

当前 `src/render/CMakeLists.txt` 第 44 行左右是 `if(WIN32 AND CHROMA_WITH_SKIA)`。目标形态：

```cmake
if(CHROMA_WITH_SKIA AND (WIN32 OR APPLE))
    add_library(chroma_render_skia STATIC
        skia/SkiaRenderer.hpp
        skia/SkiaRenderer.cpp
        skia/SkiaNativeBridge.hpp)
    target_link_libraries(chroma_render_skia PRIVATE Skia::Skia chroma_gfx)
    target_compile_definitions(chroma_render_skia PRIVATE CHROMA_HAS_SKIA=1)

    if(WIN32)
        target_sources(chroma_render_skia PRIVATE skia/SkiaNativeBridgeD3D12.cpp)
        target_link_libraries(chroma_render_skia PRIVATE d3d12 dxgi)
    elseif(APPLE)
        target_sources(chroma_render_skia PRIVATE skia/SkiaNativeBridgeMetal.mm)
        target_link_libraries(chroma_render_skia PRIVATE
            "-framework Metal" "-framework MetalKit" "-framework QuartzCore"
            "-framework CoreGraphics" "-framework Foundation" "-framework AppKit")
        target_compile_options(chroma_render_skia PRIVATE
            "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    endif()

    chroma_set_warnings(chroma_render_skia)
    target_link_libraries(chroma_render PRIVATE chroma_render_skia)
    target_compile_definitions(chroma_render PRIVATE CHROMA_HAS_SKIA=1)
endif()
```

另外新增预设 `macos-arm64-skia`（Release-only）：

```json
{ "name": "macos-arm64-skia", "inherits": "macos-arm64",
  "displayName": "macOS arm64 + Skia (Release only)",
  "cacheVariables": { "CMAKE_BUILD_TYPE": "Release", "CHROMA_WITH_SKIA": "ON",
                      "CHROMA_DEFAULT_BACKEND": "skia" } }
```

### 4.5 适配步骤（每步可验收）

| 步 | 内容 | 验收 |
|---|---|---|
| **S-M1** | 构建 `skia_use_metal=true` 的 mac 包；`FindSkia.cmake` 加 APPLE 分支；建 `SkiaNativeBridge.hpp` + 空实现；CMake 加 APPLE 分支；工厂门控在 mac 也编过 | configure + build 绿（不启用 Skia 时行为不变） |
| **S-M2** | MetalBridge：Ganesh 创建 + 离屏 MTLTexture + `WrapBackendRenderTarget` + 读取 `fullscreen.sksl`；SkSL 先换成纯色 | 窗口出现纯色（`[gfx] backend = Skia`） |
| **S-M3** | 恢复 `fullscreen.sksl` 图像采样；`setImage` 的 UNORM16 → SkImage | PTMR 图方向/留边正确 |
| **S-M4** | 色彩（mac 分支 P3 矩阵 + outputScale 1.0）+ 交互（A/D/Q/E/R） | 与手写 Metal 后端对比；旋转/缩放/复位正常 |
| **S-M5** | 截图存 `docs/handoff/images/`；更新 `09` 的"mac 验证证据"；跑一次 M5 一致性 | 文档闭环 |

### 4.6 验收标准（mac）

- Release 预设构建 0 error；
- `CHROMA_BACKEND=skia`（或预设默认）启动，日志：`[skia] backend = Skia (Ganesh Metal)`、`[gfx] backend = Skia ...`；
- 图像内容、留边、A/D/Q/E/R 行为正确；
- 与手写 Metal 后端同图对比：几何一致，颜色按平台色彩管线解释（两端 `outputScale`/矩阵不同是刻意的）；
- 不启用 Skia 的 Debug 构建行为不变。

---

## 5. 风险 / 待定

| 项 | 说明 |
|---|---|
| Skia Metal 后端的 M155 导出符号 | 新包构建后先用最小探针验证 `MakeMetal` + `WrapBackendRenderTarget` 能创建 |
| `GrMtlBackendContext` 的 `sk_cfp` 语义 | adopt；需要先 `retain` 一次，避免 Skia 与 ARC 双重释放 |
| 离屏纹理生命周期 | resize 时必须先 `waitForGpu` 再释放旧纹理（与 Windows 的 `resize` 逻辑一致） |
| blit 的格式/尺寸 | drawable 与离屏都必须是 RGBA16Float、同尺寸；否则先创建匹配的中间纹理 |
| Release-only | mac 上 Skia 预编译包按 `is_debug=false` 构建；Debug 不接入 |
| EDR/HDR | 两端都未做 `CAEDRMetadata`，高光逐通道裁剪是已知现象 |
| 双平台色彩一致性 | 共享 PQ EOTF + 2020→XYZ；最后一步矩阵/outputScale 按平台不同（Windows 0.8 / mac 1.0），**不要**拿两端截图做逐像素比较 |

---

## 6. 给 mac 侧 agent 的启动提示词

```text
你是 ChromaCMS 项目的 macOS/Metal 方向协作 agent。Windows 侧已经完成
gfx 上下文层重构（822b976）和 Skia（Ganesh D3D12）第三后端（ff89ba9）。

请先读：
1. AGENTS.md
2. docs/handoff/README.md
3. docs/handoff/09-skia-backend.md   ← 最重要：Windows Skia 实现细节 + mac 适配方案
4. docs/handoff/02-collaboration-protocol.md
5. docs/handoff/08-current-progress.md（§3 分层、§6 MetalContext 已完成）
6. docs/third-party/skia.md（含 mac 需要的 Metal 版构建参数）

你的任务是按 09 §4 把 Skia 后端适配到 macOS/Metal：
- 先构建 skia_use_metal=true 的预编译包 + FindSkia.cmake 的 APPLE 分支；
- 抽 SkiaNativeBridge（Windows 实现保留，mac 写 SkiaNativeBridgeMetal.mm）；
- 同一份 shaders/fullscreen.sksl、同一套 uniform（48 B，float3x3 = 9 floats）；
- 交互走 makeShader 的 localMatrix（记得取逆）+ canvas clipRect；
- 每步在 temp_and_scratch 副本里构建/运行/截图验证后再交付。

规则（不可协商）：用户写 .cpp/.hpp/.mm/.hlsl/.metal 实现；agent 写 CMake、建空文件、
给逐处改法；交付前自验；不自动提交；头文件一律 .hpp；中文交流；
学习型注释用 // Q: ... / // A: ...；不要顺手重排用户代码。
```

---

## 7. 参考文件

| 文件 | 说明 |
|---|---|
| `src/render/skia/SkiaRenderer.cpp` | Windows 实现（382 行）：Ganesh / 离屏 / 帧流程 / localMatrix |
| `src/render/skia/SkiaRenderer.hpp` | 类声明（实现 `IRenderer` + `IViewTransform`） |
| `shaders/fullscreen.sksl` | SkSL：图像 + PQ 色彩链 + uniform 声明 |
| `src/render/RendererFactory.cpp` | `CHROMA_BACKEND` / `CHROMA_DEFAULT_BACKEND` |
| `src/gfx/d3d12/D3D12Context.hpp` | `currentBackBuffer()`、`beginFrame/endFrame` 语义 |
| `src/gfx/metal/MetalContext.hpp` | mac 的 device/queue/layer/drawable/commandBuffer 句柄 |
| `cmake/FindSkia.cmake` | Windows 分支现状；mac 需要加 APPLE 分支 |
| `docs/third-party/skia.md` | Skia 预编译包的来源/args/消费约束 |
| `temp_and_scratch/s3-*`（本地，gitignore） | Windows 验证截图（q0/q90/zoom/skia-vs-d3d12） |
