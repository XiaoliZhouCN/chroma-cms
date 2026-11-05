---
status: active
owner: XiaoliZhouCN
updated: 2026-11-05
---

# 构建与环境

> 本文记录**本机实测过**的环境与命令。未实测的内容标 **【待验证】**，不得当成事实引用。

## 1. 实测环境（2026-11-05）

| 组件 | 实测值 | 说明 |
|---|---|---|
| OS | Windows（Build 26300 系列） | x64 |
| Visual Studio | **2026 Professional 18.8** | 工具集 MSVC **19.51**（14.51.36231） |
| Windows SDK | 10.0.26100 / 10.0.28000 | 构建实际选用 26100 系列 |
| CMake | **4.2.1** | 生成器 `Visual Studio 18 2026` |
| Ninja | QtCreator 自带 | 当前 preset 未使用（用 VS 生成器） |
| Qt | **6.11.1 msvc2022_64** | `D:\Applications\QtCreator\6.11.1\msvc2022_64` |
| FFmpeg | **n9.0.1 BtbN win64-lgpl-shared** | `external/ffmpeg`，见 §3.1 |
| GPU | **Intel UHD Graphics**，D3D12 特性等级 **12_1** | 集显，共享内存；够用 |
| D3D12 调试层 | **可用**（`d3d12SDKLayers.dll` 存在） | Debug 构建自动启用 |
| HLSL 编译器 | `fxc.exe` / `dxc.exe` 均在 SDK 10.0.26100 x64 下 | 当前用运行时 `D3DCompile` |
| vcpkg | 未安装 | 暂不需要；多平台后再评估 |
| Python | 3.14.2 | **对 Skia/depot_tools 偏新**，见 §3.3 |

## 2. 目录布局

```
<repo>/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                 QtBootstrap · FindFFmpeg · CompilerWarnings
├── external/              外部预编译依赖（gitignore，用 tools/ 复原）
│   └── ffmpeg/            include/ lib/ bin/ LICENSE.txt
├── src/
│   ├── core/              无平台依赖的基础层（chroma_core，INTERFACE）
│   ├── gfx/               D3D12 公共头（chroma_gfx，INTERFACE）
│   ├── media/             FFmpeg 解码（chroma_media）
│   ├── render/            IRenderer + D3D12Renderer（chroma_render）
│   └── app/               Qt 外壳 + 原生表面宿主
├── shaders/               image.hlsl（随 exe 复制）
├── assets/test-images/    PTMR 测试图
├── tools/                 gen_ptmr.py · fetch-ffmpeg.ps1
├── tests/                 unit/（无 GUI）· 集成 ctest
└── build/                 生成物（gitignore）
```

## 3. 外部依赖

原则：**依赖本体不入库，入库的是"如何得到依赖"**。每个外部依赖都必须有一个可重复执行的获取脚本。

### 3.1 FFmpeg（图像 + 视频解码）

见 [ADR-0008](adr/0008-unified-ffmpeg-decoding.md)。

| 项 | 值 |
|---|---|
| 版本 | FFmpeg **n9.0.1**（libavcodec 63 / libavformat 63 / libavutil 61 / swscale 10 / swresample 7） |
| 变体 | BtbN `win64-lgpl-shared`（**LGPL v3**，动态链接） |
| 位置 | `external/ffmpeg`（gitignore，约 116 MB） |
| 获取 | `pwsh tools/fetch-ffmpeg.ps1` |
| CMake | `cmake/FindFFmpeg.cmake` → `FFmpeg::FFmpeg`，自动在 `external/ffmpeg` 下查找 |
| 运行时 | `src/app/CMakeLists.txt` 的 POST_BUILD 把 `external/ffmpeg/bin/*.dll` 拷到输出目录 |

**必须随 exe 发布的 DLL**（少一个就是 `0xC0000135`）：

```
avcodec-63.dll  avformat-63.dll  avutil-61.dll  swscale-10.dll  swresample-7.dll
```

注意：`avcodec-63.dll` 依赖 `swresample-7.dll`，即使我们没直接调用 swresample。

**头文件必须包 `extern "C"`**：FFmpeg 的头不自己声明 C 链接，不包会得到 22 个 `LNK2019`
（符号按 C++ 名字修饰）。见 §7。

许可：动态链接 + 随包提供 `LICENSE.txt` + 可替换，是本项目的 LGPL 合规前提；
详见 [ADR-0008](adr/0008-unified-ffmpeg-decoding.md)「需要跟进」。

### 3.2 Qt

`cmake/QtBootstrap.cmake` 依次尝试：

1. 显式 `-DQT_ROOT=<Qt msvc*_64 目录>`；
2. 扫描 `C:/Qt/6.*/msvc*_64`、`D:/Qt/6.*/msvc*_64`、`D:/Applications/QtCreator/6.*/msvc*_64`，取**版本号最大**的一个。

本机命中第 2 条。**本机绝对路径不写进 `CMakePresets.json`。**

Qt 6.11.1 是 MSVC 2022（v143）构建的，用 VS2026（v145）工具集链接**实测可用**（MSVC v14x ABI 兼容）。

### 3.3 Skia（M3 才需要）

不进入 M0–M2 关键路径（`CHROMA_WITH_SKIA=OFF` 默认关闭）。

```powershell
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git D:\tools\depot_tools
git clone https://skia.googlesource.com/skia D:\Repos\external\skia
cd D:\Repos\external\skia
python tools/git-sync-deps
bin\gn gen out\Release --args="is_debug=false is_official_build=true skia_use_direct3d=true skia_use_gl=false skia_use_vulkan=false skia_enable_ganesh=true"
ninja -C out\Release skia
```

**【待验证】**：`skia_use_direct3d` 的实际开关名与依赖、产物名与形态、
`GrDirectContext::MakeDirect3D` / `GrD3DBackendContext` 的签名、**Python 3.14 是否被支持**（大概率要单独装 3.11/3.12）。

磁盘注意：C: 仅剩 ~12 GB，Skia 构建（10 GB+）必须放 D:。

## 4. 首次构建

```powershell
git clone git@github-shirley:XiaoliZhouCN/chroma-cms.git
cd chroma-cms
git checkout develop

pwsh tools/fetch-ffmpeg.ps1        # 一次性：拉取 FFmpeg 到 external/
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
```

生成物：`build/windows-x64-debug/Debug/ChromaCMS.exe`（Qt / FFmpeg DLL 与 `shaders/` 已就位）。

## 5. 日常命令

| 目的 | 命令 |
|---|---|
| 配置 | `cmake --preset windows-x64-debug` |
| 构建 | `cmake --build --preset windows-x64-debug` |
| 测试 | `ctest --preset windows-x64-debug --output-on-failure` |
| 文档门禁 | `scripts\check-docs.cmd` |
| 打开工程 | `build\windows-x64-debug\ChromaCMS.sln` |
| 生成测试图 | `python tools/gen_ptmr.py --out assets/test-images/... --cicp-matrix 0 --preview` |

## 6. 已知限制

- **M0–M1 保留控制台窗口**，日志直接可见；M6 前改为 WIN32 子系统。
- `CompilerWarnings.cmake` **未启用 `/WX`**：Qt 6.11 头文件在 `/W4` 下仍有告警。
- `app.selftest` 需要桌面会话（Windows 本机），CI 上不跑。
- **HLSL 当前是运行时编译**：`D3DCompileFromFile` 读 `shaders/image.hlsl`。M2 接入色彩管线时评估改离线产物。
- `CHROMA_SOURCE_DIR` 编译期写死源码路径，仅用于定位默认测试图；换机器需重新 configure。
- 平台：目前只有 Windows；macOS 的 Metal 路径见 [ADR-0009](adr/0009-multi-platform-support.md)，尚未实现。

## 7. 踩过的坑

| 症状 | 原因 | 处置 |
|---|---|---|
| `CreateSwapChainForHwnd` 返回 `0x887A0001`（DXGI_ERROR_INVALID_CALL） | **D3D12 第一个参数要传 `ID3D12CommandQueue`，不是 `ID3D12Device`** | 先建 DIRECT 队列再传 |
| FFmpeg 符号全部 `LNK2019`，且是 C++ 修饰名 | FFmpeg 头文件**不自己声明 `extern "C"`** | 所有 `#include <libav*>` 包进 `extern "C" { }` |
| 启动即 `0xC0000135`（DLL 找不到） | `avcodec-63.dll` 依赖 `swresample-7.dll`，没拷 | POST_BUILD 拷全 `bin/*.dll` |
| `cmake --build` 报 `AutoMoc subprocess error … libuv spawn failed` | Agent 沙箱禁止管道式子进程 | Agent 环境需放宽沙箱；本机终端无此问题 |
| MSVC 报 `文本后缀"鍚"无效` | 源文件含中文但未加 `/utf-8` | 所有 C++ 目标统一 `/utf-8` |
| `std::wprintf` 不是 `std` 成员 | `<cstdio>` 不提供宽字符版本 | 用全局 `wprintf` 并 `#include <cwchar>` |
| 进程启动即 `0xC0000005`，控制台无输出 | `std::shared_ptr` 被 `std::move` 后继续解引用 | move 后只用目标对象 |
| 16-bit PNG 未被识别为「10-bit 码 << 6」 | WIC/FFmpeg 的 alpha 是 `0xFFFF`，低 6 位非 0 | 判定与重映射只处理 RGB |
| 中文路径构建失败 | 工具链对非 ASCII 路径敏感 | 仓库与依赖一律放 ASCII 路径 |
