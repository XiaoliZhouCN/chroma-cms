# ChromaCMS · practice

> **色彩管理学习 + 工程实践仓库。**
> 目标：把"文件字节 → 屏幕像素"的完整色彩管理链路（容器/元数据/解码/传递函数/色域/色调映射/系统合成/显示）
> 学透，并实现一个可长期维护、精确、多格式覆盖的跨平台色彩管理渲染引擎。

## 双主线

| 主线 | 内容 |
|---|---|
| 理论学习 | `docs/color/`：L0–L5 分层手册，每篇 = 概念 + 数值例子 + 可运行实验 + 测试断言 |
| 工程实践 | CPU 色彩管理核心（oracle）+ 三后端（手写 D3D12 / 手写 Metal / Skia）+ CLI 工具 + 三级测试 |

**仓库方向唯一来源： [`docs/ROADMAP.md`](docs/ROADMAP.md)。**

## 当前状态（2026-11-17）

- **P0 完成（Windows 已验收）**：`src/color` CPU oracle、`src/io` 容器探测、
  `tools/chroma-probe`、`tests/corpus` + `unit.container_probe`、`docs/color/01`、
  目录文档规范（`docs.structure` 门禁）、双视图骨架；
- **双视图**：左 = 手写原生（D3D12/Metal），右 = Skia；默认并排，Tab 支持 左 / 右 / 并排；
  叠图需要离屏合成，放到 P1；
- **待办**：macOS 侧 P0 验收（构建 + ctest + 双视图），随后进入 P1（SDR/HDR/双层三轨 + ICC）；
- 当前内容仍是单图；ICC 解析/元数据优先级、多格式解码、integration/e2e 尚未接入。

平台现状：

| 平台 | 后端 |
|---|---|
| Windows | 手写 D3D12、Skia（Ganesh D3D12） |
| macOS | 手写 Metal、Skia（Ganesh Metal） |

**验收标准画面（后续统一）**：左右双视图——左：裸原生 API；右：Skia + 当前平台后端；
Tab 支持 左 / 右 / 并排（当前）；叠图对比在 P1 实现（需要离屏合成/差值渲染）。

## 目录

```
src/color/     CPU 色彩管理（oracle，已落地）
src/io/        容器探测 + 色彩元数据存在性（已落地；ICC 内容解析 P1）
src/media/     FFmpeg 图像解码（当前 RGBA16；Frame/ColorDescription P1）
src/gfx/       原生上下文（D3D12 / Metal）
src/render/    三后端（手写 D3D12 / 手写 Metal / Skia）
src/app/       Qt UI（双视图骨架：左 / 右 / 并排）
tools/         chroma-probe、doc-check、scripts（已落地）；chroma-bench、pattern-gen 计划
tests/         unit（6 项已接入）+ corpus（已落地）；integration / e2e / golden 计划
docs/          ROADMAP + color + testing + handoff + decisions + third-party
```

## 文档入口

- [`docs/ROADMAP.md`](docs/ROADMAP.md)：方向、架构、阶段、验收
- [`AGENTS.md`](AGENTS.md)：协作规则（agent 入口）
- [`docs/color/00-index.md`](docs/color/00-index.md)：色彩管理学习手册
- [`docs/testing/00-index.md`](docs/testing/00-index.md)：测试手册
- [`docs/handoff/README.md`](docs/handoff/README.md)：历史交接记录
- [`docs/decisions/README.md`](docs/decisions/README.md)：决策记录（ADR-lite）

## 构建

Windows（PowerShell）：

```powershell
cmake --preset windows-x64-debug -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
.\build\windows-x64-debug\Debug\ChromaCMS.exe

# Skia 后端（仅 Release）
cmake --preset windows-x64-skia -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build --preset windows-x64-skia
.\build\windows-x64-skia\Release\ChromaCMS.exe
```

macOS：

```bash
export QT_ROOT=$(brew --prefix qt)
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
ctest --preset macos-arm64-debug
open build/macos-arm64-debug/ChromaCMS.app

# Skia 后端（仅 Release）
cmake --preset macos-arm64-skia
cmake --build --preset macos-arm64-skia
```

## 协作（2026-11-13 起）

- 实现代码由 **agent 编写**；agent 每次改动给出锚点 + 改了什么 + 为什么 + 验证结果。
- agent **本地 commit**；**push 由用户执行**；未经明确授权禁止任何远程写操作。
- 每阶段必须 **Windows + macOS 双端验收通过**才能进入下一阶段。
- 新增第三方依赖先问（已批准：FFmpeg、预编译 Skia、lcms2 仅用于 ICC 解析/交叉校验）。
- 目录文档规范：每个构建目录三件套（`AGENTS.md` + `README.md` + `CMakeLists.txt`），
  继承与校验规则见 [`AGENTS.md`](AGENTS.md) §12；`ctest -R docs.structure` 门禁。
- 不要 merge `develop`（与 `practice` 无共同祖先）。
