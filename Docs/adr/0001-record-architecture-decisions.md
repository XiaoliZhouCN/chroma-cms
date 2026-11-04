---
status: accepted
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 0001. 用 ADR 记录架构决策

- **状态**：accepted
- **日期**：2026-11-04
- **决策者**：XiaoliZhouCN

## 背景

本项目同时是**学习项目**和**Agent 协作实验**。两个属性都要求"为什么"必须被保留：

- 学习：只看代码只能知道"怎么做"，不知道"当时否掉了什么"，学不到取舍；
- Agent 协作：下一次会话没有"当时为什么"的上下文，就会重新发明或推翻已有决定。

代码 diff 记录"改了什么"，不记录"为什么没选另一条路"。

## 决策

采用 ADR（Architecture Decision Record）：

1. 位置 `Docs/adr/`，文件名 `NNNN-kebab-case.md`，四位连续编号；
2. 必须包含 **背景 / 决策 / 后果 / 替代方案** 四节；
3. 状态机 `draft → proposed → accepted → superseded | deprecated`，`accepted` 后正文冻结；
4. 改变已接受决策的唯一方式是**新建 ADR 并标记旧 ADR 为 superseded**；
5. 所有 ADR 必须在 [../INDEX.md](../INDEX.md) 登记，由 `check-docs` 强制。

完整规则见 [AGENTS.md](AGENTS.md)。

## 后果

### 正面

- 决策可追溯，任何人（含 Agent）能读到"为什么不是另一个方案"；
- 免去反复讨论同一问题；
- 学习价值：ADR 本身就是渲染与色彩管理知识的沉淀。

### 负面 / 代价

- 写 ADR 有成本，容易被跳过——用 `check-docs` 与 DoD 兜底；
- 小决策也写 ADR 会造成噪音——用 [AGENTS.md](AGENTS.md) §4 的边界约束。

### 需要跟进

- [ ] 当 ADR 数量超过 20 时，评估是否按 `render/`、`color/` 子目录分组。

## 替代方案

| 方案 | 否决理由 |
|---|---|
| 只在 README 里写"设计说明" | 无时间戳、无状态、无替代方案，随代码一起腐化 |
| 依赖 commit message 承载决策 | 找不到、不可检索、无法标记 superseded |
| 用外部 wiki | 与代码分离，Agent 无法在仓库内读到，必然不同步 |

## 参考

- Michael Nygard, *Documenting Architecture Decisions*（2011）
