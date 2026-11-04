---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 构建与环境

> 当前阶段 M0：构建骨架尚未落地。文中标 **【待验证】** 的条目必须在 M1 期间实测后去掉标记；未验证的内容不得当成事实引用。

## 1. 前置环境

| 组件 | 版本 | 获取方式 |
|---|---|---|
| Windows | 10 1903+ / 11，x64 | — |
| Visual Studio 2022 | 17.10+，含 "使用 C++ 的桌面开发" | 官方安装器 |
| Windows SDK | 10.0.22621.0+ | VS 安装器勾选 |
| CMake | ≥ 3.28 | VS 自带或官方安装器 |
| Ninja | 任意近期版本 | vcpkg / VS 自带 |
| Git | ≥ 2.40 | 官方 |
| PowerShell | 5.1+（Windows 自带；无需装 PowerShell 7） | — |
| Python | 3.11+（Skia 构建脚本需要） | python.org |
| Qt | 6.8 LTS（MSVC 2022 x64） | vcpkg 或官方安装器 |
| FFmpeg | 开发包（shared 或 static） | vcpkg |
| Skia | 源码构建（见 §3） | depot_tools |

## 2. 目录布局（约定）

```
<repo>/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── src/  shaders/  tests/  assets/
└── external/            # 不入库；本机依赖根
    ├── skia/            # 源码 + out/Release
    └── qt/              # 若不用 vcpkg 的 Qt
```

`external/` 与 `build/` 均在 `.gitignore` 中。仓库只保存**如何得到依赖**的脚本与文档，不保存依赖本体。

## 3. Skia（源码构建）

D3D12 后端需要 `skia_use_direct3d=true`，官方预编译包不提供，因此必须源码构建。

```powershell
# 一次性：拉取 depot_tools 并加入 PATH
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git D:\tools\depot_tools

# 拉取 Skia
git clone https://skia.googlesource.com/skia D:\Repos\external\skia
cd D:\Repos\external\skia
python tools/git-sync-deps
```

```powershell
# 生成并构建（Debug 同理，换 out\Debug）
bin\gn gen out\Release --args="is_debug=false is_official_build=true skia_use_direct3d=true skia_use_gl=false skia_use_vulkan=false skia_enable_ganesh=true skia_enable_graphite=false skia_use_system_expat=false skia_use_system_freetype2=false"
ninja -C out\Release skia
```

**【待验证】**
1. `skia_use_direct3d` 在当前 Skia 版本的实际开关名与依赖项。
2. 产物名与形态（`skia.lib` 静态 vs `skia.dll` 动态），以及是否需要 `skia_use_icu`/`skia_use_libpng` 等额外开关。
3. `GrDirectContext::MakeDirect3D` 与 `GrD3DBackendContext` 的当前签名。
4. 是否必须同时启用 `skia_use_dawn`。

以上任一项与原假设不符，**先改本文与 [ADR-0006](adr/0006-cmake-vcpkg-source-built-skia.md)，再改构建脚本**。

CMake 侧通过 `-DSKIA_DIR=D:/Repos/external/skia` 引入，且只链接构建产物目录，不改 Skia 源码。

## 4. FFmpeg

通过 vcpkg 获取：`vcpkg.json` 中声明 `ffmpeg` 特性（`avcodec`/`avformat`/`swscale`/`avutil`）。

**许可注意**：启用 `x264` 等 GPL 组件会使整体变为 GPL。本项目为学习用途、不发布二进制，默认可开 GPL 组件；若将来要分发，需要重新评估并写 ADR。

**【待验证】** vcpkg 的 ffmpeg 端口在本机 triple（`x64-windows`）下的默认特性与 DLL 拷贝行为。

## 5. Qt

两种方式二选一，团队内保持一致：

- **A（推荐，快）**：vcpkg `qtbase`，manifest 模式自动带出。
- **B（稳定）**：官方在线安装器装 Qt 6.8 LTS MSVC2022 x64，CMake 里用 `CMAKE_PREFIX_PATH` 指向。

选 B 时必须在 `CMakePresets.json` 里写死 `CMAKE_PREFIX_PATH` 的**相对占位**，本机实际路径放 `CMakeUserPresets.json`（gitignore）。

## 6. 首次构建

```powershell
git clone git@github.com:XiaoliZhouCN/chroma-cms.git
cd chroma-cms
git checkout develop

cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
```

## 7. 日常命令

| 目的 | 命令 |
|---|---|
| 构建 | `cmake --build --preset windows-x64-debug` |
| 测试 | `ctest --preset windows-x64-debug --output-on-failure` |
| 文档校验 | `scripts\check-docs.cmd` |
| 生成色彩常量 | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/gen-color-constants.ps1`（脚本待实现） |
| 格式化 | `cmake --build --preset windows-x64-debug --target format` |

## 8. 常见问题

| 症状 | 原因 | 处置 |
|---|---|---|
| `GrDirectContext::MakeDirect3D` 链接失败 | Skia 未开 D3D12 后端 | 回 §3 检查 GN args，确认产物带 d3d 符号 |
| 运行时报 `DXGI_ERROR_UNSUPPORTED` | 交换链格式/色彩空间不被支持 | 查 [ADR-0007](adr/0007-swapchain-format-and-color-space.md) 的降级策略 |
| Qt 找不到平台插件 | DLL 未拷到输出目录 | 检查 CMake 的 `windeployqt` 步骤 |
| 中文路径构建失败 | 工具链对非 ASCII 路径敏感 | 仓库与 `external/` 一律放 ASCII 路径 |
| Skia 构建 OOM | 并行度过高 | `ninja -j 4` |
