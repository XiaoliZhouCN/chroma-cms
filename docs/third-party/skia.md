# 第三方依赖：Skia（预编译，Ganesh + D3D12 后端）

> 本文件是 **tracked 的清单**；实际二进制在 `external/skia/`（gitignore，不随仓库分发）。
> 复现方式见 §6。

## 1. 来源

| 项 | 值 |
|---|---|
| 库 | Skia（Google Skia Graphics Library） |
| 版本 | **M155**（`include/core/SkMilestone.h` 实测 `SK_MILESTONE 155`） |
| commit | `d2fe84592365b8d9eacf5f203bd50ac3545e2f44`（2026-09-02，`Improve path-based LCD text rendering`） |
| 上游 | `https://skia.googlesource.com/skia`（构建时用镜像 `https://gitee.com/mirrors/skia.git`） |
| 许可 | BSD-3-Clause（`external/skia/LICENSE`） |
| 本仓库路径 | `external/skia/`（约 **1.07 GB**，gitignore） |
| 原始构建产物 | 旧仓库 `D:\RepoStorage\Projects\ChromaCMS\ThirdParty\skia\out\pq-gpu` |

## 2. 构建配置（`external/skia/args.gn`）

```gn
target_os = "win"
target_cpu = "x64"
is_debug = false                    # Release + /MD
skia_use_gl = true
skia_use_egl = false
skia_use_direct3d = true            # ← D3D12 后端
skia_use_vulkan = false
skia_use_dawn = false
skia_enable_graphite = false
extra_cflags = ["/MD"]              # 关键：MSVC 默认 /MT，与 Qt6(/MD) 不一致会 LNK2038
extra_ldflags = ["/MD"]
```

## 3. 目录布局

```
external/skia/
  include/            公共头（include/core、include/gpu/ganesh/...）
  modules/skcms/      skcms.h（被 SkColorSpace.h 等引用）
  lib/
    skia.lib                 785 MB
    d3d12allocator.lib       1 MB    D3D12MemAlloc（GPUOpen）
    harfbuzz.lib             196 MB
    spirv_cross.lib          50 MB
    skcms.lib / expat.lib / libjpeg*.lib / libpng.lib / libwebp*.lib / wuffs.lib / zlib.lib
  args.gn
  LICENSE
```

## 4. 消费方约束（缺一不可）

| 约束 | 原因 |
|---|---|
| **必须 Release 配置** | 预编译包 `is_debug=false` → `/MD`。Debug 构建是 `/MDd`，会报 `LNK2038: _ITERATOR_DEBUG_LEVEL 不匹配` |
| 定义 `SK_GANESH` `SK_DIRECT3D` `SK_GL` | Skia 的 GN `public_defines`；否则头文件里 D3D 后端接口被 `#undef` 掉 |
| 编译选项 `/utf-8` | Skia 头文件含非 ASCII 注释 |
| 编译选项 `/bigobj` | Skia 模板实例化 `.obj` 节数超 MSVC 默认上限 |
| include 路径加 **Skia 根** | 公共头之间用 `#include "include/core/..."` 互相引用 |
| 链接 `d3d12 dxgi d3dcompiler` | D3D12 后端与 HLSL/DXBC 相关 |

以上全部由 `cmake/FindSkia.cmake` 的 `Skia::Skia` 自动带上。

## 5. 可用入口（M155 的 D3D12 后端 API）

```cpp
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"

GrD3DBackendContext backend;
backend.fAdapter = adapter;   // gr_cp<IDXGIAdapter1>（AddRef 后 adopt）
backend.fDevice  = device;    // gr_cp<ID3D12Device>
backend.fQueue   = queue;     // gr_cp<ID3D12CommandQueue>
// fMemoryAllocator 留空：Skia 内部会自动创建 GrD3DAMDMemoryAllocator

sk_sp<GrDirectContext> ctx = GrDirectContexts::MakeD3D(backend);
```

- 生命周期：`GrD3DBackendContext` 里的 D3D 对象必须比 `GrDirectContext` 活得久。
- 渲染到交换链：用 `GrBackendRenderTarget` 包一层 `ID3D12Resource`（后台缓冲），
  再 `SkSurfaces::WrapBackendRenderTarget(...)`，每帧 `ctx->flushAndSubmit()`。
- 注意 `GrD3DBackendContext.h` 会 include `d3d12.h` → `windows.h`，必须放在
  `NOMINMAX` / `WIN32_LEAN_AND_MEAN` 之后或做好隔离（本仓库放在 `.cpp` 内包含）。

## 6. 复现方式

1. 取 Skia 源码到 `ThirdParty/skia`（commit 见上），用 GN 生成 `out/pq-gpu`：
   参数见 §2（旧仓库 `manifest.md` §1.1 有完整说明与受限环境构建坑）。
2. `gn gen out/pq-gpu` + `ninja -C out/pq-gpu skia`
3. 复制到本仓库：
   ```powershell
   robocopy <skia>\include        external\skia\include /E
   robocopy <skia>\modules\skcms external\skia\modules\skcms /E
   robocopy <skia>\out\pq-gpu    external\skia\lib *.lib
   copy <skia>\LICENSE external\skia\
   copy <skia>\out\pq-gpu\args.gn external\skia\
   ```
4. 验证：`cmake/FindSkia.cmake` + 一个最小探针（创建 D3D12 设备 → `MakeD3D` → 打印 milestone）。

## 7. 与本项目的关系

- **Windows：已接入**（`ff89ba9`）。`SkiaRenderer` 实现 `IRenderer` + `IViewTransform`，
  用 Ganesh D3D12 渲染到 RGBA16F 离屏纹理，再 `CopyResource` 到交换链；
  SkSL 外置为 `shaders/fullscreen.sksl`（PQ 色彩链 + 交互）。
  构建开关 `CHROMA_WITH_SKIA=ON`、预设 `windows-x64-skia`，运行时 `CHROMA_BACKEND=skia`。
  细节与验证见 `docs/handoff/09-skia-backend.md`。
- **macOS：待做**。当前预编译包只有 Windows + D3D12 后端；mac 需要另建
  `skia_use_metal=true` 的产物，并在 `FindSkia.cmake` 加 APPLE 分支
  （参数与步骤见 `09` §4.2）。
- 由于 `/MD` 限制，Skia 后端只能在 **Release** 预设下构建；Debug 构建不接入 Skia。

## 8. macOS Metal 包（2026-11-11 已构建）

macOS 侧使用单独目录 **`external/skia-mac/`**，不覆盖 Windows 的 `external/skia/`。

### 8.1 来源与版本

| 项 | 值 |
|---|---|
| 源码 | `https://gitee.com/mirrors/skia.git`，分支 `chrome/m155` |
| commit | `a4213f447f333e76023148f45338a8b05e4060a5` |
| 里程碑 | M155（`SK_MILESTONE 155`） |
| GN | 从 `https://github.com/cglogic/gn` 源码构建（本机 appspot 不可达） |
| 构建目录 | `~/skia-build/skia/out/mac-metal`（不入库） |
| 包目录 | `external/skia-mac/`（gitignore） |

### 8.2 GN 参数（`external/skia-mac/args.gn`）

```gn
is_debug = false
target_os = "mac"
target_cpu = "arm64"
skia_use_metal = true
skia_use_gl = false
skia_use_vulkan = false
skia_use_dawn = false
skia_enable_graphite = false

# 本项目只用 Skia 的 GPU 绘制 + SkSL；图像解码仍由 FFmpeg 负责，
# 所以把可选 codec / 字体 / SVG / 压缩依赖全部关掉，避免拉第三方源码。
skia_use_partition_alloc = false
skia_use_harfbuzz = false
skia_use_expat = false
skia_use_jpeg_gainmaps = false
skia_enable_svg = false
skia_use_libpng_decode = false
skia_use_libpng_encode = false
skia_use_libjpeg_turbo_decode = false
skia_use_libjpeg_turbo_encode = false
skia_use_libwebp_decode = false
skia_use_libwebp_encode = false
skia_use_libavif = false
skia_use_libjxl_decode = false
skia_use_icu = false
skia_use_icu4x = false
skia_use_libgrapheme = false
skia_use_freetype = false
skia_use_fontconfig = false
skia_use_zlib = false
skia_use_wuffs = false
skia_use_dng_sdk = false
skia_use_piex = false
skia_use_perfetto = false
skia_enable_tools = false

cc = "clang"
cxx = "clang++"
extra_cflags = [ "-mmacosx-version-min=13.0" ]
extra_ldflags = [ "-mmacosx-version-min=13.0" ]
```

### 8.3 产物

```text
external/skia-mac/
  include/                 # 公共头（含 include/gpu/ganesh/mtl/）
  modules/skcms/           # skcms.h
  lib/
    libskia.a              # 317 MB
    libskcms.a             # 396 KB
  args.gn
  LICENSE
  MANIFEST.md
```

> 因为关掉了 codec/字体/SVG，mac 包只有 `libskia.a` + `libskcms.a`；
> 与 Windows 包的 `skia.lib + harfbuzz + libpng...` 布局不同。
> `SkiaRenderer` 的图像输入仍来自 FFmpeg 的 RGBA16，不需要 Skia codec。

### 8.4 最小探针（已通过）

`temp_and_scratch/skia-mac-probe.log`：

```text
ok milestone=155
```

探针覆盖：

```text
MTLCreateSystemDefaultDevice
  → GrDirectContexts::MakeMetal
  → MTLTexture(RGBA16Float, RenderTarget)
  → GrBackendRenderTargets::MakeMtl
  → SkSurfaces::WrapBackendRenderTarget
  → canvas->clear
  → gr->flushAndSubmit()
```

### 8.5 CMake 消费

`cmake/FindSkia.cmake` 已加 APPLE 分支：

- 默认搜索 `external/skia-mac`（可用 `-DSKIA_ROOT=` 覆盖）；
- 定义 `SK_GANESH;SK_METAL`；
- 链接 `libskia.a`、`libskcms.a` 和系统框架：
  `Foundation AppKit Metal MetalKit QuartzCore CoreGraphics CoreText IOSurface CoreVideo CoreFoundation`；
- OBJCXX 自动加 `-fobjc-arc`。

CMake 探针构建/运行：

```text
-- ChromaCMS: Skia (Metal) = .../external/skia-mac (M155, 仅 Release 配置可用)
ok milestone=155
```

> `src/render/CMakeLists.txt` 的 Skia target 仍只在 `WIN32` 下启用；
> mac 的 `SkiaGpuBackend` / `SkiaGpuBackendMetal.mm` 落地后打开 APPLE 分支。
