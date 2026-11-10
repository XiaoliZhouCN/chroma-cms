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
