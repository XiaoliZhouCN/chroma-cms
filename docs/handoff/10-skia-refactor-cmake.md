# 10 · Skia 重构与 CMake 分层交接（Windows 继续开发）

> 基线：`practice` 本次 refactor 提交（`refactor/render`）。
> 读者：Windows 侧 agent / 用户本人。
> 目标：把当前 macOS 已跑通的 Skia 后端结构同步到 Windows，并保持 D3D12 手写后端不变。

---

## 0. 一句话

本次把旧的 `SkiaNativeBridge` 换成了：

```text
SkiaRenderer
  └─ SkiaGpuBackend
       ├─ SkiaGpuBackendD3D12.cpp
       └─ SkiaGpuBackendMetal.mm
```

同时把 `MetalRenderer` 拆成：

```text
MetalRenderer.hpp/.cpp/.mm + MetalRendererImpl.hpp
```

CMake 改成“每个构建目录一个 CMakeLists，父目录建 target，子目录追加 sources”。

---

## 1. 本次完成的改动

### 1.1 MetalRenderer 拆分

```text
src/render/metal/
  MetalRenderer.hpp          纯 C++，前置声明 MetalContext
  MetalRendererImpl.hpp      私有 C++ 状态 + ObjcState 前置声明
  MetalRenderer.cpp          接口实现 + 转发到 Objc 入口
  MetalRenderer.mm           ObjcState + MSL/PSO/texture/present
```

- `MetalRenderer.cpp`：`initialize/present/setImage` 转发到 `Impl::...Objc`；
  `resize/setQuarterTurns/setZoom/backendName` 直接 C++。
- `MetalRenderer.mm`：实现 `initializeObjc/presentObjc/setImageObjc` 和 `~Impl`。
- `ObjcState` 由 `MetalRenderer.mm` 持有，`MetalRenderer.cpp` 不接触 ObjC。

### 1.2 Skia 后端重构

删除：

```text
SkiaNativeBridge.hpp
SkiaNativeBridgeD3D12.cpp
SkiaNativeBridgeMetal.mm
```

新增：

```text
src/render/skia/
  SkiaRenderer.hpp          纯 C++ pimpl
  SkiaRenderer.cpp          公共 SkSL / Canvas / uniform / 交互 / setImage
  SkiaGpuBackend.hpp        平台后端接口
  SkiaGpuBackendD3D12.cpp   D3D12Context + Ganesh + 离屏 + CopyResource
  SkiaGpuBackendMetal.mm    MetalContext + Ganesh + 离屏 + blit
```

### 1.3 CMake 分层

新增：

```text
src/gfx/d3d12/CMakeLists.txt
src/gfx/metal/CMakeLists.txt
src/render/d3d12/CMakeLists.txt
src/render/metal/CMakeLists.txt
src/render/skia/CMakeLists.txt
```

删除：

```text
cmake/CompilerWarnings.cmake
```

其 Qt 定位逻辑合并进 `cmake/QtBootstrap.cmake`。

### 1.4 M5 ColorMath

新增：

```text
src/gfx/ColorMath.hpp
tests/unit/color_math_test.cpp
```

`ctest` 当前 2/2 通过。

---

## 2. Skia Renderer 结构

### 2.1 文件与职责

| 文件 | 职责 |
|---|---|
| `SkiaRenderer.hpp` | 纯 C++ `IRenderer` + `IViewTransform` pimpl |
| `SkiaRenderer.cpp` | 公共：SkSL、SkCanvas、uniform、contain-fit、localMatrix、setImage、交互 |
| `SkiaGpuBackend.hpp` | 平台接口：原生 context / Ganesh / surface / present |
| `SkiaGpuBackendD3D12.cpp` | Windows：D3D12Context + MakeD3D + 离屏 + CopyResource |
| `SkiaGpuBackendMetal.mm` | macOS：MetalContext + MakeMetal + 离屏 + blit |

### 2.2 `SkiaGpuBackend` 接口

```cpp
class SkiaGpuBackend
{
public:
    virtual ~SkiaGpuBackend() = default;

    virtual bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) = 0;
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    virtual sk_sp<GrDirectContext> makeGaneshContext() = 0;
    virtual sk_sp<SkSurface> createSurface(int width, int height) = 0;

    virtual bool beginFrame() = 0;
    virtual void endFrame(SkSurface &surface) = 0;

    virtual std::string shaderPath(const char *name) = 0;
    virtual const char *backendName() const = 0;
};

std::unique_ptr<SkiaGpuBackend> createSkiaGpuBackend();
```

职责边界：

```text
SkiaGpuBackend
  - 原生 context
  - Ganesh 上下文
  - 离屏纹理 / surface
  - beginFrame / endFrame（copy + present）

SkiaRenderer
  - GrDirectContext / SkSurface
  - SkSL / SkCanvas / uniform
  - contain-fit / localMatrix / clipRect
  - setImage / A-D-Q-E-R
```

### 2.3 调用流

```text
SkiaRenderer::initialize
  → createSkiaGpuBackend()
  → backend->initialize()
  → backend->makeGaneshContext()
  → load fullscreen.sksl
  → backend->createSurface()

SkiaRenderer::present
  → backend->beginFrame()
  → drawFrame()
  → gr->flushAndSubmit()
  → backend->endFrame(*surface)

SkiaRenderer::resize
  → surface.reset()
  → backend->resize()
  → backend->createSurface()
```

### 2.4 Windows 侧要点

`SkiaGpuBackendD3D12.cpp`：

- `gfx::D3D12Context`
- `GrDirectContexts::MakeD3D`
- 离屏 `ID3D12Resource`
- `CopyResource` + barrier
- `GetModuleFileNameW` 取 `shaders/fullscreen.sksl`
- 必须 include `include/core/SkColorSpace.h`

> 这份 D3D12 后端**还没有在 Windows 上实际编译/运行过**；  
> mac 侧只编译了 Metal 实现。Windows 拉取后第一件事是构建 `windows-x64-skia`。

---

## 3. CMakeLists 管理规范

### 3.1 目录结构

```text
CMakeLists.txt
src/
  CMakeLists.txt
  core/CMakeLists.txt
  media/CMakeLists.txt
  app/CMakeLists.txt
  gfx/
    CMakeLists.txt
    d3d12/CMakeLists.txt
    metal/CMakeLists.txt
  render/
    CMakeLists.txt
    d3d12/CMakeLists.txt
    metal/CMakeLists.txt
    skia/CMakeLists.txt
tests/
  CMakeLists.txt
  unit/CMakeLists.txt
cmake/
  QtBootstrap.cmake
  FindFFmpeg.cmake
  FindSkia.cmake
```

### 3.2 规则

1. **每个构建目录都有 `CMakeLists.txt`**；父目录负责 `add_subdirectory()`。
2. **父目录创建 target 和公共属性**：
   - `src/gfx` 创建 `chroma_gfx`
   - `src/render` 创建 `chroma_render`
3. **子目录只追加本目录的 sources / links / options**：
   - `gfx/d3d12` → `target_sources(chroma_gfx ...)`
   - `gfx/metal` → `target_sources(chroma_gfx ...)`
   - `render/d3d12` → `target_sources(chroma_render ...)`
   - `render/metal` → `target_sources(chroma_render ...)`
4. **平台目录只在对应平台 `add_subdirectory`**：
   ```cmake
   if(WIN32)
       add_subdirectory(d3d12)
   elseif(APPLE)
       add_subdirectory(metal)
   endif()
   ```
5. **Skia 是可选独立 target**：
   - `src/render/skia/CMakeLists.txt` 创建 `chroma_render_skia`
   - 父目录只在 `if(TARGET chroma_render_skia)` 时链接并定义 `CHROMA_HAS_SKIA`
6. **`cmake/` 只放可复用模块**：
   - `QtBootstrap.cmake`：Qt 定位 + `chroma_set_warnings()`
   - `FindFFmpeg.cmake`
   - `FindSkia.cmake`
   - 不再新增“历史拆分”模块
7. **INTERFACE 库作用域**：
   - INTERFACE target 只能用 `INTERFACE` 属性；
   - `chroma_set_warnings()` 只对真实库 target 调用。
8. **叶子目录不要重复创建父 target**，也不要从父目录直接列叶子源文件。

### 3.3 Target 关系

```text
ChromaCMS
  ├─ chroma_render
  │    ├─ chroma_gfx
  │    └─ chroma_render_skia（可选）
  │         ├─ Skia::Skia
  │         └─ chroma_gfx
  ├─ chroma_media
  └─ chroma_core
```

### 3.4 新增目录检查清单

- [ ] 目录有 `CMakeLists.txt`
- [ ] 父目录 `add_subdirectory()`
- [ ] 平台目录有平台条件
- [ ] 只追加 sources，不重复 `add_library`
- [ ] public include / links 放在父 target
- [ ] 可选 target 用 `if(TARGET ...)` 判断
- [ ] 重新 configure + build + ctest

---

## 4. Windows 继续开发步骤

### 4.1 拉取后先构建

```powershell
cmake --preset windows-x64-skia `
  -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build --preset windows-x64-skia
```

需要确认：

```text
external/skia/         Windows D3D12 预编译包存在
```

### 4.2 预期目标

```text
[skia] backend = Skia (Ganesh D3D12), milestone = 155
[gfx] backend = Skia (Ganesh D3D12)
```

状态栏：

```text
后端: Skia (Ganesh D3D12)
```

### 4.3 与手写 D3D12 对照

```powershell
$env:CHROMA_BACKEND="d3d12"
.\build\windows-x64-skia\Release\ChromaCMS.exe
```

确认：

- 手写 D3D12 仍正常；
- Skia 与 D3D12 的 q0/90/zoom/reset 对齐；
- `SkiaGpuBackendD3D12.cpp` 的 `SkColorSpace`、`CopyResource`、`shaderPath` 没有 Windows 编译问题。

### 4.4 Windows 侧优先修的问题

| 项 | 说明 |
|---|---|
| `SkiaGpuBackendD3D12.cpp` 首次编译 | mac 未编译过，可能有 Windows 头文件/类型差异 |
| `Skia::Skia` 链接顺序 | 保持 `skia + d3d12 + dxgi + d3dcompiler` |
| `SkiaRenderer.cpp` | 不应出现 `#if _WIN32` / ObjC |
| `shaders/fullscreen.sksl` | POST_BUILD 拷贝到 exe 旁 |
| 交互 | Windows 键码与 mac 不同，但 `IViewTransform` 已共用 |

---

## 5. mac 验证结果

```text
cmake --preset macos-arm64-debug   → configure=0 / build=0
cmake --preset macos-arm64-skia    → configure=0 / build=0
ctest                              → 2/2 passed
```

运行：

```text
[gfx] MetalContext device = Apple M6
[gfx] MSL compiled: VSMain / PSMain
[gfx] texture uploaded = 2048x2048
[gfx] backend = Metal

[skia] backend = Skia (Ganesh Metal), milestone = 155
[skia] image = 2048 x 2048
[gfx] backend = Skia (Ganesh Metal)
```

截图：

- `docs/handoff/images/macos-skia-q0.png`
- `docs/handoff/images/macos-skia-q90.png`
- `docs/handoff/images/macos-skia-zoom.png`
- `docs/handoff/images/macos-skia-reset.png`
- `docs/handoff/images/macos-skia-resize.png`

---

## 6. 已知缺口

| 项 | 状态 |
|---|---|
| Windows `SkiaGpuBackendD3D12.cpp` 实机编译/运行 | ⬜ 下一步 |
| Windows Skia vs 手写 D3D12 对照 | ⬜ |
| M5 CSV nits 断言 | 部分完成（端点/矩阵/输出 scale） |
| macOS EDR / `CAEDRMetadata` | ⬜ |
| 非方图 90° 实测 | ⬜ |

---

## 7. 参考文件

| 文件 | 说明 |
|---|---|
| `docs/ARCHITECTURE_DESIGN.md` | 当前架构设计（只读） |
| `src/render/skia/SkiaGpuBackend.hpp` | Skia 平台接口 |
| `src/render/skia/SkiaRenderer.cpp` | 公共 Skia 绘制 |
| `src/render/skia/SkiaGpuBackendD3D12.cpp` | Windows 后端（待实机验证） |
| `src/render/metal/MetalRendererImpl.hpp` | Metal 私有状态 |
| `src/render/CMakeLists.txt` | render 分层入口 |
| `src/gfx/CMakeLists.txt` | gfx 分层入口 |
| `cmake/QtBootstrap.cmake` | Qt + warnings |
