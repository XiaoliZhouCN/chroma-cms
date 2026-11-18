# Chroma ColorPicker（HDR raw 原型）

独立的小工具：在 Windows 11 HDR 显示器上读取**鼠标光标下 1×1 像素**的原始捕获值，
用于对比 Chrome 与 ChromaCMS 同一张测试图的输出。

## 当前能力（第一版）

- Qt6 小窗口置顶，连续跟随鼠标取色；
- 放大镜 100×80（16×）+ 颜色块 50×50 + 3 行原始 RGB；
- 捕获后端：`IDXGIOutput5::DuplicateOutput1`，优先请求 `R16G16B16A16_FLOAT`（HDR），
  失败时退回 `B8G8R8A8_UNORM`；
- 显示状态：HDR 开关、DXGI 色彩空间、SDR 白点（nits）、最大亮度、显示器名；
- 隐藏调试命令：`colorpicker --dump [--x X --y Y]` 输出 JSON（用于自动验证）。

## 不做的事（本版）

- 不做色彩管理/换算（不显示 sRGB/P3/XYZ/Lab，只显示捕获原始值）；
- 不做格式选项、保存、热键、11×11 平均、ICC 转换；
- 不保证"面板物理颜色"——软件只能读到合成后的 framebuffer 数值。

## 换机/HDR 验证

完整的问题记录、当前状态和换机测试步骤见 [`VERIFICATION.md`](VERIFICATION.md)。

一键收集验证信息（推荐在新电脑上运行）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1
```

会生成 `verification-*.txt`，包含会话类型、GPU/当前模式、dxdiag HDR 字段、
`--list` 与每个显示器中心点的 `--dump` 结果。

## 构建（Windows / MSVC + Qt6）

```powershell
cd _shirley\tools\colorpicker
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_PREFIX_PATH="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build build --config Debug
```

产物：`build\Debug\colorpicker.exe`

## 运行

```powershell
.\build\Debug\colorpicker.exe
# 调试：打印当前光标位置的捕获结果
.\build\Debug\colorpicker.exe --dump
# 调试：列出所有显示器与 HDR 状态
.\build\Debug\colorpicker.exe --list
```

## 读数说明

- `R16G16B16A16_FLOAT`：DWM 合成使用的 scRGB 线性值（1.0 = 80 nits）；
- `B8G8R8A8_UNORM`：SDR 8-bit（sRGB 编码），HDR 开启时读到的是截断后的值；
- 窗口会显示当前捕获格式与显示器 HDR 状态，便于判断当前数据属于哪条路径。

## 当前验证（2026-11-19，本机 Console 会话）

- DISPLAY1（VG32AQL1A）HDR 已开启：dxdiag 显示
  `Display Color Space: DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`、
  `Active Color Mode: DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR`；
  工具 `--list` 现在正确输出 `hdr=on`、`max_lum=603.7`；
- **已知问题**：该 Intel UHD 驱动上，`IDXGIOutput5::DuplicateOutput1` 在 HDR 模式下
  仍返回 `B8G8R8A8_UNORM`，HDR 数值被截断（白色读到 1.0）。下一步补
  `Windows.Graphics.Capture` FP16 后端。
- 已知纯红窗口测试：`--dump` 返回 `r=1.0 g=0.0 b=0.0`，与 GDI 读数一致；
- 内屏 BOE 1920×1200 经 dxdiag 确认 `HDR Not Supported`，不是 HDR 屏。

## 已知限制

- 本机 Intel UHD 上 HDR 模式下 `DuplicateOutput1` 只返回 BGRA8，HDR 数值不完整；WGC 后端待补；
- 受保护内容 / 独占全屏 / secure desktop 会导致 `ACCESS_LOST`；\n- Desktop Duplication 刚初始化时可能先返回黑帧，随后桌面更新后恢复；`--dump` 会自动重试；
- 显示器切换 HDR 开关后，状态最多延迟约 2 秒刷新；
- 颜色块和放大镜使用 sRGB 近似预览，HDR 值 > 1 会被裁剪，仅作视觉参考。

## 验证方案（本版）

同一张测试图分别在 Chrome 和 ChromaCMS 中显示，把光标移到同一像素，记录工具显示的
"捕获格式 + 显示器 HDR 状态 + 原始 RGB"；两侧结论只描述"各自处于什么捕获路径、原始值是多少"，
本版不做 ΔE 或跨色域换算。
