# AGENTS.md — ChromaCMS 协作指南（全平台通用）

> 本文件是**所有 AI 协作 agent 的唯一入口**。先读本文件，再按 §2 的顺序读文档。
> **仓库方向以 [`docs/ROADMAP.md`](docs/ROADMAP.md) 为准**；本文件只管"怎么协作"。

---

## 0. 项目是什么

ChromaCMS 是一个**色彩管理学习 + 工程实践**仓库，双主线：

1. **理论学习**：把"文件字节 → 屏幕像素"的完整色彩管理链路（容器/元数据/解码/传递函数/色域/色调映射/系统合成/显示）学透；
2. **工程实践**：实现一个可长期维护、精确、多格式覆盖的跨平台色彩管理渲染引擎（三后端 + CPU oracle + 测试体系）。

- 工作分支：**`practice`**
- `develop`：完整参考实现（**与 practice 无共同祖先**，只能按文件借用，**不要 merge**）
- `main`：空

**一次实践 = 一篇理论文档 + 一个可运行实验 + 一组测试断言**，三者缺一不算完成。

---

## 1. 最重要的规则

1. **实现代码由 agent 编写**（`.cpp` / `.hpp` / `.mm` / `.hlsl` / `.metal` / `.sksl`）。
   用户负责方向、关键决策与验收。
2. **每次改动必须给出报告**：`文件:行` 锚点 + 改了什么 + **为什么** + 验证结果。
   不允许"顺手重构/静默重排"，不允许改与任务无关的代码。
3. **交付前必须自验**：构建（双平台阶段要双端）+ `ctest` + 运行/截图。
   不允许"应该可以"式交付。
4. **提交**：agent 在**本地** commit（按可验证里程碑）；**push 由用户执行**。
5. **远程仓库**：**未经用户明确授权，禁止任何远程写操作**（push / tag / 建 PR / 删除远程分支等）。
6. 头文件一律 **`.hpp`**。
7. 用中文交流；代码注释用中文；学习型注释沿用 `// Q: ...` / `// A: ...` 的问答风格。
8. 新增第三方依赖前必须问用户（已批准：FFmpeg、预编译 Skia、**lcms2 仅用于 ICC 解析/交叉校验**）。
9. 学习内容变化时，同步更新对应 `docs/color/` 文章与测试断言。

---

## 2. 先读哪些文档

| 顺序 | 文件 | 内容 |
|---|---|---|
| 1 | [`docs/ROADMAP.md`](docs/ROADMAP.md) | **仓库方向唯一来源**：定位、架构、阶段、验收、协作规则 |
| 2 | `AGENTS.md`（本文件） | 协作规则与入口 |
| 3 | [`docs/color/00-index.md`](docs/color/00-index.md) | 理论学习手册（L0–L5、文章索引、实验与断言映射） |
| 4 | [`docs/testing/00-index.md`](docs/testing/00-index.md) | 测试手册（unit/integration/e2e、阈值、资产策略） |
| 5 | [`docs/handoff/`](docs/handoff/) | 工程交接与历史方案（**历史记录，不再是方向来源**） |
| 6 | [`docs/decisions/`](docs/decisions/) | 决策记录（ADR-lite；规则/架构变化必须登记） |
| 7 | 目标目录下的 `AGENTS.md` + `README.md` | 该目录的本地规则与开发说明（按需读取） |

历史背景：`docs/handoff/01`–`10` 记录了 Windows/macOS 两端的早期开发与重构过程，可作为代码地图与踩坑参考。

---

## 3. 平台现状（2026-11-13）

| 平台 | 后端 | 状态 |
|---|---|---|
| Windows | 手写 D3D12 | 设备/交换链/三角形/纹理/FFmpeg 解码/PQ 单 pass 色彩管线/交互（A/D/Q/E/R）✅ |
| Windows | Skia（Ganesh D3D12） | 第三后端 ✅（`SkiaRenderer` + `SkiaGpuBackendD3D12`） |
| macOS | 手写 Metal | M1–M4 + 交互 ✅；`MetalContext`/`MetalRenderer` 拆分 ✅ |
| macOS | Skia（Ganesh Metal） | 第三后端 ✅（`SkiaGpuBackendMetal`） |

**当前显示方案的局限**：只有单图、单目标色域（Windows scRGB / mac Display-P3 刻意不同）、无容器/元数据层、无测试语料。
新方向由 `docs/ROADMAP.md` 定义：完整链路 + 多格式 + CPU oracle + 三级测试 + **双视图对照**。

**双视图对照（后续所有验收的标准画面）**：

```
左：裸原生 API（手写 D3D12 / 手写 Metal）
右：Skia + 当前平台后端
Tab：左 / 右 / 叠图（差值或透明度混合） + 元数据面板 / 管线开关
```

---

## 4. 常用命令

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

macOS（详见 `docs/handoff/05`）：

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

注意：`skia` 预设**只生成 Release 配置**（预编译 Skia 不兼容 Debug CRT / 迭代器调试级别）。

---

## 5. 已踩过的坑（避免重复踩）

| 平台 | 坑 | 处理 |
|---|---|---|
| 通用 | 改 `.sksl`/`.hlsl`/`.metal` 后输出目录仍是旧文件 | `POST_BUILD` 才拷贝；重新构建或手动拷到输出目录 `shaders/` |
| 通用 | Skia 失败路径静默黑屏 | 检查 `makeShader` 返回值与 `effect->uniformSize() == sizeof(struct)` |
| 通用 | SkSL 方言与 HLSL/MSL 不同 | 向量/标量不能混参（`pow(float3, float3)`）、局部变量不能随便 `const` |
| 通用 | Skia `float3x3` uniform 布局 | 9 个 float 紧凑排列（36 B），不是 std140 的 48 B |
| 通用 | `makeShader` 的 `localMatrix` 方向 | 需传"设备像素→图片像素"映射的**逆矩阵**；并 `clipRect` 到视口 |
| 通用 | 截图 ≠ 屏幕 | 系统色彩管理会处理截图；数值断言用 CPU oracle，截图只作人工证据 |
| Windows | PowerShell 执行策略 AllSigned | `powershell -NoProfile -ExecutionPolicy Bypass -File xxx.ps1` |
| Windows | `Start-Process` 启动 GUI app 会挂住工具调用 | 必须重定向 stdout/stderr 到文件 |
| Windows | 用 `Get-Content -Raw` + `Set-Content` 改中文文件会乱码 | 用 `[System.IO.File]::ReadAllText/WriteAllText` + UTF8 无 BOM |
| macOS | 截图需要"屏幕录制"权限 | 系统设置 → 隐私与安全性 → 屏幕录制，给终端授权 |
| macOS | Qt 来自 Homebrew 时 `find_package(Qt6)` 需要前缀 | 设置 `QT_ROOT`（预设已用 `$penv{QT_ROOT}`） |
| macOS | `INTERFACE` 库上用了 `PUBLIC`/`PRIVATE` 属性 → configure 失败 | INTERFACE 库只能用 INTERFACE 属性 |
| macOS | `MetalContext.hpp` include `<Metal/Metal.h>` 会让纯 CXX 的工厂编不过 | 头文件纯 C++ + pimpl + `void*` 句柄，ObjC 只在 `.mm` |

---

## 6. 交付自检清单

- [ ] 构建绿（会受影响的平台都要构建）
- [ ] `ctest` 通过（新增功能必须带测试；含 `docs.structure` 目录文档门禁）
- [ ] 运行过，并给出双视图截图或关键日志
- [ ] 改动报告：锚点 + 改了什么 + 为什么 + 验证结果
- [ ] 没有改与任务无关的代码/格式化
- [ ] 学习内容变化时，`docs/color/` 与测试断言同步更新
- [ ] `git status` 检查（不把 `external/`、`build/`、`temp_and_scratch/` 混进提交）

---

## 7. CMakeLists 分层规范（2026-11-11，保留）

完整说明见 `docs/handoff/10-skia-refactor-cmake.md` §3。要点：

1. `src` / `tests` / `tools` 下每个构建目录都有 `CMakeLists.txt` 与三件套文档（§12）。
2. 父目录创建 target 和公共属性，子目录只 `target_sources()` / 追加 links、options。
3. 平台目录只在对应平台 `add_subdirectory()`：
   `gfx/d3d12`、`gfx/metal`、`render/d3d12`、`render/metal`。
4. `render/skia` 是可选独立 target `chroma_render_skia`；
   父目录只在 `if(TARGET chroma_render_skia)` 时链接并定义 `CHROMA_HAS_SKIA`。
5. `cmake/` 只放可复用模块：`QtBootstrap.cmake`、`FindFFmpeg.cmake`、`FindSkia.cmake`。
6. INTERFACE target 只能用 INTERFACE 属性；`chroma_set_warnings()` 只对真实 target 调用。
7. 叶子目录不要重复 `add_library()`，也不要在父目录直接列叶子源文件。

---

## 8. 学习文档规范（`docs/color/`）

每篇固定五段：**概念 → 公式/数值例子 → 本仓库实验（可运行命令）→ 测试断言 → 常见误解**。

- 文章编号与分层见 `docs/color/00-index.md`；
- 文章里的公式与代码必须一致（以 `src/color` 的实现为准）；
- 文章状态（计划/编写中/已发布）在索引中维护；
- 数值例子必须可手算或可用 `tools/` 复现。

---

## 9. 测试规范（`docs/testing/`）

- 三层：unit（数学/解析）、integration（文件→管线→像素）、e2e（三后端 + 双视图）；
- 判据：unit 绝对误差 1e-6~1e-5；integration SDR `ΔE2000 < 1.0`、HDR `ΔE ITP < 1.0`；e2e 后端间/对 oracle 在阈值内；
- 参考必须来自 **CPU oracle**，不能用 GPU 输出当参考；
- 阈值调整必须单独说明原因，不允许"调阈值让测试过"；
- 小样本入库（<10 MB 总量），大文件走下载脚本并校验哈希。

---

## 10. 提交与远程（2026-11-13 变更）

| 项 | 规则 |
|---|---|
| commit | agent 按可验证里程碑**本地提交**；提交信息遵守仓库风格 |
| push | **由用户执行** |
| 远程操作 | **未经明确授权，禁止 push / tag / PR / 删除远程分支等任何远程写操作** |
| 工作区 | 可直接在工作区开发；实验验证放 `temp_and_scratch/`（gitignore） |
| 回滚 | 用户可随时要求回滚；agent 不得用破坏性 git 命令（`reset --hard` 等）覆盖用户改动 |

---

## 11. 与旧文档的关系

- `docs/handoff/01`–`10`：历史记录（早期协作模式：用户写实现、agent 给逐处改法）。
  其中**技术原理**仍有效，**协作规则**已被本文件取代。
- `docs/ROADMAP.md`：方向与阶段唯一来源，优先于所有历史文档。

---

## 12. 目录规范（2026-11-13）

### 12.1 三件套范围

| 目录类型 | AGENTS.md | README.md | CMakeLists.txt |
|---|---|---|---|
| 构建目录：`src/`、`tests/`、`tools/` 下每个含 `CMakeLists.txt` 的目录 | 必须 | 必须 | 必须（已有） |
| `docs/` 及子目录 | 可选（仅当有本地规则） | 必须 | 不加（不参与构建） |
| 根目录 | 必须 | 必须 | 必须（已有） |

### 12.2 分层继承与冲突

1. **就近优先**：同一事项，子目录 `AGENTS.md` > 父目录 > 根。
2. **硬规则不可覆写**：§1（分工、验证、commit/push/远程、`.hpp`、依赖审批）任何子目录不得放松。
3. **只能更严格**：子目录可以增加约束，不能减少验证或放宽阈值。
4. **冲突即停**：无法用"更严格"解释的冲突，agent 必须停下来问，不得自行裁决。
5. **占位合法**：无本地扩展时必须写"无本地扩展"，禁止空文件。

### 12.3 AGENTS.md front-matter（机器可校验）

```yaml
---
extends: ../AGENTS.md        # 相对本文件的父级 AGENTS 路径，必须存在
scope: src/color             # 相对仓库根的目录路径，必须与本目录一致
owner: agent                 # 维护者
last_verified: 2026-11-13    # 最近一次人工/AI 复核日期
---
```

正文必须包含 `## 继承`，并包含 `## 本地约定`（有扩展）或"无本地扩展"字样（占位）。

### 12.4 README.md

必须包含：本目录是什么、文件与职责、依赖关系（允许/禁止/被依赖）、构建与测试（target/ctest）、
怎么加新东西、相关文档。

### 12.5 校验

`tools/doc-check` 以 ctest `docs.structure` 运行，检查：

- 必需文件存在；
- front-matter 完整、`extends` 与 `scope` 正确；
- 占位/本地约定声明；
- `docs/` 规定目录的 README；
- 所有被检查 Markdown 的相对链接存在。

失败即阻断提交。模板见 `docs/templates/`，决策记录见 `docs/decisions/0001-directory-docs-convention.md`。

## 13. 决策记录（2026-11-13）

- 影响长期协作/架构/规范的决定写入 `docs/decisions/NNNN-<kebab-title>.md`；
- 固定结构：背景 → 决定 → 后果 → 规则；
- 生效后根 `AGENTS.md` / `ROADMAP.md` 必须引用编号；
- 被推翻的决策保留，并标注"已废止（被 NNNN 取代）"；
- 口头约定不算决策，必须落盘。
