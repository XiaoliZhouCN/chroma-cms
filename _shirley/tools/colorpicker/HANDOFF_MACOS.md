# macOS 开发 Handoff

> 交接时间：2026-11-20
> 分支：`practice`
> Windows 基线：`0367ca8`（平台抽象重构 `65ca627` + 放大镜去网格）
> 目标：在这份交接的基础上完成 macOS ScreenCaptureKit 捕获、HDR 与 EDR 预览。

## 0. 先读什么

按顺序：

1. [`ARCHITECTURE.md`](ARCHITECTURE.md)（框架规范，必须遵守）
2. [`WINDOWS.md`](WINDOWS.md)（理解当前捕获语义、HDR/CCCS、picker 行为）
3. [`README.md`](README.md)（构建/运行）
4. 本文（macOS 任务拆分、验收、坑）

## 1. 当前状态

### Windows

- 已按 `ARCHITECTURE.md` 完成平台抽象重构；
- Duplication/WGC、FP16 HDR、HDR scRGB 预览、WGC picker、SDR 白点刷新、无边框 UI 均保持不变；
- Debug/Release 构建通过；重构前后 `--dump` JSON 对同一坐标完全一致；
- 不需要在 macOS 阶段修改 Windows 后端（除非接口要演进）。

### macOS

已有骨架，**尚未在 Mac 上编译/运行验证**：

| 文件 | 当前状态 |
|---|---|
| `src/platform/macos/MacPlatform.mm` | 工厂：probe + capture session |
| `src/platform/macos/MacDisplayProbe.mm` | 用 `CGGetOnlineDisplayList` 枚举显示器；光标用 `CGEventGetLocation`；Retina/多屏坐标是 TODO |
| `src/platform/macos/MacCaptureSession.mm` | ScreenCaptureKit 占位：initialize 打印 TODO 并返回 false |
| `src/platform/macos/MacHdrPreview.cpp` | Qt SDR 占位色块；Metal EDR TODO |
| `CMakeLists.txt` | Apple 分支已建立，链接 AppKit/CoreGraphics/ApplicationServices |

Windows 上的边界规则已检查：`core/ui/app` 不直接 include 平台头文件。

## 2. 环境与构建

要求：

- macOS 13+ 可以构建 SDR 骨架；
- **HDR 捕获需要 macOS 15+**（ScreenCaptureKit HDR preset）；
- Xcode command line tools；
- Qt6（Widgets）。

```bash
cd _shirley/tools/colorpicker
cmake -S . -B build-macos -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build-macos --config Release
```

当前预期：

- 构建成功；
- `./build-macos/colorpicker.app/Contents/MacOS/colorpicker --list` 能列出显示器；
- GUI 打开，但捕获初始化打印 `[mac] MacCaptureSession 是重构骨架...`，界面显示初始化失败；
- 这是正常的 skeleton 状态。

## 3. 代码地图（Mac 侧只改这些）

```text
src/core/Types.hpp                 # 不要加平台类型；必要时扩展可选字段
src/core/CaptureSession.hpp       # 捕获接口
src/core/DisplayProbe.hpp         # 显示器/光标接口
src/core/Platform.hpp             # 工厂声明
src/ui/MainWindow.cpp             # 共享 UI；原则上不改，除非需要平台提示文案
src/ui/HdrPreview.hpp             # HdrPreview 接口
src/platform/macos/               # ← Mac 实现全部在这里
  MacPlatform.mm
  MacDisplayProbe.{hpp,mm}
  MacCaptureSession.{hpp,mm}
  MacHdrPreview.cpp
```

规则：

- `core/ui/app` 不写 `#ifdef __APPLE__`；
- 平台行为进 `platform/macos`；
- 需要新能力时先扩展 core 接口/类型，再改 Windows（如果需要）和 Mac；
- 不要把 Windows 的 `CCCS` 硬编码到 Mac UI；Mac 用实际捕获色彩空间描述。

## 4. 实施顺序

### M1：ScreenCaptureKit SDR 捕获（优先）

目标：macOS 13+ 的 SDR 取色可用。

1. **TCC 屏幕录制权限**
   - `CGPreflightScreenCaptureAccess()` 检查；
   - 未授权时用 `CGRequestScreenCaptureAccess()` 或首次 `SCShareableContent` 触发；
   - UI 显示"需要屏幕录制权限，授权后重启应用"，提供打开系统设置的方式；
   - 注意：从 Terminal 启动时，授权对象可能是 Terminal；从 .app 启动时是 app。

2. **显示器/坐标**
   - `MacDisplayProbe` 的 `bounds` 与 `cursorPosition` 必须是**同一坐标系**；
   - 建议统一为物理像素：用 `NSScreen.backingScaleFactor` / `CGDisplayMode` 换算；
   - 在 Retina + 多显示器上验证：光标在窗口内、magnifier 中心与实际像素一致；
   - 明确 `CGDisplayBounds` / `CGEventGetLocation` / Qt logical 坐标的差异，并把结论写回 `ARCHITECTURE.md`。

3. **捕获**
   - `SCShareableContent.current` 异步拿到 display；
   - `SCContentFilter(display:excludingApplications:)`，排除自己的 app，避免放大镜/窗口入镜；
   - `SCStreamConfiguration`：
     - `width/height` = 捕获输出像素尺寸；
     - `pixelFormat = kCVPixelFormatType_32BGRA`；
     - `showsCursor = NO`；
   - `SCStream` + `SCStreamOutput` 在后台 queue 接收 `CMSampleBuffer`；
   - 锁 `CVPixelBuffer`，拷贝到"最新帧"槽（BGRA8，记录 rowBytes/width/height）；
   - UI 线程 `captureFrame()` 只读最新帧；`sampleRect()` 从最新帧拷贝 1×1/16×16；
   - 不要每个 UI tick 都做完整 `SCScreenshotManager` 截图。

4. **DisplayInfo**
   - `MacDisplayProbe`/session 填充：显示器名、bounds、primary；
   - `colorSpace` 可先填 `CGColorSpace` 名称 / `Display P3 / sRGB`；
   - `sdrWhiteNits`、`bitsPerColor`、`advancedColor*` 在 macOS 没有等价字段时保持 `nullopt`/0，UI 显示 `n/a`；
   - 不要伪造 Windows 的 `SDRWhiteLevel`。

验收：

- SDR 测试图（已知 RGB 色块）中心 1×1 读出与图像源预期一致（允许因显示器 ICC/合成造成的合理差异，需记录依据）；
- 16×16 放大镜、连续跟随、多显示器切换正常；
- 未授权/拒绝/撤销权限时 UI 有明确提示，不崩溃；
- `--dump` / `--list` 输出可读，JSON 字段与 Windows 版本兼容（可选字段允许 null）。

### M2：HDR 捕获（macOS 15+）

目标：HDR 屏幕上捕获 HDR 内容，不把值截断成 SDR。

1. 用官方 preset（优先）：
   - `SCStreamConfiguration(preset: .captureHDRStreamLocalDisplay)`：本机捕获+显示，适合"测当前屏幕"；
   - 或 `.captureHDRStreamCanonicalDisplay`：标准 HDR 传输；二选一需在文档里写明理由；
   - `SCScreenshotManager.captureSampleBuffer(contentFilter:configuration:)` 可作为 `--dump` 单点精确取值路径。

2. 处理 Frame：
   - preset 输出 10-bit YCbCr（Display P3 PQ/HLG，具体以实际 config 为准）；
   - 从 `CVPixelBuffer` attachments 读取 `kCVImageBufferColorPrimariesKey`、
     `kCVImageBufferTransferFunctionKey`、`kCVImageBufferYCbCrMatrixKey`；
   - 做 range（video/full）、YCbCr→RGB、PQ/HLG→linear/nits；
   - 记录最终的 `PixelEncoding` / `CaptureFormat` / `DisplayInfo.colorSpace`。

3. **精度风险（必须验证）**：
   - HDR preset 可能是 4:2:0/4:2:2 色度抽样；1×1 取色可能不精确；
   - 如果 stream 抽样不可接受，使用 `SCScreenshotManager` HDR screenshot 作为单点取值，
     stream 只负责连续跟踪/放大镜；
   - 把测量结论写进 `VERIFICATION.md`，不要静默忽略。

4. 色彩语义：
   - macOS 不存在 Windows CCCS；UI 的 `CCCS色域` 标签在 macOS 应显示实际捕获色域
     （必要时改成平台无关的"捕获色域"文案，Windows 仍显示 scRGB）；
   - 不要拿 macOS HDR 值直接和 Windows scRGB 数值比较；先统一到明确的空间/nits，再比较。

验收：

- macOS 15 + HDR 屏上，HDR 测试块读到 > SDR 白的值；转换后的 nits/色度有书面依据；
- 旧 macOS / SDR 屏自动回退 SDR，不崩溃；
- `--dump` 标记 HDR 格式；文档记录 local vs canonical 的选择和精度限制。

### M3：Metal EDR 预览

目标：右侧色块在 EDR 屏上显示 HDR 值，不裁剪。

1. 用 `QWidget::winId()` 拿到 NSView；创建 `CAMetalLayer`；
2. `wantsExtendedDynamicRangeContent = YES`；
3. `colorspace` 用扩展线性色域（如 `kCGColorSpaceExtendedLinearDisplayP3`）；
4. 根据屏幕 EDR headroom 决定是否启用：
   - `NSScreen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0`；
   - 否则回退 Qt SDR 并明确标注"预览被裁剪"；
5. 处理窗口移动/跨屏（`ScreenChangeInternal`）时重建 layer/尺寸；
6. `Present` 不要在 UI 线程阻塞；参考 Windows `HdrSwatch` 的 `DO_NOT_WAIT` 处理。

验收：

- EDR 屏上 >1 的值可见，SDR 屏自动回退；
- 跨屏/全屏/权限变化不崩溃、不卡死；
- UI 不把 EDR 预览当测量值。

### M4：验证与文档

- 更新 `README.md`、`ARCHITECTURE.md` §9、`VERIFICATION.md`；
- 增加 Mac 实测环境（macOS 版本、显示器、Retina、HDR/EDR）；
- `--dump` SDR/HDR 样例、截图；
- 若接口有改动，同步 Windows 并重跑 Windows 回归。

## 5. 接口契约（不要破坏）

- `MonitorInfo.bounds` / 光标坐标：**全局物理像素**；平台内部负责换算；
- `ICaptureSession::initialize(monitor, owner)`：owner 是原生窗口句柄；Mac 若不需要可忽略；
- `captureFrame()` 非阻塞；`sampleRect()` 返回 `format` 和 `rgba`；
- `ColorSample.rgba` 是捕获空间原始分量，不自动转 sRGB；
- `HdrPreview::setColor(..., linear)`：`linear=true` 表示传入线性值；Mac 实现负责到 EDR 空间；
- 失败要回退并输出 stderr 原因，不能静默失败；
- core/UI 不 include 平台头文件。

## 6. 已知坑

1. **TCC**：Screen Recording 权限不在 Info.plist 里；在系统设置 → 隐私与安全性 → 屏幕录制中授权；
   修改后可能需要重启 app。Terminal 启动和 .app 启动的授权主体不同。
2. **ScreenCaptureKit 异步**：`SCShareableContent.current` / `SCStream.startCapture` 都是 async；
   不要阻塞 UI 主线程。
3. **Retina**：`CGDisplayBounds` 与 Qt logical point、`CGEventGetLocation` 可能不是同一坐标系；
   先在 Mac 上打印实际值验证，再决定 probe 的换算。
4. **HDR 需要 macOS 15+**：旧系统编译时用 `@available` 守卫，运行时回退 SDR。
5. **EDR 不等于参考显示器**：EDR 只保证渲染 HDR；第三方 HDR 屏不一定校准；
   不要把预览当测量基准。
6. **原生子窗口**：QWidget 内嵌 CAMetalLayer/D3D 子窗口在跨屏/DPI 时可能错位；
   `HdrPreview::refreshSurface()` 必须在屏幕变化时调用。
7. **不要用 `CGDisplayCreateImage` 当 HDR 方案**：它已弃用且通常只有 SDR。

## 7. 验证清单（Mac）

- [ ] `--help` / `--list` / `--dump` 可运行；
- [ ] 未授权、拒绝、撤销权限均有提示；
- [ ] 单显示器 / Retina / 多显示器坐标正确；
- [ ] SDR 1×1 与 16×16 放大镜正确；
- [ ] HDR 测试块读取和转换有数值依据；
- [ ] EDR 色块不裁剪，SDR 自动回退；
- [ ] 跨屏、休眠唤醒、HDR 开关切换不崩溃；
- [ ] 更新 docs + 截图 + 验证记录；
- [ ] Windows 构建/CLI 回归仍通过（接口有改动时）。

## 8. 交付方式

- 在 `practice` 分支本地 commit；push 由用户执行；
- 每个里程碑单独 commit + 文档；
- 不要改 `docs/` 之外未授权范围。

## 9. 需要产品确认的问题

1. macOS 最低版本：`13+`（SDR）还是直接 `15+`（HDR）？
2. macOS HDR 用 local display 还是 canonical display 作为默认？
3. HDR 1×1 取色是否接受 YCbCr 抽样误差，还是必须用 screenshot 路径保证单点精度？
4. 是否要求签名/公证/App Store；当前默认本地未签名开发。
