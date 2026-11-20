# Windows 后端技术方案

> 状态：2026-11-20，重构后 Windows 实现说明。
> 本文记录 **Windows 当前实际做法**；跨平台框架见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

## 1. 为什么用 D3D11

Windows 屏幕捕获 API 是 D3D11 生态：

- `IDXGIOutput1::DuplicateOutput` / `IDXGIOutput5::DuplicateOutput1` 要求传入 **D3D11 device**；
- WGC 的帧池是 `Direct3D11CaptureFramePool`，没有 D3D12 版本；
- 本工具只取 1×1 / 16×16 像素，D3D11 的 `CopyResource + Map` 已足够；
- D3D12 不提供捕获入口；如需 D3D12 渲染，应从 D3D11 捕获纹理 interop 到 D3D12，而不是重写捕获。

## 2. 捕获后端

文件：`src/platform/windows/Capture.*`、`WgcCapture.*`、`WindowsCaptureSession.*`。

### 2.1 Desktop Duplication

- 用 `CreateDXGIFactory1` 枚举 adapter/output，按 `HMONITOR` 选中当前显示器；
- `D3D11CreateDevice` 创建 BGRA-support 设备；
- `IDXGIOutput5::DuplicateOutput1` 先请求 `R16G16B16A16_FLOAT + B8G8R8A8_UNORM`；
- 若驱动不支持 FP16，再请求纯 BGRA8；
- 最后回退 legacy `IDXGIOutput1::DuplicateOutput`；
- 帧先 `CopyResource` 到内部纹理；采样时只把目标区域拷贝到 staging 并 `Map`，避免全帧回读；
- `AcquireNextFrame` 超时保留上一帧；`ACCESS_LOST` 时重建 duplication。

### 2.2 WGC（Windows.Graphics.Capture）

- 仅 MSVC + Windows SDK C++/WinRT 编译（`CHROMA_HAS_WGC`）；
- HDR 或 `CHROMA_FORCE_WGC=1` 时，若 Duplication 拿不到 FP16，则释放 duplication 并尝试 WGC；
- `IGraphicsCaptureItemInterop::CreateForMonitor` 的 programmatic 路径在 Windows 11 26H2
  对未打包 Win32 返回 `E_ACCESSDENIED`；
- 回退到系统 `GraphicsCapturePicker`，用户选择目标显示器；
- 选中的 `GraphicsCaptureItem` 按 `HMONITOR` 缓存在进程内，避免反复弹 picker；
- 使用 `Direct3D11CaptureFramePool::CreateFreeThreaded(..., R16G16B16A16Float, 2, size)`；
- 每次取色前 drain 最多 8 帧，取最新帧；
- WGC 初始化成功后 **锁定当前显示器**，鼠标移出不再重建捕获（避免闪烁和重复 picker）；
  需要换屏时重启工具或回到目标屏。

## 3. 格式与 HDR 语义

| 路径 | 格式 | 数值含义 |
|---|---|---|
| Duplication SDR | `B8G8R8A8_UNORM` | sRGB 编码，0..1；面板按 0–255 显示 |
| Duplication HDR | 可能 `R16G16B16A16_FLOAT` | scRGB linear，1.0 = 80 nits |
| WGC HDR | `R16G16B16A16_FLOAT` | scRGB linear，1.0 = 80 nits |

- SDR 白色 204 nits → scRGB `204/80 = 2.55`；
- HDR 预览色块使用 D3D11 `R16G16B16A16_FLOAT` swapchain + `SetColorSpace1(RGB_FULL_G10_NONE_P709)`，
  交给 DWM 做 scRGB 合成，因此 >1 的值不会被 Qt 裁剪；
- 放大镜仍是 sRGB 近似预览（仅视觉参考）。

## 4. 显示器/HDR 元数据

`WindowsCaptureSession::syncDisplay()` 把 Win32 数据转换为 core `DisplayInfo`。

### DXGI_OUTPUT_DESC1

通过 `IDXGIOutput6::GetDesc1` 获取：

- `ColorSpace`（HDR = `RGB_FULL_G2084_NONE_P2020`）；
- `BitsPerColor`（wire 位深）；
- `MinLuminance` / `MaxLuminance` / `MaxFullFrameLuminance`；
- primaries/white point。

### DisplayConfig

通过 `QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)` + `DisplayConfigGetDeviceInfo`：

- `GET_TARGET_NAME`：显示器友好名（如 VG32AQL1A）；
- `GET_ADVANCED_COLOR_INFO`：Advanced Color 支持/启用、wide color、编码、bits；
- `GET_SDR_WHITE_LEVEL`：**SDRWhiteLevel 原始值**。

SDR 白点换算使用 Microsoft 定义：

```text
nits = SDRWhiteLevel / 1000 × 80
SDRWhiteLevel = nits / 80 × 1000
```

例如 `2550` = `204 nits`。Windows 设置滑杆的"界面百分比"没有公开 API；工具显示原始值。

## 5. 坐标与 DPI

- `--dump` 模式调用 `SetProcessDPIAware()`，使用全局物理像素；
- GUI 由 Qt 管理 DPI；坐标从 `IDisplayProbe::cursorPosition()` / `monitorAt()` 获取；
- `MonitorInfo.bounds` 是物理像素；采样坐标 = 光标全局坐标 − 显示器左上角；
- 切换显示器/HDR 设置后重新初始化捕获；应用失焦时暂停采集并保留读数。

## 6. HDR 预览

文件：`platform/windows/HdrSwatch.*`、`WindowsHdrPreview.*`。

- 子 HWND + `R16G16B16A16_FLOAT` flip-discard swapchain；
- `SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)`（scRGB）；
- `Present(1, DXGI_PRESENT_DO_NOT_WAIT)`，避免 HDR 设置切换时阻塞 UI；
- `DXGI_ERROR_DEVICE_REMOVED/RESET` 时释放并重建；
- 屏幕/DPI 变化（`QEvent::ScreenChangeInternal`）重建 swapchain，避免原生子窗口错位；
- 创建失败时回退 Qt SDR 近似绘制。

## 7. 当前限制

1. 本机 Intel UHD + HDR 下 Duplication 有时只返回 BGRA8；WGC 是完整 HDR 的保障；
2. WGC programmatic capture 在未打包 Win32 上被系统拒绝，需要 picker 或系统截图权限；
3. WGC picker 每次工具启动通常需要选一次显示器（进程内缓存 item）；
4. WGC 捕获期间锁定显示器，不支持自动跟随到其他显示器；
5. 受保护内容/独占全屏/secure desktop 可能 `ACCESS_LOST`；
6. 显示器切换 HDR 开关后，元数据最多延迟 500 ms（独立刷新定时器）；
7. 预览色块是 HDR 真值，放大镜/UI 本身仍是 SDR 内容。

## 8. 代码入口

| 文件 | 作用 |
|---|---|
| `src/platform/windows/Capture.cpp` | Duplication + 设备/格式/display 状态 |
| `src/platform/windows/WgcCapture.cpp` | WGC FP16/picker |
| `src/platform/windows/WindowsCaptureSession.cpp` | `ICaptureSession` 适配 |
| `src/platform/windows/WindowsDisplayProbe.cpp` | `IDisplayProbe` 适配 |
| `src/platform/windows/WindowsHdrPreview.cpp` / `HdrSwatch.cpp` | HDR 预览 |
| `src/platform/windows/WindowsPlatform.cpp` | 工厂/DPI 初始化 |

## 9. 验证命令

```powershell
cd _shirley\tools\colorpicker
cmake -S . -B build_refactor -DCMAKE_PREFIX_PATH="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build build_refactor --config Debug
.\build_refactor\Debug\colorpicker.exe --list
.\build_refactor\Debug\colorpicker.exe --dump --x 3000 --y 500
.\build_refactor\Debug\colorpicker.exe
```

期望：

- `--list` 同时列出 SDR/HDR 显示器；
- HDR 屏 `hdr=on`、`color_space=...G2084_NONE_P2020`、`sdr_white=204.0`；
- `--dump` JSON 与重构前完全一致；
- GUI 显示 scRGB 读数（204 nits 白色约 2.55），HDR 色块不裁剪。
