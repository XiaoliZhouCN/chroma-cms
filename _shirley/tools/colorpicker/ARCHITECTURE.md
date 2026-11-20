# Chroma ColorPicker 架构规范（Windows / macOS）

> 状态：2026-11-20
> 本文是 colorpicker 的**框架规范**。Windows 已按本规范完成重构；macOS 只提供骨架占位，ScreenCaptureKit 实现待 macOS 上完成。

## 1. 目标

1. 用一套 Qt6 Widgets UI 同时服务 Windows 和 macOS；
2. 捕获、显示器枚举、HDR 预览各自封装为平台后端；
3. core / UI 不包含任何 `windows.h`、DXGI、D3D11、AppKit、ScreenCaptureKit 类型；
4. Windows 行为、CLI JSON、GUI 读数与重构前保持一致；
5. macOS 先能编译、能枚举显示器、能显示 UI；捕获/HDR 预览留 TODO。

非目标（当前阶段）：

- macOS 的完整 ScreenCaptureKit 实现；
- 跨平台绝对色彩/亮度换算（本轮只保留捕获原始值与格式元数据）；
- 打包、签名、公证、App Store。

## 2. 分层

```
Qt UI（src/ui）
  MainWindow / MagnifierWidget / HdrPreview
        │ 只依赖 core 接口
        ▼
core（src/core，纯 C++，无 Qt、无平台 API）
  Types / CaptureSession / DisplayProbe / Platform
        │ 平台工厂
        ├──────────────┬───────────────┐
        ▼              ▼               ▼
Windows backend    macOS backend    macOS skeleton
D3D11/Duplication  ScreenCaptureKit  TODO (见 §9)
WGC / scRGB 预览   Metal EDR 预览
```

## 3. 目录与职责

```text
src/
  core/
    Types.hpp            # MonitorInfo / DisplayInfo / CaptureFormat / ColorSample
    CaptureSession.hpp   # ICaptureSession：初始化、抓帧、sampleRect、display()
    DisplayProbe.hpp     # IDisplayProbe：enumerate / monitorAt / cursorPosition
    Platform.hpp         # prepareProcessForDump / createDisplayProbe / createCaptureSession
  ui/
    MainWindow.*         # Qt 窗口、鼠标跟踪、读数、16×16 放大镜
    HdrPreview.hpp       # HdrPreview 接口 + createHdrPreview 工厂声明
  app/
    main.cpp             # --list / --dump / GUI 入口；无平台头文件
  platform/
    windows/
      WindowsPlatform.cpp / WindowsDisplayProbe.* / WindowsCaptureSession.*
      WindowsHdrPreview.* / HdrSwatch.*
      Capture.* / WgcCapture.* / WindowsConversions.*
    macos/
      MacPlatform.mm / MacDisplayProbe.* / MacCaptureSession.* / MacHdrPreview.cpp
```

规则：

- `src/core`、`src/ui`、`src/app` 只能包含 core/UI 头；**不得包含** `windows.h`、`d3d11.h`、`dxgi*.h`、`AppKit`、`ScreenCaptureKit`。
- `src/platform/windows` 可以使用 Win32/D3D11/C++/WinRT。
- `src/platform/macos` 使用 Objective-C++（`.mm`）访问 CoreGraphics/AppKit/ScreenCaptureKit。
- 平台差异只通过 `createDisplayProbe()`、`createCaptureSession()`、`createHdrPreview()` 三个工厂进入。
- 不允许 UI 里写 `#ifdef _WIN32` 分支；需要平台行为时扩展接口，而不是在 UI 里 include 平台头。

## 4. 核心类型语义

### MonitorInfo

- `bounds`：**全局物理像素**；Windows 直接来自 `RECT`，macOS 来自 `CGDisplayBounds`。
- `nativeHandle`：Windows 为 `HMONITOR`，macOS 为 `CGDirectDisplayID`；只在平台后端解释。
- `id`：稳定可读标识（Windows `\\.\DISPLAY1`；macOS `display:<CGDirectDisplayID>`）。

### DisplayInfo

字段统一为平台无关的普通类型：

- `hdr`、`advancedColor*`、`bitsPerColor`、`colorSpace`；
- `sdrWhiteLevelRaw` / `sdrWhiteNits`：Windows 提供；macOS 骨架为 `nullopt`；
- `maxLuminance` 等为 `optional`，平台不提供时 UI 显示 `n/a`；
- `wireFormat`：UI 用短文本（由平台填）；`colorSpace`：CLI/JSON 的完整描述。

### CaptureFormat

- `encoding`：`Fp16` / `Bgra8` / `R10G10B10A2` / `YCbCr10`（macOS HDR 预留）；
- `name`：用于 UI 和 JSON 的格式名；
- `linear`：该格式的值是否线性。

### ColorSample

- `rgba` 是**捕获空间原始分量**，不是统一线性空间；
- 解释方式由 `format.encoding` + 平台语义决定：
  - Windows FP16：scRGB linear，1.0 = 80 nits；
  - Windows BGRA8：sRGB 编码值；
  - macOS：由 ScreenCaptureKit 输出格式决定（当前骨架未实现）。

## 5. 平台能力矩阵

| 能力 | Windows（已完成） | macOS（骨架/TODO） |
|---|---|---|
| 显示器枚举 | `EnumDisplayMonitors` + `GetMonitorInfo` | `CGGetOnlineDisplayList`（已占位） |
| 光标坐标 | `GetCursorPos` | `CGEventGetLocation`（Retina/多屏换算 TODO） |
| SDR 捕获 | Duplication BGRA8 | ScreenCaptureKit `32BGRA`（TODO） |
| HDR 捕获 | Duplication FP16 / WGC FP16（scRGB） | ScreenCaptureKit macOS 15 HDR（TODO） |
| HDR 预览 | D3D11 scRGB FP16 swapchain | CAMetalLayer EDR（TODO） |
| SDR 白点 | `DISPLAYCONFIG_SDR_WHITE_LEVEL` | 无等价字段，UI 显示 n/a |
| 权限 | WGC 可能需系统 picker | 屏幕录制 TCC |

## 6. 线程与生命周期

- UI 定时器（50 ms）在 Qt 主线程调用 `captureFrame()` / `sampleRect()`；当前 Windows 实现是同步的。
- macOS ScreenCaptureKit 是异步流；macOS 实现应在后台队列接收帧，写入"最新帧"槽，UI 线程只读最新帧（后续实现时补充锁）。
- `ICaptureSession::initialize()` 可重复调用；Windows 实现内部先 `release()`。
- 应用失去前台时 UI 暂停采集/预览，回到前台后刷新显示元数据并重建捕获（避免 HDR 设置切换导致会话失效）。
- 显示器切换：Windows WGC 选择后**锁定显示器**，避免反复弹 picker；Duplication 后端跟随光标。

## 7. 构建规则

- `CMakeLists.txt` 统一创建 `colorpicker` target；按平台追加 platform sources。
- Windows：
  - 链接 `d3d11 dxgi user32 gdi32`；
  - WGC 仅在 MSVC + Windows SDK C++/WinRT 存在时开启（`CHROMA_HAS_WGC`）；
  - MinGW 保持 Duplication/SDR 回退。
- macOS：
  - `enable_language(OBJCXX)`；
  - 链接 `AppKit CoreGraphics ApplicationServices`（ScreenCaptureKit/Metal 在实现阶段再加）；
  - `MACOSX_BUNDLE TRUE`。
- 构建目录不应提交；`.gitignore` 忽略 `build*/`。

## 8. 色彩语义约定（重要）

- Windows 的 "CCCS / scRGB / 1.0 = 80 nits" 是 **Windows 平台语义**，不是跨平台通用概念。
- macOS 没有 CCCS，也没有 `SDRWhiteLevel`。macOS 实现时要：
  - SDR：报告实际 `CGColorSpace`（sRGB / Display P3）与 SDR display 语义；
  - HDR：使用 ScreenCaptureKit HDR preset（macOS 15+），输出 10-bit YCbCr + Display P3 PQ/HLG，做 YCbCr→RGB 与 PQ/HLG→nits；
  - UI 的 `CCCS色域` 行在 macOS 应显示 `捕获色域` 的平台实际值，而不是假装 scRGB。
- 不要把 EDR 预览值当作测量值；测量值始终来自 capture backend。

## 9. macOS 骨架与下一步

当前 macOS 文件：

- `MacDisplayProbe.mm`：CGDisplay 列表 + 基本坐标（Retina/多屏待校验）；
- `MacCaptureSession.mm`：返回 false 的占位，打印 TODO；
- `MacHdrPreview.cpp`：Qt SDR 占位色块；
- `MacPlatform.mm`：工厂。

macOS 实现顺序：

1. ScreenCaptureKit SDR：`SCShareableContent` + `SCContentFilter` + `SCStream`（32BGRA），坐标系/Retina 校验；
2. TCC 屏幕录制权限：`CGPreflightScreenCaptureAccess` / `CGRequestScreenCaptureAccess` + UI 提示；
3. HDR（macOS 15+）：`.captureHDRStreamLocalDisplay` / `.captureHDRStreamCanonicalDisplay`；
   `SCScreenshotManager` 用于 `--dump` 单点取值；
4. 10-bit YCbCr → RGB、PQ/HLG → nits；
5. Metal EDR 预览：`CAMetalLayer.wantsExtendedDynamicRangeContent` + 扩展线性 Display P3；
6. 实测校准与 UI 文案。

## 10. 验证

Windows 当前回归基准：

```powershell
cd _shirley\tools\colorpicker
cmake -S . -B build_refactor -DCMAKE_PREFIX_PATH="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build build_refactor --config Debug
.\build_refactor\Debug\colorpicker.exe --list
.\build_refactor\Debug\colorpicker.exe --dump --x 3000 --y 500
.\build_refactor\Debug\colorpicker.exe
```

验收：

- Debug/Release 构建通过；
- 重构前后同一坐标 `--dump` JSON 完全一致；
- GUI 读数、16×16 放大镜、HDR 色块、Esc 退出正常；
- 无平台头文件泄漏到 core/ui/app（`grep` 检查）。

macOS 验收待 macOS 实机执行；在 Windows 上只保证骨架文件存在、CMake 分支清晰、无法被 Windows 构建误编。

## 11. 变更流程

1. 改 core 接口必须同步更新本文和对应平台实现；
2. 新增平台能力先补接口/类型，再改 UI；
3. 不在 UI 里写平台条件编译；
4. 平台实现失败必须回退到可用路径，并在 stderr 记录原因；
5. 完成一个阶段更新 `README.md` 与 `VERIFICATION.md`。
