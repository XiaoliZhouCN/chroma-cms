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

