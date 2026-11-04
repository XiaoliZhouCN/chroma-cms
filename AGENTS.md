# AGENTS.md — ChromaCMS

> 本文件是给 Agent 的**硬约束清单**，只放"必须/禁止"，不放解释。
> 需要背景与理由时读 [Docs/INDEX.md](Docs/INDEX.md)。
> 子目录中的 `AGENTS.md` 在本文件基础上**追加**规则，不得与之冲突。

## 0. 一句话

Windows 图像 / 视频显示工具；用 D3D12 原生与 Skia(D3D12) 两条渲染路径做**自研色彩管理**，目的是学习渲染链路与色彩管理。

## 1. 硬规则（违反即返工）

1. `main` **永远保持为空**，禁止直接提交；一切工作在 `develop` 及其特性分支上。
2. **禁止使用框架自带的色彩管理**：不用 `SkColorSpace` 做转换、不依赖 Windows ICM 自动映射、不靠 DXGI 隐式校正。
   颜色数学只允许存在于 `shaders/`（HLSL / SkSL）与 `src/color/`（CPU 参考实现）中。
3. 颜色数学必须**一份规范、两套实现**：HLSL 与 SKSL 严格对齐 [Docs/COLOR_SPEC.md](Docs/COLOR_SPEC.md)；
   改一侧必须同步另一侧，并跑双后端一致性测试。
4. 有取舍的技术选择，先写 ADR（[Docs/adr/](Docs/adr/)）；哪怕 `status: proposed` 也必须先落文档再写代码。
5. 代码变更必须同步受影响文档，**同一个提交内完成**；文档改动与代码改动同等评审。
6. 提交前必须通过 `scripts\check-docs.cmd`，退出码非 0 不得提交。
7. 不提交空文件、空目录、只含标题的占位文档。

## 2. 写授权矩阵

| 范围 | 档位 |
|---|---|
| `temp_and_scratch/` 下任何临时文件 | 自由 |
| `src/`、`shaders/`、`tests/`、`assets/` 新增与修改 | 自由 |
| `Docs/` 下非 ADR 文档更新（须过 check-docs） | 自由 |
| 新建 ADR（`status: proposed`） | 自由 |
| 修改本文件、`Docs/CONVENTIONS.md` | 需人工确认 |
| 修改已 `accepted` 的 ADR、修改 `Docs/COLOR_SPEC.md` 的规范数值 | 需人工确认 |
| 修改 `scripts/`、CI、`.gitignore`、`.gitattributes` | 需人工确认 |
| 删除文档或目录、重写 `SESSION_INDEX` 历史、跳过 check-docs | 禁止 |

遇到"需人工确认"的档位：**停下来提问，不要猜**。

## 3. 按场景读文档（不要通读全部）

| 你要做的事 | 先读 |
|---|---|
| 第一次进入本仓库 | [README.md](README.md) → [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md) |
| 改渲染后端 / 呈现 | [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md) → [ADR-0003](Docs/adr/0003-dual-render-backend.md) |
| 动任何颜色相关代码 | [Docs/COLOR_MANAGEMENT.md](Docs/COLOR_MANAGEMENT.md) → [Docs/COLOR_SPEC.md](Docs/COLOR_SPEC.md) → [Docs/TESTING.md](Docs/TESTING.md) |
| 加依赖 / 动构建 | [Docs/BUILD.md](Docs/BUILD.md) → [ADR-0006](Docs/adr/0006-cmake-vcpkg-source-built-skia.md) |
| 做技术选型 | [Docs/adr/AGENTS.md](Docs/adr/AGENTS.md) → [Docs/adr/TEMPLATE.md](Docs/adr/TEMPLATE.md) |
| 开工 / 收工 | [Docs/AGENT_WORKFLOW.md](Docs/AGENT_WORKFLOW.md) |
| 不知道读哪个 | [Docs/INDEX.md](Docs/INDEX.md) |

## 4. 完成定义（DoD）

- [ ] 构建通过，`ctest` 全绿
- [ ] 涉及渲染的改动：双后端一致性测试通过
- [ ] `scripts\check-docs.cmd` 退出码 0
- [ ] 受影响的文档已同步；产生决策的已写 ADR
- [ ] [Docs/SESSION_INDEX.md](Docs/SESSION_INDEX.md) 已追加本次会话条目
- [ ] 提交信息符合 Conventional Commits，落在 `develop` 或 `feat/*` 分支
