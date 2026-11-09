# 08 · 当前进度与 macOS 重构交接（2026-11-09）

> **这份文档补的是 `07` 之后的缺口。** `README` / `01` 还停在 `e545d91`，实际上 Windows 侧又推进了两段：
> ① 交互适配落地并提交（`42518f8`）；② 渲染层重构（`gfx` 上下文层 + 后端分目录，本次提交）。
>
> 基线：`practice` @ `42518f8`（已提交）+ 本次重构提交
> `refactor(render): split gfx context layer and backend dirs`（与本文件同一次提交）。
> 读者：换到 Mac 上的 agent / 用户本人。先读 `AGENTS.md`、`README.md`、`01`、`02`，再读本文件。

---

## 0. 一句话

Windows 侧把"一个 777 行的 `D3D12Renderer`"拆成了 **`gfx/` 平台上下文层 + `render/d3d12/` 后端目录**，
并把 Metal 后端搬进 `render/metal/`；同时为第三个后端 Skia 做了**导入准备**（`FindSkia.cmake` + 第三方文档 +
探针源码，**还没进构建图**）。

Mac 侧要做的对应改造是：**把 `MetalRenderer` 里的设备 / 图层 / 呈现拆成 `gfx/metal/MetalContext`**，
让两个平台拥有同构的三层（app → render → gfx），然后继续 M5 一致性。

---

## 1. 时间线（`07` 之后发生了什么）

| 时间 | 事件 | 说明 |
|---|---|---|
| 11-09 01:38 | `49caf72` handoff 文档 | 基于 `e545d91`，把 Windows 交互当成"下一步"交给 Windows agent（= `07`） |
| 11-09 02:00 | **`42518f8` `[win][image] sync interaction`** | **Windows 交互已实现并提交**：`shaders/fullscreen.hlsl`（8×float + `TransformUV` + `discard`）、`D3D12Renderer`（继承 `IViewTransform`、8 个 root constants、effectiveAspect）。`07` 的 5 处改动全部落地 ✅ |
| 11-09 13:08–13:12 | Skia 预编译包导入 | `external/skia`（M155，Windows+D3D12，1.07 GB，gitignore）；新增 `cmake/FindSkia.cmake`、`docs/third-party/skia.md`；写了最小探针源码 `temp_and_scratch/skia-probe/`（**没留下构建/运行证据**，见 §5） |
| 11-09 13:31–13:58 | 渲染层重构 | 新增 `src/gfx/d3d12/D3D12Context.{hpp,cpp}`；`src/render/D3D12Renderer.* → d3d12/`、`MetalRenderer.* → metal/`；`chroma_gfx` 在 Windows 下由 INTERFACE 变 STATIC；`render/CMakeLists.txt`、`RendererFactory.cpp` 路径更新 |
| 11-09 13:44 | 旧版本的一次完整验证 | 在 `temp_and_scratch/ctx` 里 configure/build/run；交互 diff：q0-q90=212.68、q0-zoom=205.22、q0-reset=0.02 |
| 11-09 20:33–20:54 | 用户继续整理实现 | `D3D12Renderer.hpp/.cpp` 把设备/交换链/fence 相关成员与函数改成注释留档，`D3D12Context` 补访问器；20:54 构建产物更新（当时的树是绿的） |
| 11-09 21:30–22:xx | **本次交接** | 独立复核（clean build + ctest + GUI + 交互 diff）→ 写本文件 → 提交；顺手修了 macOS 会直接踩到的 `chroma_gfx` 作用域问题（§3.5） |

> 说明：原 Windows developer agent 的会话中途损坏，09:00–21:00 之间"哪个版本验过、哪个版本只是改完没验"没有记录。
> 本文件 §4 的复核是**对最终工作树**重新做的，不依赖中间日志。

---

## 2. 已提交的进度：Windows 交互（对照 `07`）

`07` 列出的 5 处改动全部已在 `42518f8` 完成，本次重构后行为不变：

| `07` 的要求 | 现在的代码位置 | 状态 |
|---|---|---|
| 继承 `IViewTransform`，加两个 setter | `render/d3d12/D3D12Renderer.hpp`（class 声明）、`.cpp` `setQuarterTurns/setZoom` | ✅ |
| root signature 4 → 8 个 DWORD | `.cpp` `createRootSignature()`：`params[1].Constants.Num32BitValues = 8` | ✅ |
| `present()` 合成 2×2 矩阵并传 8 个 float | `.cpp` `present()`：`shaderParams[8]` + `SetGraphicsRoot32BitConstants(1, 8, ...)` | ✅ |
| contain-fit 在 90°/270° 交换宽高比 | `.cpp` `present()`：`effectiveAspect = (m_quarterTurns & 1) ? 1/aspect : aspect` | ✅ |
| HLSL `cbuffer` 8 float + `TransformUV` + `discard` | `shaders/fullscreen.hlsl` | ✅ |

重构后复核（`temp_and_scratch/verify/ui-*.png`，未入库）：

- 按 A → 90°：截图平均差 **212.66**；按 E → 放大：**205.22**；按 R → 复位：**0.00**（与 q0 完全一致）。
- 启动日志：`[media] 载入图像: ... 2048 x 2048`、`[gfx] backend = D3D12 (native) surface = 1600 x 974 dpr = 1.25`。

**色彩没有变**：Windows 仍是 BT.709 / scRGB、`outputScale = 0.8`；mac 仍是 Display-P3 线性、`1.0`。
两端刻意不同，**不要**拿 mac 截图做逐像素比较。

---

## 3. 本次重构：把"原生资源"从渲染器里拆出去

### 3.1 新目录结构（`practice`）

```text
src/
  gfx/
    D3D12Common.hpp            ComPtr / ThrowIfFailed（全项目唯一 include <d3d12.h>/<dxgi*.h> 的地方）
    d3d12/
      D3D12Context.{hpp,cpp}   【新】设备/队列/交换链/RTV/命令录制/fence（299 行 .cpp）
    metal/                     【当前为空】下一步：MetalContext.{hpp,mm}（见 §6）
  render/
    IRenderer.hpp              不变
    IViewTransform.hpp         不变
    RendererFactory.{hpp,cpp}  只改了 include 路径
    d3d12/D3D12Renderer.{hpp,cpp}   瘦身：只剩绘制侧（777 → 520 行）
    metal/MetalRenderer.{hpp,mm}    原样搬家（.mm 只改第 1 行 include）
    skia/                      【未建】第三后端预留
```

### 3.2 分层职责

| 层 | 文件 | 负责 | 不负责 |
|---|---|---|---|
| `chroma_gfx` | `gfx::D3D12Context` | 设备/工厂/适配器/交换链/RTV 堆/后台缓冲/命令分配器与命令列表/fence 与 Win32 event/resize/`beginFrame`-`endFrame`/一次性命令 | 根签名、PSO、着色器、纹理上传、Draw |
| `chroma_render` | `render::D3D12Renderer` | 根签名、PSO、HLSL 编译、SRV 堆与纹理上传、`present()` 里的 clear / 绑定 / 8 个常量 / contain-fit / Draw | 原生资源生命周期与同步 |

设计目的：**同一个 `D3D12Context` 将来能被 `SkiaRenderer`（Ganesh + D3D12）复用**——
这正是 `D3D12Context` 暴露 `adapter()`（Skia 的 `GrD3DBackendContext` 需要 adapter）的原因。

### 3.3 `gfx::D3D12Context` 公开 API

```cpp
// src/gfx/d3d12/D3D12Context.hpp
class D3D12Context
{
public:
    D3D12Context();
    ~D3D12Context();

    D3D12Context(const D3D12Context &) = delete;
    D3D12Context &operator=(const D3D12Context &) = delete;

    void initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);
    void waitForGpu();

    // 帧生命周期：beginFrame -> 渲染器录制 clear/draw -> endFrame（含 Present）
    void beginFrame();          // waitForGpu + allocator/list Reset + PRESENT→RT barrier
    void endFrame();            // RT→PRESENT barrier + Close + Execute + Present + 更新 frameIndex

    // 一次性命令（纹理上传等）：beginCommandList -> 录制 -> submitAndWait
    void beginCommandList();
    void submitAndWait();       // Close + Execute + waitForGpu

    ID3D12GraphicsCommandList *commandList() const;
    D3D12_CPU_DESCRIPTOR_HANDLE currentRtv() const;

    ID3D12Device       *device() const;
    ID3D12CommandQueue *queue() const;
    IDXGIAdapter1      *adapter() const;       // 当前无人调用，为 Skia 预留
    IDXGISwapChain3    *swapChain() const;     // 当前无人调用
    DXGI_FORMAT         swapChainFormat() const;

    std::uint32_t width() const;
    std::uint32_t height() const;
    std::uint32_t frameIndex() const;          // 当前无人调用
};
```

`D3D12Renderer` 的调用点：

- `initialize()`：`m_context = make_unique<gfx::D3D12Context>()` + `initialize(...)`，然后建根签名 / PSO / SRV 堆；
- `createTextureFromImage()`：`beginCommandList()` → `commandList()` 录制拷贝 + barrier → `submitAndWait()`；
- `present()`：`beginFrame()` → `commandList()` + `currentRtv()` 录制 → `endFrame()`（含 Present）；
- `resize()`：直接转发给 context。

### 3.4 `render::D3D12Renderer` 现在只剩什么

保留：`m_context`、`m_rootSignature`、`m_pipeline`、`m_srvHeap`、`m_texture`、
`m_imageWidth/m_imageHeight`、`m_outputScale = 0.8f`、`m_quarterTurns`、`m_zoom`、`m_initialized`。

删掉的旧成员/私有函数（`m_device`、`m_queue`、`m_factory`、`m_swapChain`、`m_rtvHeap`、`m_rtvHandles[]`、
`m_renderTargets[]`、`m_allocator`、`m_commandList`、`m_fence`、`m_fenceEvent`、`m_fenceValue`、
`createDeviceAndQueue`、`createSwapChain`、`createDescriptorHeaps`、`createRenderTargets`、`waitForGpu`）
在 `.hpp` 里**以注释形式留档**（学习记录，不是死代码告警原因）。

### 3.5 CMake 变化（含本次修正）

`src/gfx/CMakeLists.txt`：

- `WIN32`：`chroma_gfx` 由 `INTERFACE` 改为 **`STATIC`**（要编 `D3D12Context.cpp`），
  `PUBLIC` 链接 `d3d12 dxgi`、`PUBLIC` 定义 `NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE`；
- `APPLE` / 其他：仍是 `INTERFACE`（`MetalContext` 还没落地）；
- **作用域按目标类型判定**（本次修正）：`INTERFACE` 库只能设 `INTERFACE` 属性，
  所以 `target_include_directories` / `target_compile_features` 用 `get_target_property(... TYPE)` 决定
  `INTERFACE` 还是 `PUBLIC`；`chroma_set_warnings()`（内部是 PRIVATE `target_compile_options`）只对真实库目标调用。

  原因：重构初版无条件写 `PUBLIC` + `chroma_set_warnings`，**macOS 会在 configure 阶段直接失败**
  （`target_include_directories may only set INTERFACE properties on INTERFACE targets`，已用最小 CMake 用例复现）。
  Windows 分支行为不变（仍 STATIC + PUBLIC + `/W4`）；等 `MetalContext` 落地、APPLE 分支变 STATIC 后自动恢复 `PUBLIC`。

`src/render/CMakeLists.txt`：只把源文件路径改到 `d3d12/`、`metal/`；
其余（STATIC、link `chroma_gfx`、WIN32 链 `d3dcompiler`、APPLE 的 `-fobjc-arc`）不变。

顶层 `CMakeLists.txt` / `CMakePresets.json` / `src/app/CMakeLists.txt` **没有改**。

### 3.6 没有改的东西（Mac 侧不用跟着动）

- `IRenderer` / `IViewTransform` 接口签名；
- `ViewportWidget` / `MainWindow` / `main.cpp`；
- `shaders/fullscreen.{hlsl,metal}` 与色彩数学（PQ、BT.2020→XYZ、平台最后矩阵、`/80`、`outputScale`）；
- `RendererFactory` 的接口（只改了 include 路径）；
- FFmpeg 解码层（`src/media/`）。

### 3.7 已知问题（本次重构引入/暴露；**本批没有修**，等用户决定）

| # | 问题 | 位置 | 影响 |
|---|---|---|---|
| 1 | `present()` **无条件**绑定 SRV 并 `DrawInstanced` | `render/d3d12/D3D12Renderer.cpp` `present()`（旧代码的 `if (m_imageWidth > 0 && m_imageHeight > 0)` 守卫被注释进注释行，大括号块还在） | 正常启动会先 `setImage`，画面正常；但"没有图"时绑的是空 SRV，debug layer 可能报错。建议恢复守卫。mac 的 `MetalRenderer.mm` 有 `texture == nil` 分支可对照 |
| 2 | 注释写 "8 个 DWORD = 16 字节" | `D3D12Renderer.cpp` `createRootSignature()` | 行为正确（8×4 = **32** 字节），注释误导 |
| 3 | 命名空间注释写错 + 无用 include | `gfx/d3d12/D3D12Context.cpp` 结尾 `// namespace chroma::render`（实际 `chroma::gfx`）；`#include <d3dcompiler.h>` 未使用 | 无行为影响 |
| 4 | 每帧全同步 | `D3D12Context::beginFrame()` 开头 `waitForGpu()` | 旧设计，未改；无 per-backbuffer fence |
| 5 | `initialize` 返回类型不一致 | context 返回 `void`，renderer 返回 `bool` 且恒 `true`（异常由 `ViewportWidget` 兜底） | 无行为影响 |

> Mac 侧对应注意：**不要**把第 1 条照搬到 `MetalContext`（"无脑 Draw"）；维持 `MetalRenderer.mm`
> 现有的 `texture == nil` 早退更正确。

---

## 4. 本次复核的证据（Windows，2026-11-09）

| 项 | 结果 |
|---|---|
| 环境 | CMake 4.2.1 / VS 2026 x64 / MSVC 19.51.36252.0 / Win SDK 10.0.28000 / Qt 6.11.1 / FFmpeg `external/ffmpeg` |
| 验证位置 | `temp_and_scratch/verify/`（**不碰用户工作区**，用显式 `-S/-B` 构建，未用 preset） |
| Configure | exit 0；`Qt 6.11.1 @ ...`、`FFmpeg = ...` 正常 |
| Build（clean 全量） | exit 0；8 个编译单元 + 各 target mocs；**0 warning** |
| ctest | exit 0；`1/1 unit.smoke Passed`，`100% tests passed` |
| GUI 运行 | exit 0；窗口标题 `ChromaCMS 0.1.0 — practice`；stderr 含 `[media] 载入图像`、`[gfx] backend = D3D12 (native) ...` |
| 交互 | A→90° diff **212.66**、E→放大 **205.22**、R→复位 **0.00**（截图 `temp_and_scratch/verify/ui-{q0,q90,zoom,reset}.png`，未入库） |
| `git status` | 验证前后逐字节一致（除本次有意修改的 `src/gfx/CMakeLists.txt`） |

> 验证会话的沙箱禁止 libuv 命名管道：AUTOMOC 与 `windeployqt` 的 POST_BUILD 需在 scratch 里绕过。
> 这是**环境限制，不是仓库问题**；用户本机正常构建不受影响。
>
> **macOS 侧没有构建验证**（本机是 Windows）。"macOS 能 configure"是本次修 CMake 作用域后的**推断**，
> 需要在 Mac 上实测（§6.5 第 0 步）。

---

## 5. Skia 准备状态（只到"可复现"，没接进构建）

| 项 | 状态 | 说明 |
|---|---|---|
| `external/skia/` | ✅ 本地有，❌ 未入库 | M155（`SK_MILESTONE 155`）、Windows + D3D12（Ganesh，非 Graphite），约 1.07 GB，被 `.gitignore` 排除 |
| `cmake/FindSkia.cmake` | ✅ 新增，**未接线** | 提供 `Skia::Skia` imported target；**当前没有任何 CMakeLists `include` 它**（死文件，等 `SkiaRenderer`） |
| `docs/third-party/skia.md` | ✅ 新增 | 来源 / `args.gn` / 目录布局 / 消费约束（**必须 Release**、`/MD`、`SK_GANESH;SK_DIRECT3D;SK_GL`、`/utf-8`、`/bigobj`）/ D3D 入口 |
| 最小探针源码 | ⚠️ 只有源码 | `temp_and_scratch/skia-probe/`（未入库）：建 D3D 设备 → `GrDirectContexts::MakeD3D` → 打印 milestone；**没有留下构建/运行日志，交接时无法确认是否成功** |
| 顶层 `include(FindSkia.cmake)` / Release 预设 / `SkiaRenderer` 空壳 | ⬜ 未做 | 计划中的第三后端 |

计划定位：**第三后端** `SkiaRenderer`（实现 `IRenderer`），与 `D3D12Renderer` 共用 `D3D12Context`。
建议顺序：Windows 先把 `SkiaRenderer` 打通 → 再谈 mac；**不要**和 `MetalContext` 拆分混在一起做。

---

## 6. macOS 侧改造：MetalContext 拆分（本次交接重点）

### 6.1 换机前（Windows 上，已完成）

- 本次提交之后，`D3D12Context.*`、`FindSkia.cmake`、`docs/third-party/skia.md` 等 **untracked 文件全部入库**，
  Mac 直接 `git pull` 即可（等用户 push）。
- 换机前先在 Windows 上确认：`git show --stat HEAD` 里能看到 `src/gfx/d3d12/D3D12Context.*`。

### 6.2 第一步：CMake（已预修，仍要在 mac 上实测）

- 现状：`src/gfx/CMakeLists.txt` 的 APPLE 分支是 `INTERFACE` 库。本次已改成按目标类型判定作用域，
  **不会再因为 `INTERFACE` + `PUBLIC` 在 configure 阶段报错**。
- 但 APPLE 分支还没有 `MetalContext.mm` 可编。下一步把它变成 `STATIC`（agent 负责，用户不用动）：

```cmake
elseif(APPLE)
    # MetalContext 落地后：和 Windows 一样是 STATIC，末尾的作用域判定会自动变回 PUBLIC。
    add_library(chroma_gfx STATIC
        metal/MetalContext.hpp
        metal/MetalContext.mm)
    target_link_libraries(chroma_gfx PUBLIC
        "-framework Foundation"
        "-framework AppKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics")
    # MetalContext.mm 里的 id<...>/CAMetalLayer/NSView 用 ARC 管，与 chroma_render 保持一致。
    target_compile_options(chroma_gfx PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
```

- `src/render/CMakeLists.txt` **已就绪**（`metal/` 路径、APPLE ARC）；顶层已 `enable_language(OBJCXX)`；
  `src/app/CMakeLists.txt` 的 shader 拷贝已覆盖 `.metal`、`.app` 产物路径也不用动。**这三处不用改。**
- `src/gfx/metal/` 目录当前**不存在**（空目录不入 git），agent 建文件时先建目录。

### 6.3 `MetalContext` 建议形态（与 `D3D12Context` 对偶）

两条硬约束：

1. **头文件必须纯 C++**。`RendererFactory.cpp` 是纯 CXX 编译单元 → include `MetalRenderer.hpp`；
   一旦 `MetalContext.hpp` 里 `#include <Metal/Metal.h>`，那个 TU 直接编不过。
   → 用 pimpl + `void*` 不透明句柄，所有 `id<MTL...>` 只在 `.mm` 里 `__bridge` 回真实类型。
2. **ObjC 对象生命周期归 context**（device / queue / layer / 子 NSView）；
   renderer 只留"资源对象"（library / pipeline / sampler / texture）。

建议接口：

```cpp
// src/gfx/metal/MetalContext.hpp —— 纯 C++，pimpl
#pragma once

#include <cstdint>
#include <memory>

namespace chroma::gfx
{
class MetalContext
{
public:
    MetalContext();
    ~MetalContext();

    MetalContext(const MetalContext &) = delete;
    MetalContext &operator=(const MetalContext &) = delete;

    void initialize(void *nativeView, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);
    void waitForGpu();

    // 帧：beginFrame 取 drawable + 新建 command buffer；renderer 自建 render pass/encoder 录制
    bool beginFrame();          // nextDrawable == nil 返回 false（跳过这一帧，别崩）
    void endFrame();            // presentDrawable + commit + waitUntilCompleted（保持全同步）

    // 一次性命令（纹理上传）：beginCommandList 新建 cb；renderer 自建 blit encoder
    void beginCommandList();
    void submitAndWait();

    // 原生句柄：void* 只是"不透明指针"，只在 .mm 里 __bridge 转回 id<...>/CAMetalLayer*
    void *currentDrawable() const;   // id<CAMetalDrawable>
    void *commandBuffer() const;     // id<MTLCommandBuffer>（帧 / 上传通用）
    void *device() const;            // id<MTLDevice>
    void *queue() const;             // id<MTLCommandQueue>
    void *layer() const;             // CAMetalLayer*

    std::uint32_t colorFormat() const;   // MTLPixelFormat 原始值（RGBA16Float）
    std::uint32_t width() const;
    std::uint32_t height() const;
};
} // namespace chroma::gfx
```

与 `D3D12Context` **刻意不同**的 3 处（需要用户拍板，见 §9）：

1. `beginFrame()` 返回 `bool` —— `nextDrawable` 可能返回 nil，必须跳过该帧而不是崩（镜像 `MetalRenderer.mm` 现有判空）；
2. 没有 `currentRtv()`，改成 `currentDrawable()` —— `drawable.texture` 直接当颜色附件，不存在 RTV 描述符；
3. `commandList()` 改成 `commandBuffer()`；**render pass / encoder 由 renderer 自己建** ——
   clear color 留在 renderer（与 D3D12 的 `kClearColor` + `ClearRenderTargetView` 对偶），
   将来 `SkiaRenderer` 复用同一个 context 时也不会被 context 的 render pass 绑死。

**不存在的概念 → 不要硬造**：

| D3D12 概念 | Metal 现实 |
|---|---|
| RTV 描述符堆 / `CreateRenderTargetView` / `currentRtv()` | 不需要；`drawable.texture` 直接当颜色附件 |
| 根签名 / SRV 描述符堆 / 描述符表 | 不需要；`setFragmentTexture:atIndex:` 直接给对象 |
| `ResourceBarrier`（PRESENT↔RT、COPY_DEST→SHADER_READ） | 不需要（tracked 模式自动跟踪）；context 里**一行 barrier 都不要写** |
| `CommandAllocator::Reset` / `CommandList::Reset` | 不需要；每帧 / 每次上传新建 `MTLCommandBuffer` |
| `ID3D12Fence` + Win32 event | `waitUntilCompleted`（保持全同步）；要 pipelining 才上 `MTLSharedEvent` |
| `frameIndex()` / `GetCurrentBackBufferIndex` | 不存在；每帧 `nextDrawable`，没有可缓存的帧索引 |
| `adapter()` / DXGI 枚举 | 不存在；Metal 只有 `MTLCreateSystemDefaultDevice`。Skia 的 `GrMtlBackendContext` 只要 device + queue |
| `swapChain()` / `ResizeBuffers` | 不存在；`layer.drawableSize` + 子 view `frame` |
| `ClearRenderTargetView` | render pass 的 `loadAction = Clear` + `clearColor`（renderer 建 pass 时设置） |
| `SetColorSpace1` | `layer.colorspace = kCGColorSpaceExtendedLinearDisplayP3` + `wantsExtendedDynamicRangeContent = YES` |
| `Present(1, 0)` | `presentDrawable` + `commit` + `layer.displaySyncEnabled = YES` |

> 结论：Metal 的 `beginFrame/endFrame` 只是"取 drawable + 建/提交 cb + 等完成"的封装，
> **不要**为了对称加 barrier / 描述符堆之类的空壳。

### 6.4 `MetalRenderer` 要删 / 留什么（以当前 `src/render/metal/MetalRenderer.mm`，496 行为准）

**搬去 `MetalContext`：**

- `Impl` 成员 `device` :60、`queue` :61、`layer` :68、`host` :69、`metalView` :70、`width` :72、`height` :73；
- `initialize()`：device/queue 创建 :100-108；子 NSView + `CAMetalLayer` + colorspace + EDR :189-223；
  字段赋值 :225-235 里属于 context 的几项（library/pipeline/sampler 留在 renderer）；
- `resize()`：view/layer 三行 :256-264；
- `present()`：`nextDrawable` + nil 判空 :274-280、`MTLRenderPassDescriptor` + command buffer + encoder :285-300、
  以及 `presentDrawable/commit/waitUntilCompleted` :377-379
  （**注意**：`pass` 的 `clearColor` 与 `loadAction` 建议留在 renderer，见 §6.3 第 3 条）；
- `setImage()`：cb/blit 创建与提交 :464-481（**保留"提交后等完成"**）；
- 析构：`removeFromSuperview` :86-88。

**留在 `MetalRenderer`：**

- `Impl` 的 `library`、`pipeline`、`sampler`、`texture`、`imageWidth/imageHeight`、`quarterTurns/zoom`、`initialized`；
- MSL 读取 / 编译 / PSO / sampler :110-188；
- 8 个 float 的 `ShaderParams` :316-330；contain-fit :332-368；`drawPrimitives` :370；
- 纹理创建 / 256 行对齐 / staging :410-476。

**新增：**

- `Impl` 里加 `std::unique_ptr<gfx::MetalContext> context;`（`Impl` 定义在 `.mm` 里，
  所以 **`MetalRenderer.hpp` 零改动**）；
- PSO 的 `colorAttachments[0].pixelFormat` 用 `context->colorFormat()`，别再写死 `MTLPixelFormatRGBA16Float`；
- 编译 MSL 需要 device：`id<MTLDevice> dev = (__bridge id<MTLDevice>)m_impl->context->device();`。

### 6.5 迁移顺序（每步可单独验证）

0. **[Mac] 跑通现有树**（只改了 CMake，不改代码）：
   `export QT_ROOT=$(brew --prefix qt); cmake --preset macos-arm64-debug; cmake --build --preset macos-arm64-debug`，
   运行 `.app/Contents/MacOS/ChromaCMS`，确认 `[gfx] backend = Metal`、画面与 A/D/Q/E/R 正常
   —— 这是"重构前基线"。
1. **[agent]** 改 `src/gfx/CMakeLists.txt` APPLE 分支为 STATIC（§6.2），建 `src/gfx/metal/` 目录与
   `MetalContext.hpp` / `.mm` 两个 **0 字节**空文件。
2. **[用户]** 写 `MetalContext`：先只放 device/queue/layer/子 view + `initialize`/`resize`，renderer 暂不改
   → 构建应绿（新文件还没被引用）。
3. **[用户]** `MetalRenderer` 接上 context（删自建 device/layer，改调 context），**`present()` 先保持原样**
   → 验收：画面与第 0 步一致。
4. **[用户]** 把 `present()` 的 drawable/cb/present 搬进 `beginFrame`/`endFrame`
   （renderer 里用 `if (!m_impl->context->beginFrame()) return;`），renderer 只建 pass/encoder
   → 验收：A/D/Q/E/R 与截图对照。
5. **[用户]** 把 `setImage()` 的 cb/blit 换成 context 的 `beginCommandList()`/`submitAndWait()`
   → 验收：启动 / 切图纹理正常。
6. **[回归]** 截 q0 / 90° / zoom / reset 四张图，与第 0 步及 `docs/handoff/images/macos-*.png` 对照。
7. **[用户说 commit 才提交]** 信息参考：`refactor(metal): extract MetalContext from MetalRenderer`。

### 6.6 验收标准

- 日志：`[gfx] backend = Metal`；`[gfx] texture uploaded = 2048x2048 (... B/row)`（行宽应仍为 16384）；
- 画面：PTMR 色带顺序/朝向与重构前一致；A 一次 90° 逆时针、A 两次 180°、R 完全复位；
- 色彩：仍 Display-P3 线性、`outputScale = 1.0`、colorspace 仍是 `kCGColorSpaceExtendedLinearDisplayP3`；
- 不要求与 Windows 截图逐像素一致（两端刻意不同）。

---

## 7. 之后再做什么（Mac 侧）

1. **M5 双平台一致性**（`03` §8 / `04` §5 的计划不变）：
   `src/gfx/ColorMath.hpp`（header-only CPU 参考）+ `tests/unit/color_math_test.cpp`：
   PQ 端点、白点、PTMR CSV nits、`outputScale`；共享到 XYZ，最后一步矩阵按平台分别断言。
2. **macOS EDR / tone mapping**（可选）：`CAEDRMetadata`；当前 `outputScale=1.0` + 无 EDR metadata
   会高光逐通道裁剪（已知情况，不是 shader 错）。
3. **Skia**（后置）：需要 `skia_use_metal=true` 的 macOS 预编译包（`.a`，不是 `.lib`）。
   现在的 `cmake/FindSkia.cmake` **是 100% Windows/D3D12 专用**：硬编码 `lib/skia.lib`、
   链 `d3d12;dxgi;d3dcompiler`、定义 `SK_DIRECT3D`、要求 `include/gpu/ganesh/d3d/...`。
   Ganesh 的 Metal 入口是 `GrDirectContexts::MakeMetal(GrMtlBackendContext)`
   （M155 的精确签名 / 所有权语义**要在 Mac 上用头文件核对**；`sk_cfp::reset` 是 adopt 不是 retain，
   容易和 ARC 双重释放）。
   建议顺序：**MetalContext → M5 → Skia**。

---

## 8. mac 首跑常见错误（在 `05` 基础上新增）

| 现象 | 原因 | 处理 |
|---|---|---|
| configure 报 `target_include_directories may only set INTERFACE properties on INTERFACE targets` | `gfx/CMakeLists.txt` 的 APPLE 分支是 INTERFACE 库，却用了 PUBLIC / `chroma_set_warnings` | 本次已修（§3.5）；若又出现，说明该文件被回退了 |
| `Cannot find source file: .../metal/MetalContext.mm` | `src/gfx/metal/` 空目录不入 git，Mac clone 后不存在 | 先建目录 + 空文件（agent 建） |
| 纯 CXX 编译单元报 Objective-C 语法错 | `MetalContext.hpp` include 了 `<Metal/Metal.h>` | 头文件纯 C++ + pimpl + `void*`（§6.3） |
| include 找不到 | 照 `docs/handoff/03/04/07` 里的旧路径抄（`render/MetalRenderer.hpp`） | 以 `src/render/RendererFactory.cpp` 的 `render/metal/`、`render/d3d12/` 为准 |
| 纹理偶发花屏 / 闪一下 | 上传 cb 只 commit 没 `waitUntilCompleted` | `submitAndWait()` 必须等完成 |
| 偶发崩溃在 `nextDrawable` | drawable 超时返回 nil | `beginFrame()` 返回 bool，renderer 早退 |
| 其余（Qt 前缀、FFmpeg dylib、截图权限、shader 拷贝） | — | 见 `05` §7 |

---

## 9. 需要用户拍板的点

1. **MetalContext 的 encoder / render pass 归属**：本文件建议 "context 只管 drawable / cb / present，
   pass 与 encoder 由 renderer 建"。这会影响将来 `SkiaRenderer` 的复用方式。
2. **`waitForGpu()` 语义**：建议先保持全同步（`waitUntilCompleted`）；`MTLSharedEvent` 双帧以后再说。
3. **§3.7 的 5 个已知问题**是否顺手修（其中"`present()` 无图也 Draw"建议修；那是用户代码，agent 只给改法）。
4. **Skia 的定位**：第三后端可切换，还是替换手写后端？色彩管理走 Skia 的 `SkColorSpace`
   还是继续共享 shader / `ColorMath`？`IViewTransform` 是否用 canvas matrix 实现？
5. **mac 目标机信息**：Apple Silicon 还是 Intel（决定 preset 与 Skia 包架构）？macOS 版本
   （影响 `MTLMathModeSafe` 分支）？屏幕是否 P3 / EDR（决定 M4 / EDR 能否实测）。

---

## 10. 参考文件

| 文件 | 作用 |
|---|---|
| `_shirley/notes/pipeline_comparison.md` | **D3D12 vs Metal 逐项对照**（生命周期 / 同步 / 交换链 / 命令 / 坑表）；拆 MetalContext 前建议先读 |
| `docs/third-party/skia.md` | Skia M155 预编译包的来源、GN 参数、消费约束、D3D 入口 |
| `cmake/FindSkia.cmake` | `Skia::Skia`（Windows/D3D12 专用，未接线） |
| `src/gfx/d3d12/D3D12Context.{hpp,cpp}` | 要镜像到 Metal 的参考实现 |
| `src/render/d3d12/D3D12Renderer.cpp` | 拆分后 renderer 的参考形态（`present()` 如何用 context） |
| `src/render/metal/MetalRenderer.mm` | 被拆对象；§6.4 的行号都指向它 |
| `docs/handoff/03-metal-plan.md` | Metal 原理与 D3D12→Metal 映射（实现已超前于伪代码，注意 §0 的差异表） |
| `docs/handoff/04-dual-platform.md` | 双平台架构与一致性测试计划（§2/§3 的路径已过时，以本文件为准） |
| `docs/handoff/07-windows-interaction.md` | Windows 交互的完整改法（**已完成**，保留作历史/原理参考） |

---

## 11. 给 Mac 新会话的启动提示词（可直接粘贴）

```text
你是 ChromaCMS 项目的 macOS/Metal 方向协作 agent。Windows 侧刚完成"渲染层重构"，
现在要在 Mac 上把同一结构镜像到 Metal。请先读：
1. AGENTS.md
2. docs/handoff/README.md
3. docs/handoff/08-current-progress.md   ← 最重要：当前进度、重构内容、MetalContext 拆分方案
4. docs/handoff/03-metal-plan.md
5. docs/handoff/04-dual-platform.md
6. docs/handoff/05-macos-setup.md
7. _shirley/notes/pipeline_comparison.md（D3D12 vs Metal 对照）

现状：HEAD 是一次跨平台重构。Windows 已有 gfx/d3d12/D3D12Context（设备/交换链/同步）
+ render/d3d12/D3D12Renderer（只画）；Metal 还全在 render/metal/MetalRenderer.mm 里。
你的第一个任务是：按 08 §6，先把 gfx/metal/MetalContext 拆出来（与 D3D12Context 对偶），
再继续 M5 一致性。不要改 IRenderer / IViewTransform 接口，不要 merge develop。

规则（不可协商）：用户写 .cpp/.hpp/.mm/.hlsl/.metal 实现；你写 CMake、建空文件、给逐处改法；
交付前在 temp_and_scratch 副本里验证；不自动提交；头文件一律 .hpp；中文交流；
学习型注释沿用 // Q: ... // A: ...；不要顺手重排用户代码。

现在开始：先读上述文件，然后给出一份"MetalContext 拆分"的计划（改哪些文件、每处改什么、怎么验证），
等用户确认后再逐处给代码。
```
