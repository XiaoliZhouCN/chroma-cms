---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 构建与环境

> 本文记录**本机实测过**的环境与命令。未实测的内容标 **【待验证】**，不得当成事实引用。

## 1. 实测环境（2026-11-04）

| 组件 | 实测值 | 说明 |
|---|---|---|
| OS | Windows（Build 26300 系列） | x64 |
| Visual Studio | **2026 Professional 18.8** | 工具集 MSVC **19.51**（14.51.36231） |
| Windows SDK | 10.0.26100 / 10.0.28000 | 构建实际选用 26100 系列 |
| CMake | **4.2.1** | 生成器 `Visual Studio 18 2026` |
| Ninja | QtCreator 自带 | 当前 preset 未使用（用 VS 生成器） |
| Qt | **6.11.1 msvc2022_64** | `D:\Applications\QtCreator\6.11.1\msvc2022_64` |
| GPU | **Intel UHD Graphics**，D3D12 特性等级 **12_1** | 集显，共享内存；够用 |
| D3D12 调试层 | **可用**（`d3d12SDKLayers.dll` 存在） | 启动时应启用 |
| HLSL 编译器 | `fxc.exe` / `dxc.exe` 均在 SDK 10.0.26100 x64 下 | 离线编译可用 |
| vcpkg | **未安装** | M0–M3 不需要；M4 引入 FFmpeg 时再定 |
| Python | 3.14.2 | **对 Skia/depot_tools 偏新**，见 §4 |

## 2. 目录布局

```
<repo>/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                 QtBootstrap.cmake · CompilerWarnings.cmake
├── src/
│   ├── core/              无 Qt 依赖的基础层（chroma_core，INTERFACE）
│   └── app/               Qt 外壳 + 原生表面宿主
├── tests/                 unit/（无 GUI）· 集成 ctest
└── build/                 生成物（gitignore）
```

## 3. Qt 的定位方式

`cmake/QtBootstrap.cmake` 会依次尝试：

1. 显式传入的 `-DQT_ROOT=<Qt msvc*_64 目录>`；
2. 扫描 `C:/Qt/6.*/msvc*_64`、`D:/Qt/6.*/msvc*_64`、`D:/Applications/QtCreator/6.*/msvc*_64`，取**版本号最大**的一个。

本机命中第 2 条，解析到 `D:\Applications\QtCreator\6.11.1\msvc2022_64`。
**本机绝对路径不写进 `CMakePresets.json`**，避免换机即坏。

Qt 6.11.1 是用 MSVC 2022（v143）构建的，用 VS2026（v145）工具集链接**实测可用**（MSVC v14x ABI 兼容）。

## 4. Skia（M3 才需要）

已决定**不进入 M0–M2 关键路径**（`CHROMA_WITH_SKIA=OFF` 默认关闭）。开启时需要：

```powershell
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git D:\tools\depot_tools
git clone https://skia.googlesource.com/skia D:\Repos\external\skia
cd D:\Repos\external\skia
python tools/git-sync-deps
bin\gn gen out\Release --args="is_debug=false is_official_build=true skia_use_direct3d=true skia_use_gl=false skia_use_vulkan=false skia_enable_ganesh=true"
ninja -C out\Release skia
```

**【待验证】**
1. `skia_use_direct3d` 在当前 Skia 版本的实际开关名与依赖项。
2. 产物名与形态（`skia.lib` 静态 vs `skia.dll` 动态）。
3. `GrDirectContext::MakeDirect3D` 与 `GrD3DBackendContext` 的当前签名。
4. **Python 3.14 是否被 depot_tools / Skia 脚本支持**——大概率需要单独装 3.11/3.12。

以上任一与本假设不符，**先改本文与 ADR-0006，再改构建脚本**。

磁盘注意：C: 仅剩 ~12 GB，Skia 构建（10 GB+）必须放在 D:（仓库所在盘，余量 300 GB+）。

## 5. 首次构建（当前可用）

```powershell
git clone git@github-shirley:XiaoliZhouCN/chroma-cms.git
cd chroma-cms
git checkout develop

cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
```

生成物：`build/windows-x64-debug/Debug/ChromaCMS.exe`（windeployqt 已把 Qt DLL 拷到同目录）。

## 6. 日常命令

| 目的 | 命令 |
|---|---|
| 配置 | `cmake --preset windows-x64-debug` |
| 构建 | `cmake --build --preset windows-x64-debug` |
| 测试 | `ctest --preset windows-x64-debug --output-on-failure` |
| 文档门禁 | `scripts\check-docs.cmd` |
| 生成色彩常量 | 待实现（M2） |
| 打开工程 | `build\windows-x64-debug\ChromaCMS.sln` |

## 7. 已知限制

- **M0–M1 保留控制台窗口**，日志直接可见；M6 前改为 WIN32 子系统。
- 当前 `CompilerWarnings.cmake` **未启用 `/WX`**：Qt 6.11 头文件在 `/W4` 下仍有告警，待收敛后再开。
- 测试里的 `app.selftest` 需要桌面会话（Windows 本机），CI 上不跑。
- **HLSL 目前是运行时编译**：`D3D12Renderer` 用 `D3DCompileFromFile` 读 `shaders/image.hlsl`（随 exe 复制到 `shaders/`）。
  好处是改着色器不用重新构建；代价是启动多花几十毫秒、发布包带源码。M2 接入色彩管线时评估改为离线 `fxc`/`dxc` 产物。
- `CHROMA_SOURCE_DIR` 编译期写死源码路径，仅用于定位默认测试图；换机器需重新 configure。

## 8. 踩过的坑

| 症状 | 原因 | 处置 |
|---|---|---|
| `CreateSwapChainForHwnd` 返回 `0x887A0001`（DXGI_ERROR_INVALID_CALL） | **D3D12 下第一个参数要传命令队列 `ID3D12CommandQueue`，不是 `ID3D12Device`**（D3D11 才传 device） | 先建 DIRECT 队列，再传队列指针 |
| `cmake --build` 报 `AutoMoc subprocess error ... libuv process spawn failed: operation not permitted` | Agent 沙箱禁止以管道方式派生子进程，而 AUTOMOC 走 libuv | 在 Agent 环境构建需放宽沙箱；本机普通终端无此问题 |
| MSVC 报 `文本后缀"鍚"无效` | 源文件含中文但未加 `/utf-8` | 所有 C++ 目标统一加 `/utf-8` |
| `std::wprintf` 不是 `std` 成员（MSVC） | `<cstdio>` 不提供宽字符版本 | 用全局 `wprintf` 并 `#include <cwchar>` |
| 进程启动即 `0xC0000005`，控制台无任何输出 | `std::shared_ptr` 被 `std::move` 后继续解引用（`loaded->width`） | move 之后只用目标对象 |
| 16-bit PNG 未被识别为「10-bit 码 << 6」 | alpha 由 WIC 补成 `0xFFFF`，低 6 位非 0，使「所有通道低 6 位为 0」永假 | 判定与重映射都只处理 RGB 通道 |
| 中文路径构建失败 | 工具链对非 ASCII 路径敏感 | 仓库与依赖一律放 ASCII 路径 |
