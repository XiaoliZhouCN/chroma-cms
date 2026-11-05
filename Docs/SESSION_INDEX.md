---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 会话索引（追加式台账）

> **只允许追加，禁止改写历史。** 保留最近 30 条，更早的删掉——全史交给 `git log`。
> 格式见 [templates/SESSION_ENTRY.md](templates/SESSION_ENTRY.md)。
> 每条必须写"下次第一件事"，否则下次会话仍然从零开始。

## 索引

| # | 日期 | 会话目标 | 产出 | 未决 / 风险 | 下次第一件事 |
|---|---|---|---|---|---|
| 001 | 2026-11-04 | 建立文档框架与仓库基线 | AGENTS/README/INDEX/ARCHITECTURE/CONVENTIONS/BUILD/COLOR_MANAGEMENT/COLOR_SPEC/TESTING/ROADMAP/AGENT_WORKFLOW；ADR-0001~0007；`check-docs.ps1`；git 初始化到 `develop` | Skia 构建参数未实测（BUILD §3 四项待验证）；ADR-0007 仍是 proposed；Q2（Skia 能否 wrap 交换链后台缓冲）未验证 | 落 M0 剩余出口：`CMakeLists.txt` + `CMakePresets.json` + `vcpkg.json`，跑通空窗口 |
| 002 | 2026-11-04 | M0 构建骨架 + 空窗口 | `CMakeLists.txt`/`CMakePresets.json`/`cmake/`；`src/core`、`src/app`（MainWindow/ViewportWidget）；`tests`（unit.smoke、app.selftest）；BUILD 按实测环境重写；ADR-0007 补实测 | Skia 构建未做（改为 M3 前置）；Agent 沙箱无法跑 AUTOMOC；`/WX` 未启用 | M1：`IRenderer` + D3D12 设备/队列/交换链，画出第一张纯色图 |
| 003 | 2026-11-05 | 迁入 HDR 测试图；M1 上屏 | `tools/gen_ptmr.py`、`assets/test-images/*`（主图+预览+CSV）、`src/gfx`、`src/media`、`src/render`、`shaders/image.hlsl`；CMake 按模块拆分；D3D12 成功显示 PTMR 图 | resize/拖拽未验证；`ReportLiveObjects` 未做；HLSL 运行时编译待改；色彩管线（M2）未开始 | M1 收尾：加泄漏检查 + resize 验证，然后进 M2 |

## 详细记录

### 001 · 2026-11-04 · 文档框架

**目标**：把空占位骨架变成可执行、可校验的文档体系。

**决策**

- 文档分三类（契约 / 导航 / 台账），每类不同变更频率与评审强度，见 [INDEX.md](INDEX.md) §2。
- `main` 分支保持为空，全部工作在 `develop`（ADR 未单列，写入 [AGENTS.md](../AGENTS.md) 硬规则）。
- 目录统一小写（`Docs/adr/`），文档文件名 `UPPER_SNAKE.md`。
- 删除原 `Docs/TEMPLATES/`（4 个 0 字节文件）与 `ARCHITECTURE_DESIGN.md` 死链引用，改为 `Docs/templates/` + `Docs/adr/TEMPLATE.md`。

**产出**：见索引表。

**未决 / 风险**

1. Skia D3D12 的 GN 参数、产物名、API 签名均未实测 → 不得当作事实引用。
2. ADR-0007（交换链格式与色彩空间）仍为 proposed，M1 前必须定。
3. 双后端一致性测试的读回格式未定，M3 前需要定。
4. 仓库尚无任何代码，`check-docs` 是当前唯一的自动门禁。

**踩坑**

- 原 `AGENTS.md` 的必读列表指向不存在的 `ARCHITECTURE_DESIGN.md`，且所有目标都是空文件——已修正。教训：**空占位文档比没有文档更有害**，它会让 Agent 误以为规则已加载。

**下次第一件事**：写 `CMakeLists.txt` / `CMakePresets.json` / `vcpkg.json`，实现 M0 的"空窗口启动 + 一个 ctest 占位用例"。

### 002 · 2026-11-04 · M0 构建骨架 + 空窗口

**目标**：让仓库从零构建出可运行的空窗口，并跑通测试链路。

**决策**

- **Skia 移出 M0 关键路径**：`CHROMA_WITH_SKIA=OFF` 默认关闭，源码构建改为 M3 前置任务。
  理由：Skia 首次构建 1–3 小时且依赖 Python/depot_tools，把它放在 M0 会让整条学习路径阻塞在一个与"渲染链路"无关的环节上。
- **不引入 vcpkg**：Qt 已本地安装，M0–M3 无第三方 C++ 依赖；FFmpeg 到 M4 再决定。
- **Qt 自动发现**：`cmake/QtBootstrap.cmake` 扫描常见安装位置取最高版本，本机绝对路径不进 `CMakePresets.json`。
- **保留控制台子系统**（不设 `WIN32_EXECUTABLE`）：M0–M1 需要直接看到日志。
- `/WX` 暂缓：Qt 6.11 头文件在 `/W4` 下仍有告警，待收敛后开启。

**产出**

- `CMakeLists.txt`、`CMakePresets.json`、`cmake/QtBootstrap.cmake`、`cmake/CompilerWarnings.cmake`
- `src/core/`（`chroma_core` INTERFACE，注入 `CHROMA_VERSION`）
- `src/app/`：`main.cpp`、`MainWindow`、`ViewportWidget`（原生 HWND 宿主，M1 交接点）
- `tests/`：`unit.smoke`、`app.selftest`（无交互启动 1.2s 后退出）
- `Docs/BUILD.md` 按实测环境重写；`Docs/ROADMAP.md` M0 关闭

**验证**

- `cmake --preset windows-x64-debug` → 配置成功，Qt 6.11.1 解析到 `D:/Applications/QtCreator/6.11.1/msvc2022_64`
- `cmake --build --preset windows-x64-debug` → 成功（VS2026 18.8 / MSVC 19.51 链接 Qt 6.11.1 无 ABI 问题）
- `ctest --preset windows-x64-debug` → **2/2 通过**
- D3D12 能力探针：Intel UHD，特性等级 **12_1**，调试层可用；`R10G10B10A2_UNORM` 交换链支持 SDR 与 HDR10 色彩空间；输出 `BitsPerColor = 8`

**未决 / 风险**

1. Skia 源码构建仍未开始，且 Python 3.14 可能不被 depot_tools 支持 → M3 前需单独装 3.11/3.12。
2. Agent 沙箱禁止 libuv 派生（AUTOMOC 失败），Agent 侧构建需放宽沙箱；本机普通终端不受影响。
3. 输出链路只有 8 bpc：10-bit 交换链的收益需要靠抖动实测证明，否则按 ADR-0007 回退到 8-bit 方案。
4. `Tests` 里没有真正的色彩单测，`unit.smoke` 是占位。

**踩坑**

- `CreateSwapChainForHwnd` 在 D3D12 下第一个参数必须是 `ID3D12CommandQueue`，传 `ID3D12Device` 返回 `0x887A0001`。已在 [BUILD.md](BUILD.md) §8 记录。
- MSVC 未加 `/utf-8` 时，中文字符串字面量报 `文本后缀"鍚"无效`。
- 用 PowerShell 5.1 的 `Get-Content -Raw` + `Set-Content` 改中文源文件会把 UTF-8 读成 ANSI 并写坏；**改文件一律走编辑工具，不用 shell 重写**。

**下次第一件事**：M1 —— 建 `src/gfx/` 的 Device/Queue/SwapChain/Fence 薄封装与 `IRenderer`，在 `ViewportWidget` 上画出第一张纯色图，并用调试层确认零对象泄漏。

### 003 · 2026-11-05 · 迁入 HDR 测试图 + M1 上屏

**目标**：把 HDR 测试图与生成工具迁进仓库，并用 D3D12 把这张图显示到屏幕上（暂不做色彩管理）。

**决策**

- 工具放 `tools/`、产物放 `assets/test-images/`，与 [CONVENTIONS §7](CONVENTIONS.md) 的资源约定一致；生成器的 `--out` 由命令行控制，不改脚本逻辑。
- **M1 不做任何颜色转换**：PQ 码值原样送进 `R10G10B10A2_UNORM` 交换链，观感等同 8-bit 预览图。这是阶段性结果，不是缺陷。
- 加载时**还原存储约定**（`value16 = code10 << 6` → 按 `code/1023` 归一化），否则满量程处会差 1 个码值（9.6e-4）。
- HLSL 暂时**运行时编译**（`D3DCompileFromFile`），优先迭代速度；M2 再决定是否改离线产物。
- `src/` 按架构分层拆成 `chroma_gfx`（头文件）/ `chroma_media` / `chroma_render` 三个目标。

**产出**

- `tools/gen_ptmr.py`、`tools/README.md`
- `assets/test-images/`：主图（16-bit PNG）、8-bit 预览、CSV 对照表、`assets/README.md`
- `src/gfx/D3D12Common.h`、`src/media/ImageSource.{h,cpp}`、`src/render/IRenderer.h`、`src/render/D3D12Renderer.{h,cpp}`
- `shaders/image.hlsl`（全屏三角形 + contain-fit 采样）
- `ViewportWidget` 接 `IRenderer`；`MainWindow` 状态栏显示图像与后端信息；`main` 支持 `--image` / `--selftest`

**验证**

- 构建通过（VS2026 / MSVC 19.51）
- 启动日志：`[media] 载入图像 … 源位深 16 (10-bit 还原)`；`[gfx] backend = D3D12 (native) swapchain = 1600 x 975 dpr = 1.25`
- **截图确认**：窗口内显示 8 条 ramp（灰→红→绿→蓝→青→黄→品→灰），左暗右亮，方形图左右留边居中——与预期一致

**未决 / 风险**

1. 窗口 resize / 拖动 / 最大化未系统验证（`ResizeBuffers` 代码路径已写，但没跑过）。
2. 未调用 `IDXGIDebug::ReportLiveObjects`，D3D12 对象泄漏未检查。
3. `dpr = 1.25` 下物理像素与逻辑像素的换算只在本机验证过。
4. 色彩管线（M2）尚未开始；`COLOR_SPEC.md` 的锚点测试也还没有对应代码。

**踩坑**

- `std::shared_ptr` 被 `std::move` 后继续解引用 → 进程启动即 `0xC0000005`，且控制台没有任何输出。教训：崩溃点在第一个日志之前时，先怀疑"刚 move 过什么"。
- 10-bit 判定最初检查了所有通道，但 WIC 的 alpha 恒为 `0xFFFF`（低 6 位 = `0x3F`），使判据永假 → 只检查 RGB。
- 用 PowerShell 的 `Start-Process` 起 GUI 程序时，子进程继承了 Agent 工具的输出管道，导致工具调用一直不返回 → 必须重定向子进程 stdout/stderr 到文件。

**下次第一件事**：M1 收尾——加 `ReportLiveObjects` 退出检查，并手动验证 resize/最大化；然后进 M2 色彩管线（CPU 参考实现 + SPEC §8 锚点测试先行）。

