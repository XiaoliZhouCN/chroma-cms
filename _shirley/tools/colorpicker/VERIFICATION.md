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
