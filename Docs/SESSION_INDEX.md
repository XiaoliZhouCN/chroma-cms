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
