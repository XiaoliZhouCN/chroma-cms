# ADR 目录规则

> 本文件对本目录**追加**规则；不覆盖根 [AGENTS.md](../../AGENTS.md)。

## 1. ADR 是追加式台账

- 一个决策一份 ADR，编号 `NNNN-kebab-case.md`，四位**连续**编号，不跳号、不复用。
- 编号只增不减；删掉 ADR 视为破坏历史，禁止。

## 2. 已接受的 ADR 不可修改

`status: accepted` 之后，**正文冻结**。允许改的只有：

- front-matter 的 `status`（改成 `superseded` / `deprecated`）与 `updated`；
- 在文末追加 `## 后续` 小节，写"被 XXXX 取代/部分修正"。

要改变已接受的决策：**新建一份 ADR**，在它的 §背景 里引用旧编号，并把旧 ADR 标 `superseded`。

## 3. 状态机

```
draft ──→ proposed ──→ accepted ──→ superseded
              │              └────→ deprecated
              └─→ (被否决: rejected，正文保留理由)
```

- `proposed`：已有倾向但未拍板，或依赖未验证的前提。**可以先写代码验证，但不得对外声称是最终方案。**
- `accepted`：当前生效。实现必须遵循。
- `rejected`：记录被否决的方案与原因，防止重复讨论。

## 4. 必须写 ADR 的情形

1. 引入或替换依赖；
2. 改变分层、接口契约、线程模型；
3. 改变颜色数学（公式、工作空间、位深、交换链格式）；
4. 任何"当时争论过"的选择——争论本身就是信号。

不需要 ADR：命名、格式、局部重构、修 bug。

## 5. 格式

必须使用 [TEMPLATE.md](TEMPLATE.md)，且必须包含：**背景 / 决策 / 后果 / 替代方案**。
缺"替代方案"的 ADR 视为无效——没有对比的决策无法在未来被重新评估。

## 6. 登记

新建 ADR 后，必须在 [../INDEX.md](../INDEX.md) §3 的台账表加一行（`check-docs` 会检查编号连续性与登记完整性）。
