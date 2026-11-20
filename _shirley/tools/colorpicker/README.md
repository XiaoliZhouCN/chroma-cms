# Chroma ColorPicker

跨平台（Windows / macOS）屏幕原始取色小工具：读取鼠标光标下 1×1 像素的捕获值，
用于对比 Chrome 与 ChromaCMS 同一张测试图的输出。

当前状态（2026-11-20）：

- **Windows**：已完成平台抽象重构，功能与重构前保持一致（Duplication/WGC + HDR 预览）；
- **macOS**：已建立双平台 CMake/目录/接口骨架，ScreenCaptureKit 捕获与 Metal EDR 预览为 TODO；
  下一步开发直接按 [`HANDOFF_MACOS.md`](HANDOFF_MACOS.md) 执行；
- UI 使用 Qt6 Widgets，无边框置顶、可拖动、Esc 退出；16×16 放大镜 + HDR 色块。

## 文档

| 文档 | 内容 |
|---|---|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | 跨平台框架规范：分层、core 接口、平台职责、macOS TODO |
| [`WINDOWS.md`](WINDOWS.md) | Windows 当前技术方案：D3D11/Duplication/WGC、HDR、元数据、限制 |
| [`HANDOFF_MACOS.md`](HANDOFF_MACOS.md) | **macOS 开发交接**：任务拆分、验收、已知坑 |
| [`VERIFICATION.md`](VERIFICATION.md) | 历史问题记录与验证结果 |
| `verify.ps1` | Windows 一键收集验证信息 |

## 目录

```text
src/
  core/        平台无关类型 + ICaptureSession / IDisplayProbe / 工厂声明
  ui/          MainWindow、放大镜、HdrPreview 接口
  app/         main.cpp（GUI / --list / --dump）
  platform/
    windows/   D3D11 Duplication、WGC、HDR swatch、probe、适配器
    macos/     CoreGraphics probe + ScreenCaptureKit 骨架
```

规则：`core/ui/app` 不包含平台头文件；平台差异只通过 `createDisplayProbe()` /
`createCaptureSession()` / `createHdrPreview()` 进入。详见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

## 构建（Windows / MSVC + Qt6）

```powershell
cd _shirley\tools\colorpicker
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_PREFIX_PATH="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build build --config Debug
```

产物：`build\Debug\colorpicker.exe`

MinGW：可构建 SDR/Duplication 路径；WGC 仅在 MSVC + Windows SDK C++/WinRT 下启用。

## macOS 骨架构建（待 macOS 实机验证）

```bash
cd _shirley/tools/colorpicker
cmake -S . -B build-macos -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build-macos --config Release
```

当前 macOS 后端是占位：能枚举显示器、打开 UI，但捕获初始化会打印 TODO 并失败；
ScreenCaptureKit/HDR/Metal EDR 实现计划见 [`ARCHITECTURE.md`](ARCHITECTURE.md) §9。

## 运行

```powershell
.\build\Debug\colorpicker.exe              # GUI
.\build\Debug\colorpicker.exe --dump       # 当前光标位置 JSON
.\build\Debug\colorpicker.exe --list       # 显示器与 HDR 状态
```

## 读数

- `R16G16B16A16_FLOAT`（Windows HDR）：DWM CCCS scRGB 线性值，1.0 = 80 nits；
- `B8G8R8A8_UNORM`（SDR）：sRGB 编码值，0..1；
- 面板中的 `CCCS`、`wire format`、`SDRWhiteLevel` 均为 Windows 语义；
  macOS 实现后会显示实际捕获色彩空间，不假装 scRGB。
- 放大镜是 sRGB 近似预览；右侧色块走平台 HDR 预览（Windows 为 scRGB swapchain）。

## 验证

重构后的 Windows 回归步骤与结果见 [`VERIFICATION.md`](VERIFICATION.md) 的
"2026-11-20 平台抽象重构回归"一节。
