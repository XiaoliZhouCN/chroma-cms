# 04 · 双平台架构（Dual Platform）

> 目标：Windows(D3D12) 与 macOS(Metal) 共用同一套窗口层、解码层、交互接口与验收标准。
>
> **当前色域是刻意分离的**：Windows 输出 BT.709/scRGB（`outputScale=0.8`），
> macOS 输出 Display-P3 线性（`outputScale=1.0`）。共享 PQ 与 BT.2020→XYZ，
> 最后一步矩阵按平台不同；不要追求逐像素一致。

## 1. 目标结构

```
src/
  core/            版本号等（平台无关）
  gfx/             平台相关"薄"层：D3D12Common.hpp / MetalCommon.hpp / 颜色数学 CPU 参考
  media/           FFmpeg 解码（平台无关）
  render/
    IRenderer.hpp          平台无关接口（**不要改签名**）
    IViewTransform.hpp     可选交互接口：setQuarterTurns / setZoom
    RendererFactory.hpp/.cpp   双平台工厂
    D3D12Renderer.*        Windows 后端
    MetalRenderer.hpp/.mm  macOS 后端
  app/             Qt 窗口 + ViewportWidget（按键/焦点逻辑两平台共用）
shaders/
  fullscreen.hlsl  Windows：PQ → 709 → scRGB
  fullscreen.metal macOS：PQ → Display-P3
```

## 2. 渲染器工厂（消除 `ViewportWidget` 里的硬编码）

### 新增 `src/render/RendererFactory.hpp`

```cpp
#pragma once

#include <memory>

namespace chroma::render {

class IRenderer;

/// 按平台创建后端：Windows -> D3D12，macOS -> Metal。
std::unique_ptr<IRenderer> createRenderer();

} // namespace chroma::render
```

### 新增 `src/render/RendererFactory.cpp`

```cpp
#include "render/RendererFactory.hpp"

#include "render/IRenderer.hpp"

#if defined(_WIN32)
#include "render/D3D12Renderer.hpp"
#elif defined(__APPLE__)
#include "render/MetalRenderer.hpp"
#else
#error "ChromaCMS: 暂不支持的平台"
#endif

namespace chroma::render {

std::unique_ptr<IRenderer> createRenderer()
{
#if defined(_WIN32)
    return std::make_unique<D3D12Renderer>();
#elif defined(__APPLE__)
    return std::make_unique<MetalRenderer>();
#endif
}

} // namespace chroma::render
```

### `ViewportWidget.cpp` 的改动（两平台共用）

```cpp
// 改前
#include "render/D3D12Renderer.hpp"
...
auto renderer = std::make_unique<render::D3D12Renderer>();

// 改后
#include "render/RendererFactory.hpp"
...
auto renderer = render::createRenderer();
```

其余逻辑（`initialize` / `setImage` / `resize` / `present`）完全不动。


## 2.1 可选交互接口 `IViewTransform`

`IRenderer` 的签名保持不变；旋转/缩放是可选能力，单独放一个接口：

```cpp
// src/render/IViewTransform.hpp
class IViewTransform
{
public:
    virtual ~IViewTransform() = default;
    virtual void setQuarterTurns(int quarterTurns) = 0;  // 正 = 逆时针 90°
    virtual void setZoom(float zoom) = 0;                // 1.0 = 原始
};
```

- `MetalRenderer` 已实现；
- `D3D12Renderer` **待实现**（见 `07-windows-interaction.md`）；
- `ViewportWidget` 用 `dynamic_cast<render::IViewTransform*>` 探测；
- app 层按键逻辑已经写好：A/D 旋转、Q/E 缩放、R 复位。

shader 参数统一为 8 个 float：

```text
outputScale, aspect, m00, m01, m10, m11, reserved0, reserved1
```

- 90° 旋转 + 缩放预先在 CPU 合成为 2×2 矩阵；
- 不用在 fragment shader 里算 `cos/sin`；
- `m00..m11` 已包含 `1/zoom`；
- 非方图 90°/270° 时，contain-fit 的宽高比要交换。

## 3. CMake 改动

### 3.1 顶层 `CMakeLists.txt`

```cmake
project(ChromaCMS VERSION 0.1.0 DESCRIPTION "..." LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)          # MetalRenderer.mm 需要
    set(CMAKE_OBJCXX_STANDARD 20)
endif()
```

### 3.2 `src/gfx/CMakeLists.txt`

```cmake
add_library(chroma_gfx INTERFACE)
target_include_directories(chroma_gfx INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_gfx INTERFACE cxx_std_20)

if(WIN32)
    target_link_libraries(chroma_gfx INTERFACE d3d12 dxgi)
elseif(APPLE)
    target_link_libraries(chroma_gfx INTERFACE
        "-framework Foundation"
        "-framework AppKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics")
endif()
```

### 3.3 `src/render/CMakeLists.txt`

```cmake
set(_chroma_render_sources
    IRenderer.hpp
    IViewTransform.hpp
    RendererFactory.hpp
    RendererFactory.cpp)

if(WIN32)
    list(APPEND _chroma_render_sources
        D3D12Renderer.hpp
        D3D12Renderer.cpp)
elseif(APPLE)
    list(APPEND _chroma_render_sources
        MetalRenderer.hpp
        MetalRenderer.mm)
endif()

add_library(chroma_render STATIC ${_chroma_render_sources})

target_link_libraries(chroma_render PUBLIC chroma_gfx)

if(WIN32)
    target_link_libraries(chroma_render PUBLIC d3dcompiler)
endif()

if(APPLE)
    target_compile_options(chroma_render PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
endif()

chroma_set_warnings(chroma_render)
```

> `.mm` 文件不需要单独声明 `LANGUAGE OBJCXX`，CMake 会按扩展名识别（前提是顶层 `enable_language(OBJCXX)`）。

### 3.4 `src/app/CMakeLists.txt`

```cmake
# 着色器拷贝：两平台通用（macOS 上是 .app/Contents/MacOS/shaders）
# 注意：POST_BUILD 只在目标重新链接时执行；只改 shader 时需要手动拷贝，
# 或依赖各生成器对 LINK_DEPENDS 的支持（Ninja 已配置，VS 生成器不一定）。
add_custom_command(TARGET ChromaCMS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:ChromaCMS>/shaders"
    COMMENT "复制 shaders 到输出目录")

# FFmpeg DLL：仅 Windows
if(WIN32 AND DEFINED FFMPEG_DLL_DIR AND EXISTS "${FFMPEG_DLL_DIR}")
    ...
endif()

# Qt 部署：Windows 用 windeployqt；macOS 开发阶段可直接跑 .app
if(WIN32 AND CHROMA_QT_ROOT AND EXISTS "${CHROMA_QT_ROOT}/bin/windeployqt.exe")
    ...
endif()
```

### 3.5 `cmake/FindFFmpeg.cmake`（macOS 支持）

需要补三点：

1. 搜索候选里加 Homebrew 路径：`/opt/homebrew/opt/ffmpeg`、`/usr/local/opt/ffmpeg`；
2. `find_library` 在 macOS 上会找到 `.dylib`，逻辑本身不用改；
3. `FFMPEG_DLL_DIR` 只在 Windows 有意义——保留变量，但在 app 里加 `WIN32` 守卫。

```cmake
if(NOT FFMPEG_ROOT)
    set(_chroma_ffmpeg_candidates
        "${PROJECT_SOURCE_DIR}/external/ffmpeg"
        "${PROJECT_SOURCE_DIR}/third_party/ffmpeg"
        "/opt/homebrew/opt/ffmpeg"     # macOS Apple Silicon
        "/usr/local/opt/ffmpeg")       # macOS Intel
    ...
endif()
```

## 4. CMakePresets（macOS 部分已加进仓库）

```json
{
  "name": "macos-arm64",
  "hidden": true,
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/build/${presetName}",
  "cacheVariables": {
    "CMAKE_OSX_ARCHITECTURES": "arm64",
    "CMAKE_OSX_DEPLOYMENT_TARGET": "13.0",
    "CMAKE_PREFIX_PATH": "$penv{QT_ROOT}"
  },
  "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Darwin" }
},
{ "name": "macos-arm64-debug",   "inherits": "macos-arm64", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
{ "name": "macos-arm64-release", "inherits": "macos-arm64", "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
```

用法：

```bash
export QT_ROOT=$(brew --prefix qt)
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
```

> Intel Mac 用 `-DCMAKE_OSX_ARCHITECTURES=x86_64` 覆盖即可。

## 5. 一致性保证（双平台等价性）

### 5.1 单一事实来源：CPU 参考实现 + 单元测试（M5 待做）

在 `src/gfx/ColorMath.hpp`（header-only，纯 C++）里实现与着色器**逐行对应**的数学。
注意：共享部分到 XYZ；最后一步需要同时提供 `xyzToRec709`（Windows）和 `xyzToDisplayP3`（macOS）。

```cpp
namespace chroma::gfx {

struct Rgb { float r, g, b; };

// PQ EOTF：code（0..1）-> 相对亮度（1.0 = 10000 nits）
inline Rgb pqEotf(const Rgb& code);

// BT.2020 线性 RGB -> CIE XYZ (D65)
inline Rgb rec2020ToXyz(const Rgb& linear2020);

// XYZ -> BT.709 线性 RGB（Windows）
inline Rgb xyzToRec709(const Rgb& xyz);

// XYZ -> Display-P3 线性 RGB（macOS）
inline Rgb xyzToDisplayP3(const Rgb& xyz);

// 相对亮度 -> ÷80 × outputScale
inline Rgb linearToOutput(const Rgb& linear, float outputScale);

} // namespace chroma::gfx
```

在 `tests/unit/color_math_test.cpp` 里断言：

| 断言 | 期望 |
|---|---|
| `pqEotf(0) == 0` | 黑 |
| `pqEotf(1) ≈ 1.0` | 10000 nits |
| 用 `tools/gen_ptmr.py` 生成的 CSV 中若干 `code10` 反算 nits | 与 CSV 误差 < 0.5% |
| `xyzToRec709(rec2020ToXyz({1,1,1})) ≈ {1,1,1}` | Windows 白点不变 |
| `xyzToDisplayP3(rec2020ToXyz({1,1,1})) ≈ {1,1,1}` | macOS P3 白点不变 |
| `linearToOutput(1.0, 0.8)` | Windows scRGB 基准 |
| `linearToOutput(1.0, 1.0)` | macOS 输出基准 |

> 两个平台跑同一份测试 → 只要测试绿，就说明**两侧数学常量没跑偏**。
> 着色器里仍各自写着公式，但常量表由测试锁定。

### 5.2 画面验收：对照截图 + 断言顺序

由于 Windows DWM 与 macOS EDR 的显示映射不同，**两平台截图不会逐像素相同**。验收时看：

1. 色带顺序与朝向（灰/红/绿/蓝/青/黄/品/灰，左暗右亮）；
2. 灰阶单调递增、暗部能分出层次；
3. 高光不整体泛白；
   - Windows 若整体泛白，先查 PQ 解码或 `/80`；
   - macOS 当前 `outputScale=1.0` 且无 `CAEDRMetadata`，高光逐通道裁剪是已知情况；先看数值/Windows 对照。
4. 转角标记方向（纹理阶段）。

基准图：`images/expected-step4-texture.png`、`images/expected-step6-scrgb.jpg`。

### 5.3 调试模式（可选，强烈建议）

在着色器里加一个编译期开关，把平台最后矩阵的输出（或 nits）直接当灰度输出，两平台并排对比：

```hlsl
// HLSL: #define DEBUG_LINEAR 1 时
return float4(linear709 * 10000.0 / 10000.0, 1.0);   // Windows：0..1 相对亮度
```

```metal
// MSL 同理：macOS 用 linearP3
return float4(linearP3, 1.0);
```

这样能绕开两平台的显示映射差异，直接比较"解码结果"。

## 6. macOS 侧文件（已完成）

| 文件 | 状态 | 说明 |
|---|---|---|
| `src/render/MetalRenderer.hpp` | ✅ | pimpl 声明 |
| `src/render/MetalRenderer.mm` | ✅ | 设备/图层/PSO/纹理/PQ→P3/交互 |
| `shaders/fullscreen.metal` | ✅ | VS + PS(PQ→P3) + 2×2 矩阵 UV |
| `src/render/RendererFactory.hpp/.cpp` | ✅ | 双平台工厂 |
| `src/render/IViewTransform.hpp` | ✅ | 可选交互接口 |
| `src/gfx/ColorMath.hpp` | ⬜ | CPU 参考（M5） |
| `tests/unit/color_math_test.cpp` | ⬜ | 一致性测试（M5） |

CMake 由 agent 维护（§3），用户只写实现。

## 7. 提交与分支建议

- macOS 上的改动继续提交到 **`practice`**（同一练习分支），保持"一条手写线"；
- 如果 Metal 侧改动风险大，可开 `practice-macos`，但**不要**与 `develop` merge；
- 每完成一个 M 步骤提交一次，信息参考：`feat(metal): M1 device and CAMetalLayer clear`。
