# Chroma ColorPicker · 验证记录与换机测试指南

> 目的：记录当前问题、已修复项、HDR 验证方法，方便换一台 Windows 电脑（同一台 VG32AQL1A）复测。
> 更新时间：2026-11-19

---

## 1. 当前状态（2026-11-19）

### 测试环境

| 项 | 值 |
|---|---|
| 系统 | Windows 10 Pro build 26300.9974（实际为 Windows 11 内核；以 `winver` 为准） |
| 会话 | `SESSIONNAME=Console`，非远程会话 |
| GPU | Intel(R) UHD Graphics，驱动 31.0.101.4502 |
| 外接 HDR 屏 | AUS VG32AQL1A，工具识别为 `\\.\DISPLAY1` |
| 内屏 | BOE 1920×1200，dxdiag 报告 **HDR Not Supported**（不是 HDR 屏） |
| 当前外接屏模式 | 2560×1440 @ 59Hz（dxdiag） |

### 已确认的事实

dxdiag（`dxdiag /t report.txt`）对外接屏报告：

```
Card name: Intel(R) UHD Graphics
Current Mode: 2560 x 1440 (32 bit) (59Hz)
HDR Support: Supported
Display Color Space: DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
Monitor Capabilities: HDR Supported (BT2020RGB BT2020YCC Eotf2084Supported)
Advanced Color: AdvancedColorSupported AdvancedColorEnabled
Active Color Mode: DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR
```

**结论：外接屏 HDR 已真正开启（HDR10/PQ 输出）。**

工具 `--list` 修复后输出：

```
DISPLAY1 VG32AQL1A hdr=on
color_space=RGB_FULL_G2084_NONE_P2020 (HDR10/PQ)
advanced_supported=on advanced_enabled=on
max_lum=603.7
```

### 已修复的 bug（commit `6b7ecf2`）

重构 `readOutputDesc1()` 时漏了 `IDXGIOutput6` 的 QI：

```cpp
selectedOutput.As(&m_output5);
selectedOutput.As(&m_output6);   // ← 之前漏掉，导致 GetDesc1 从未调用
readOutputDesc1();
```

修复前：HDR 屏被误报为 SDR（`hdr=off`、`G22_P709`、`max_lum=0`）。
修复后：正确报告 `hdr=on`、`G2084_NONE_P2020`、`max_lum=603.7`。

### 当前仍然存在的问题

**HDR 已开启，但 `IDXGIOutput5::DuplicateOutput1` 仍返回 `B8G8R8A8_UNORM`（8-bit），不是 `R16G16B16A16_FLOAT`。**

实测 `--dump`：

```json
"capture_format": "B8G8R8A8_UNORM",
"hdr": true,
"r": 1.0, "g": 1.0, "b": 1.0
```

白色读到 1.0 是 HDR 数值被截断后的结果，**不能作为 HDR 测量值**。
尝试过优先请求 `R10G10B10A2_UNORM` / FP16 / 混合格式列表，该 Intel 驱动仍退回 BGRA8。

**下一步方案**：补 `Windows.Graphics.Capture` + `R16G16B16A16_FLOAT` 后端（微软官方 HDR 捕获路径），
用 `IGraphicsCaptureItemInterop::CreateForMonitor` 直接捕获显示器，输出 scRGB 线性值。

---

## 2. 验证方法

### 2.1 一键验证（推荐）

在工具目录下运行：

```powershell
cd _shirley\tools\colorpicker
powershell -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1
```

脚本会收集并保存一份报告（默认 `verification-YYYYMMDD-HHmmss.txt`）：

- 会话类型（Console / RDP）与系统版本；
- GPU 名称/驱动/当前模式；
- dxdiag 中 `HDR Support`、`Display Color Space`、`Active Color Mode` 等行；
- `colorpicker --list` 的完整输出；
- 对每个显示器中心点执行 `--dump` 的结果。

### 2.2 手动验证

```powershell
cd _shirley\tools\colorpicker

# 1) 看所有显示器状态
.\build\Debug\colorpicker.exe --list

# 2) 在 HDR 屏的坐标范围内采样（注意 rect 可能包含负坐标）
.\build\Debug\colorpicker.exe --dump --x 3000 --y 500
```

### 2.3 验收标准

| 観察项 | 期望 |
|---|---|
| `--list` 的 HDR 屏 | `hdr=on`、`color_space=...G2084_NONE_P2020`、`max_lum>0` |
| `--dump` 的 `hdr` | `true` |
| `--dump` 的 `capture_format` | **`R16G16B16A16_FLOAT`**（HDR 可用路径） |
| dxdiag | `HDR Support: Supported`、`Active Color Mode: ...HDR` |

如果 `hdr=true` 但 `capture_format=B8G8R8A8_UNORM`：
→ 说明 Windows HDR 已开，但该 GPU/驱动的 Desktop Duplication 不提供 HDR 像素；
→ 需要 WGC 后端；请附上 `verify.ps1` 生成的报告。

如果 `--list` 的 HDR 屏 `hdr=off`：
→ 先检查 Windows 设置 → 系统 → 显示 → 该显示器"使用 HDR"是否开启；
→ 确认不是远程会话；
→ 检查驱动输出色彩格式（10-bit/RGB Full）与线材/接口。

---

## 3. 换到新 Windows 电脑的步骤

### 3.1 准备

1. 新电脑安装 **Visual Studio**（含"使用 C++ 的桌面开发"工作负载）和 **Windows SDK**；
2. 安装 **Qt 6（msvc2022_64）**；
3. 把仓库同步到新电脑（至少包含 `_shirley/tools/colorpicker`）；
4. 把 VG32AQL1A 接到新电脑，在 Windows 设置里打开该显示器的 **"使用 HDR"**；
5. 确认显示模式与线材支持 HDR（必要时降低刷新率/换 DP 线）。

### 3.2 构建

```powershell
cd _shirley\tools\colorpicker
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_PREFIX_PATH="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build build --config Debug
```

如果新电脑没有 VS 2026 生成器，把 `-G` 改为本机版本，例如：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="<Qt6 msvc 路径>"
```

### 3.3 验证

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1
```

把生成的 `verification-*.txt` 发回来即可。重点看：

1. `hdr=on` 还是 `hdr=off`；
2. `capture_format` 是 `R16G16B16A16_FLOAT` 还是 `B8G8R8A8_UNORM`；
3. 同一张测试图在 Chrome 与 ChromaCMS 中的取值。

### 3.4 输出示例（本机记录）

```
monitor=\\.\DISPLAY1 rect=(2400,-300)-(5600,1500) init=ok format=DXGI_FORMAT_0 hdr=on
 color_space=RGB_FULL_G2084_NONE_P2020 (HDR10/PQ) advanced_supported=on
 advanced_enabled=on wide_enforced=off force_disabled=off encoding=RGB
 cfg_bits=8 desc1_bits=8 sdr_white=204.0 max_lum=603.7 name=VG32AQL1A
 adapter=Intel(R) UHD Graphics
```

`--dump --x 3000 --y 500`：

```json
{
  "capture_format": "B8G8R8A8_UNORM",
  "hdr": true,
  "color_space": "RGB_FULL_G2084_NONE_P2020 (HDR10/PQ)",
  "max_luminance_nits": 603.7,
  "sdr_white_nits": 204.0,
  "config_bits_per_color": 8,
  "r": 1.0, "g": 1.0, "b": 1.0
}
```

---

## 4. 备注

- `--list` 中 HDR 屏偶尔显示 `format=DXGI_FORMAT_0`，表示该次初始化没有在超时内拿到首帧，不影响 HDR 状态判断；`--dump` 会重试取帧。
- 内屏 BOE 1920×1200 在本机 dxdiag 中是 `HDR Not Supported`，不要拿它做 HDR 测试。
- 换机后如果两块屏的编号/坐标不同，以 `--list` 的 `rect=` 为准；`--dump` 的 `--x/--y` 是虚拟桌面物理坐标，可为负数。
- 工具只读取 framebuffer 合成后的数值，不测量面板物理亮度；没有硬件校色仪时，验证结论只能到"合成链路数值是否一致"。

---

## 5. 换机复测记录（2026-11-19，第二台 Windows 机器）

> **结论：HDR 仍拿不到。** Windows HDR 已开启，但 `--dump` 只能拿到 `B8G8R8A8_UNORM`（8-bit），
> 白色锁死在 1.0。命中 §2.3 的判定分支：**需要 WGC 后端**。

### 5.1 复测环境（与 §1 基线不同）

| 项 | §1 基线 | 本次复测机 |
|---|---|---|
| OS | Windows 10 Pro build 26300.9974 | 相同（build 26300.9974，26H2） |
| 会话 | `Console` | `Console`（`TerminalServerSession=False`） |
| GPU | Intel UHD Graphics，驱动 31.0.101.4502 | **Intel UHD Graphics 770**，驱动 **32.0.101.7088** |
| 当前模式 | 2560×1440 @ 59Hz | 2560×1440 @ **144Hz** |
| 输出名 | `\\.\DISPLAY1` | **`\\.\DISPLAY5`**，rect `(0,0)-(2560,1440)` |
| 显示器 | AUS VG32AQL1A | 相同（`name=VG32AQL1A`，DisplayPort External） |
| 其它适配器 | — | NVIDIA RTX 3060（未接显示器）、Microsoft Basic Render Driver |

dxdiag 与工具读数一致：`HDR Support: Supported`、
`Display Color Space: DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`、
`Advanced Color: AdvancedColorSupported AdvancedColorEnabled`、
`Active Color Mode: DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR`。

### 5.2 构建（该机没有 MSVC，改用 MinGW）

复测机没装 Visual Studio，只能用 Qt 安装器自带的 MinGW 套件：

| 组件 | 版本 |
|---|---|
| Qt | 6.12.0 `mingw_64`（`mkspecs/qconfig.pri` 标明由 GCC 15.1.0 编译） |
| 编译器 | MinGW-w64 GCC **15.1.0**（`<Qt>/Tools/mingw1510_64`，必须与 Qt 的 GCC 版本一致） |
| CMake | 3.30.5（`<Qt>/Tools/CMake_64`） |
| 生成器 | `MinGW Makefiles` |

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH="<Qt>/6.12.0/mingw_64" `
  -DCMAKE_CXX_COMPILER="<Qt>/Tools/mingw1510_64/bin/g++.exe" `
  -DCMAKE_MAKE_PROGRAM="<Qt>/Tools/mingw1510_64/bin/mingw32-make.exe"
cmake --build build
```

构建时额外踩到两个坑（详见 §5.5）：`%ws` 格式符崩溃、`AUTOMOC` 在受限环境下失败。

### 5.3 实测结果

`--list`（`format=` 字段取自打开 HDR 窗口**之前**的 6 次运行，均为 FP16；打开之后再测 10 次变成
`DXGI_FORMAT_0`，属 §4 记录的首帧超时现象，HDR 状态判断不受影响）：

```text
monitor=\\.\DISPLAY5 rect=(0,0)-(2560,1440) init=ok format=R16G16B16A16_FLOAT hdr=on
color_space=RGB_FULL_G2084_NONE_P2020 (HDR10/PQ) advanced_supported=on advanced_enabled=on
wide_enforced=off force_disabled=off encoding=RGB cfg_bits=8 desc1_bits=8
sdr_white=200.0 max_lum=603.7 name=VG32AQL1A adapter=Intel(R) UHD Graphics 770
```

> **待查**：`--list` 的 `format=` 报过 FP16（6/6），而 `--dump` 始终报 BGRA8（20+ 次）。
> 两者读的都是纹理的 `GetDesc().Format`，差别只在"取到第几帧"，怀疑复制建立后的**首帧**
> 可能是 FP16(scRGB)、后续帧回落 BGRA8。这是推测，需要给工具加逐帧诊断输出才能确认。
> 对结论没有影响：`--dump`（实际测量路径）稳定拿不到 HDR 值。

`--dump`（5×4 网格 20 点 + 白点重复 5 次，**全部一致**）：

```json
{ "capture_format": "B8G8R8A8_UNORM", "hdr": true, "max_luminance_nits": 603.7 }
```

典型读数（全是 `n/255` 的精确值，说明走的是 8-bit 分支）：

| 坐标 | `capture_format` | r / g / b |
|---|---|---|
| (1280, 200) | `B8G8R8A8_UNORM` | 1.000000 / 1.000000 / 1.000000 |
| (1280, 600) | `B8G8R8A8_UNORM` | 0.000000 / 1.000000 / 0.000000 |
| (1280, 1000) | `B8G8R8A8_UNORM` | 1.000000 / 1.000000 / 0.000000 |
| (200, 200) | `B8G8R8A8_UNORM` | 0.105882（= 27/255）×3 |

**打开 HDR 窗口后再测，格式没有变化**（再测 25 次全部 BGRA8，白色仍锁在 1.0）。
桌面复制的格式在建立 duplication 时协商，不随屏幕内容改变；
若走 FP16/scRGB，1000 nits 的白应读到约 `1000/80 = 12.5`，而不是 1.0。

### 5.4 与 §1 已知问题的差异：`DuplicateOutput1` 直接不可用

§1 记录的是「`DuplicateOutput1` 仍返回 `B8G8R8A8_UNORM`」。本机更彻底：

| 调用 | 结果 |
|---|---|
| `IDXGIOutput5::DuplicateOutput1([FP16, BGRA8])` | `DXGI_ERROR_UNSUPPORTED (0x887A0004)` |
| `IDXGIOutput5::DuplicateOutput1([BGRA8])` | `DXGI_ERROR_UNSUPPORTED` |
| `IDXGIOutput5::DuplicateOutput1([FP16])` | `DXGI_ERROR_UNSUPPORTED` |
| `IDXGIOutput1::DuplicateOutput` | `S_OK`，表面为 `B8G8R8A8_UNORM` |

即这颗驱动连「只请求 BGRA8」的格式列表都拒绝，工具必然落到 `recreateDuplication()` 的
`DuplicateOutput` 回退分支，`--dump` 的结果就是 8-bit 截断值。

### 5.5 新踩的坑

| 症状 | 原因 | 处置 |
|---|---|---|
| 启动即 `0xC0000005`；gdb 栈落在 `printf → __mingw_pformat → strlen` | `main.cpp` 用了 `%ws`（MSVC 专有格式符）。标准 C 的宽字符串是 `%ls`；MinGW 把 `%ws` 当 `%s`，于是对 `wchar_t*` 调 `strlen` | 改成 `%ls`（MSVC 同样支持），共 6 处、分布在 5 行：`main.cpp:65/68/104/146/150` |
| `AUTOMOC` 报 `libuv process spawn failed: operation not permitted` | CMake 的 AUTOMOC 走 libuv 进程链，需要命名管道；受限环境（沙箱）禁止 | 普通终端无此问题；受限环境下手工跑 `moc MainWindow.hpp` 并把生成的 `.cpp` 加入源列表 |
| 用 `DXGI_OUTDUPL_DESC.ModeDesc.Format` 判断捕获格式，会误报 `R16G16B16A16_FLOAT` | `ModeDesc` 描述的是**输出模式**，不是复制表面的像素格式 | 判断捕获格式必须读纹理的 `GetDesc().Format`（工具现有实现是对的） |
| 自编译的 exe 被拒绝运行 | 该机启用了 **Smart App Control**（强制模式），未签名 exe 一律拦截；事件 `Microsoft-Windows-CodeIntegrity/Operational` ID 3077/3033/3118 | 关闭 Smart App Control，或使用正规 CA 签名的证书；**自签名无效**（要求证书链在 Microsoft Trusted Root Program 内） |
| Qt 运行时缺失导致 exe 直接退出 | 只拷了 exe | 用 `windeployqt`，或手工拷 `Qt6Core/Gui/Widgets.dll` + `platforms\qwindows.dll` + MinGW 运行时（`libstdc++-6`、`libgcc_s_seh-1`、`libwinpthread-1`） |

### 5.6 结论与下一步

- 本机 **不满足** §2.3 的全部验收标准：`--list` 的 `hdr=on`、`color_space`、`max_lum` 三项成立，
  dxdiag 也成立，但 `--dump` 的 `capture_format` 只能是 `B8G8R8A8_UNORM`。
- 命中 §2.3 判定分支 → **Windows HDR 已开，但 Desktop Duplication 不提供 HDR 像素**。
- 下一步按 §1 的方案补 **`Windows.Graphics.Capture` + `R16G16B16A16_FLOAT`** 后端；
  在后端落地前，本机读数只能当"8-bit 截断值"用，不能用于 HDR 数值判断。

---

## 6. WGC 备用后端实现与验证（2026-11-19）

### 6.1 实现

- 新增 `src/WgcCapture.hpp/.cpp`：`IGraphicsCaptureItemInterop::CreateForMonitor` +
  `Direct3D11CaptureFramePool::CreateFreeThreaded(..., R16G16B16A16Float, 2, size)` +
  `GraphicsCaptureSession::StartCapture()`；
- `ScreenCapture` 自动选择后端：
  - SDR 输出 → Duplication；
  - HDR 输出 → 先探测 Duplication 是否能返回 `R16G16B16A16_FLOAT`；不能则用 WGC；
    WGC 初始化失败则回退 Duplication（HDR 数值会被截断）；
- 设置 `CHROMA_FORCE_WGC=1` 可在 SDR 下强制测试 WGC 路径；
- `--list` / `--dump` 增加 `backend` 字段：
  - `Duplication (DXGI)`；
  - `WGC (Windows.Graphics.Capture FP16)`；
- `%ws` 改为标准 `%ls`，修复 MinGW 下启动崩溃；
- WGC 依赖 Windows SDK 自带 C++/WinRT 头，仅 MSVC 构建启用；MinGW 构建保持 Duplication。

### 6.2 本机测试状态

- 构建：Debug / Release 均通过（MSVC + Qt 6.11.1 + Windows SDK 10.0.28000.0）；
- 本会话 HDR 当前开启（`--dump` 显示 `hdr=true`），但 WGC 初始化在
  `CreateForMonitor` 处返回 `E_ACCESSDENIED`（step=7）；工具按设计回退 Duplication；
- **已定位到关键环境因素**：Windows 11 的 `CaptureService`（"Enables optional screen
  capture functionality for applications that call Windows.Graphics.Capture"）在本机为
  `Stopped`，且普通用户没有启动权限（服务 DACL 只给 Administrators 启动权）；
  WGC 会因该服务不可用而返回 `E_ACCESSDENIED`；
- 以**管理员** PowerShell 启动服务后重试：

  ```powershell
  Start-Service CaptureService
  Get-Service CaptureService     # 应为 Running
  ```

  也可以把启动类型改为自动（需管理员）：

  ```powershell
  Set-Service CaptureService -StartupType Automatic
  ```

- 进一步验证：
  - `GraphicsCaptureAccess.RequestAccessAsync(Programmatic)` 对未打包 Win32 返回
    `NotDeclaredByApp(1)`；
  - `CaptureService` 的 per-user 实例 `CaptureService_4a3b9` 实际是 **Running**，
    模板服务 Stopped 属正常；
  - 在 `ConsentStore\graphicsCaptureProgrammatic` 设为 `Allow` 后，programmatic
    `CreateForMonitor` 仍返回 `E_ACCESSDENIED`。
- 因此新增 **系统 picker 回退**：GUI 模式下若 programmatic 失败，弹出
  `GraphicsCapturePicker`，用户选择目标显示器后继续走 WGC FP16。

### 6.3 在物理 HDR 控制台上的验证步骤

1. Windows 设置 → 系统 → 显示 → HDR 显示器 → 打开"使用 HDR"；
2. 构建 MSVC 版本：
   ```powershell
   cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
     -DCMAKE_PREFIX_PATH="<Qt6 msvc 路径>"
   cmake --build build --config Debug
   ```
3. 运行：
   ```powershell
   .\build\Debug\colorpicker.exe --list
   .\build\Debug\colorpicker.exe --dump --x <HDR屏内坐标> --y <HDR屏内坐标>
   ```
4. 期望：
   - `--list` 的 HDR 屏 `hdr=on`，`backend=WGC (Windows.Graphics.Capture FP16)`；
   - `--dump` 的 `capture_format` 为 `R16G16B16A16_FLOAT`；
   - `--dump` 的 `backend` 为 WGC；
   - SDR 白点 204 nits 时，白色应读到约 `204/80 = 2.55`（不是 1.0）；
     1000 nits 的 HDR 测试块应读到约 `12.5`。

如果 `--dump` 仍是 `B8G8R8A8_UNORM` 或 stderr 出现：

```
[wgc] initialize 失败: step=7 hr=0x80070005
```

说明当前会话不允许 `CreateForMonitor`（常见于远程/受限会话），请改用本机物理控制台；
如果物理控制台也失败，把 `verify.ps1` 报告发回来。

### 6.4 `CreateForMonitor` 返回 `E_ACCESSDENIED` 时

按顺序排查：

1. **使用 GUI 的 picker 回退**（推荐）：
   ```powershell
   .\build\Debug\colorpicker.exe
   ```
   把鼠标移到 HDR 屏、保持 HDR 开启；如果 programmatic 被拒绝，工具会弹出
   系统捕获选择框；选择 **目标显示器**（不是窗口），然后查看 stderr 是否出现
   `[wgc] capture started`，再检查 `--dump`/GUI 的 backend 是否变成 WGC。
2. `CaptureService` 是否为 `Running`（管理员启动，见 §6.2）；
3. 是否在**物理控制台**而不是远程/Virtual Desktop 会话；
4. 用 Windows 自带的 `Win+Shift+S`（截图工具）验证系统捕获栈是否正常；
5. 若 picker 也失败，检查事件查看器 → Windows 日志 → 系统；
6. `--dump` 没有 HWND，无法弹 picker；自动化测试请用 GUI。

### 6.5 已知限制

- WGC 需要 MSVC 构建；MinGW 构建看不到 WGC（会自动走 Duplication）；
- WGC 首次捕获可能有短暂黑帧/延迟，属于正常现象；
- 工具使用 `CreateFreeThreaded` + 轮询 `TryGetNextFrame`，未使用事件回调；
- WGC 数值语义为 scRGB 线性（1.0 = 80 nits），与 Duplication FP16 路径一致。

## 7. 2026-11-20 平台抽象重构回归（Windows）

### 7.1 范围

- 新增 `src/core`：平台无关类型、`ICaptureSession`、`IDisplayProbe`、工厂声明；
- Windows 实现移入 `src/platform/windows`，并通过 `WindowsCaptureSession` /
  `WindowsDisplayProbe` / `WindowsHdrPreview` 适配；
- UI 与 `main.cpp` 改为只依赖 core/UI 接口；
- macOS 放入 `src/platform/macos` 骨架（CoreGraphics probe + 占位 capture/HDR 预览）；
- 文档：`ARCHITECTURE.md`、`WINDOWS.md`。

### 7.2 构建

```powershell
cd _shirley\tools\colorpicker
cmake -S . -B build_refactor -DCMAKE_PREFIX_PATH="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build build_refactor --config Debug
cmake --build build_refactor --config Release
```

结果：Debug / Release 均 exit 0；MSVC + WGC 检测通过。

### 7.3 CLI 兼容性

用重构前的 `build_ui\Debug\colorpicker.exe` 与重构后的
`build_refactor\Debug\colorpicker.exe` 对同一坐标做 `--dump`，`Compare-Object`
无差异：

- SDR 坐标 `(100,100)`：JSON 完全一致；
- HDR 坐标 `(3000,500)`：JSON 完全一致（`hdr=true`、`R16G16B16A16_FLOAT`/BGRA 当前路径、
  `sdr_white_level=2550`、`sdr_white_nits=204.0`、`max_luminance_nits=603.7`）。

`--list` 也保持 SDR/HDR 显示器、格式、backend、HDR 字段与新字段一致。

### 7.4 GUI

- 重构后窗口可启动，16×16 放大镜、HDR 色块、9 行读数正常；
- 截图：`temp_and_scratch/refactor-ui2.png`（SDR 路径，读数与旧版一致）；
- Esc/QShortcut、无边框拖动逻辑未改动（沿用重构前实现）。

### 7.5 边界检查

- `src/core`、`src/ui`、`src/app` 未 include `windows.h` / DXGI / D3D11 / AppKit /
  ScreenCaptureKit（注释中的关键词不计）；
- 平台差异只通过 core 工厂进入。

### 7.6 未验证项

- macOS 骨架无法在 Windows 上编译；仅保证文件、工厂、CMake 分支完整；
- macOS ScreenCaptureKit、TCC 权限、EDR 预览均为 TODO（见 `ARCHITECTURE.md` §9）；
- WGC picker/HDR 路径行为未改，仍按原实现验证。
