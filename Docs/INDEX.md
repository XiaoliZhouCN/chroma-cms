---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 文档索引

> **唯一检索入口。** 本仓库每份文档都必须在这里登记，否则 `check-docs` 判为孤儿文档并失败。

## 1. 按需求找文档

| 我想知道 | 读 |
|---|---|
| 这个项目是什么、怎么跑 | [README](../README.md) |
| 规则边界、能改什么不能改什么 | [AGENTS.md](../AGENTS.md) |
| 系统怎么分层、数据怎么流动 | [ARCHITECTURE.md](ARCHITECTURE.md) |
| 代码怎么写、分支怎么开、提交怎么写 | [CONVENTIONS.md](CONVENTIONS.md) |
| 环境怎么装、Skia 怎么编、依赖怎么来 | [BUILD.md](BUILD.md) |
| 颜色到底怎么算（设计意图） | [COLOR_MANAGEMENT.md](COLOR_MANAGEMENT.md) |
| 颜色到底怎么算（规范数值，实现依据） | [COLOR_SPEC.md](COLOR_SPEC.md) |
| 怎么证明颜色算对了 | [TESTING.md](TESTING.md) |
| 接下来做什么、为什么这个顺序 | [ROADMAP.md](ROADMAP.md) |
| 人和 Agent 怎么配合、怎么交接 | [AGENT_WORKFLOW.md](AGENT_WORKFLOW.md) |
| 上次做到哪了、有什么坑 | [SESSION_INDEX.md](SESSION_INDEX.md) |
| 为什么做了某个选择 | [adr/](adr/AGENTS.md) 下对应 ADR |

## 2. 文档清单

| 文档 | 类型 | 变更频率 | 谁维护 |
|---|---|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | 导航 / 契约 | 低（分层变了才改） | 人工评审 |
| [CONVENTIONS.md](CONVENTIONS.md) | 契约 | 低 | 人工确认 |
| [COLOR_MANAGEMENT.md](COLOR_MANAGEMENT.md) | 设计 | 中 | Agent 可改 |
| [COLOR_SPEC.md](COLOR_SPEC.md) | **规范（数值）** | 极低 | 人工确认 |
| [TESTING.md](TESTING.md) | 流程 | 中 | Agent 可改 |
| [BUILD.md](BUILD.md) | 流程 | 中 | Agent 可改 |
| [ROADMAP.md](ROADMAP.md) | 计划 | 高 | Agent 可改 |
| [AGENT_WORKFLOW.md](AGENT_WORKFLOW.md) | 流程 | 低 | 人工确认 |
| [SESSION_INDEX.md](SESSION_INDEX.md) | 台账（追加） | 每会话 | Agent 追加 |
| [adr/](adr/AGENTS.md) | 台账（追加） | 有决策时 | 见 adr/AGENTS.md |

## 3. ADR 台账

| 编号 | 决策 | 状态 |
|---|---|---|
| [0001](adr/0001-record-architecture-decisions.md) | 用 ADR 记录架构决策 | accepted |
| [0002](adr/0002-qt-widgets-native-surface.md) | Qt Widgets 自绘控件承载原生表面 | accepted |
| [0003](adr/0003-dual-render-backend.md) | 双渲染后端共享同一 D3D12 设备 | accepted |
| [0004](adr/0004-self-implemented-color-management.md) | 自研色彩管理，禁用框架自带转换 | accepted |
| [0005](adr/0005-ffmpeg-for-video-decode.md) | 视频解码选 FFmpeg | accepted |
| [0006](adr/0006-cmake-vcpkg-source-built-skia.md) | CMake + vcpkg + 源码构建 Skia | accepted |
| [0007](adr/0007-swapchain-format-and-color-space.md) | 交换链格式与色彩空间 | **proposed** |

模板：[adr/TEMPLATE.md](adr/TEMPLATE.md)

## 4. 其它目录

| 路径 | 内容 |
|---|---|
| [adr/AGENTS.md](adr/AGENTS.md) | ADR 目录的追加规则（追加式、不可改） |
| [templates/SESSION_ENTRY.md](templates/SESSION_ENTRY.md) | 会话记录模板 |
| [../assets/README.md](../assets/README.md) | 测试素材的规格、来源与校验哈希 |
| [../tools/README.md](../tools/README.md) | 离线生成工具（PTMR 测试图） |
| [../instruments/README.md](../instruments/README.md) | 测量工件目录约定 |

## 5. 维护规则

1. 新增文档必须在上表登记；未登记的文档 = 孤儿，`check-docs` 会失败。
2. 每份文档必须有 YAML front-matter（`status` / `owner` / `updated`）。
3. 文档之间只用**相对路径链接**，禁止绝对路径与反斜杠。
4. 一份文档只回答一个问题；超过 300 行考虑拆分。
5. 内容与代码矛盾时，以代码与 ADR 为准，并**当天**修正文档。
