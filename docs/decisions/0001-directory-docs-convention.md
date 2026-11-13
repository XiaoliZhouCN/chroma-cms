# 0001 · 目录文档规范（三件套 + 继承 + doc-check）

## 背景
项目进入长期人机协作阶段：
- 12 个构建目录里没有一个 README/AGENTS，agent 改代码只能靠猜"新文件放哪、target 叫什么";
- 规则、方向、平台状态混杂在根 `AGENTS.md`，越写越长，且出现过文档落后于代码；
- 规则在项目中途变更（用户写实现 → agent 写实现）只存在于对话记录里。

## 决定
1. **构建目录三件套**：`src/`、`tests/`、`tools/` 下每个含 `CMakeLists.txt` 的目录必须有
   `AGENTS.md` + `README.md`（CMakeLists 已有）。
2. **docs 目录**：必须有 `README.md`；`AGENTS.md` 可选；**不加** `CMakeLists.txt`。
3. **AGENTS.md 分层继承**：
   - 子目录 AGENTS 以父目录为基准，只写 delta（扩展/覆盖）；
   - 就近优先；根 §1 硬规则不可覆写；子目录只能更严格；
   - 无扩展必须写"无本地扩展"占位。
4. **YAML front-matter**：`extends` / `scope` / `owner` / `last_verified`，用于机器校验。
5. **README.md**：说明"本目录是什么、文件职责、依赖关系、构建与测试、怎么加新东西、相关文档"。
6. **doc-check 门禁**：`tools/doc-check` 作为 ctest `docs.structure`，校验必需文件、front-matter、
   占位声明、docs README 与相对链接；失败阻断提交。
7. **变更原子性**：目录结构/构建/命令变化必须在同一 commit 更新对应文档。

## 后果
- 新目录/新模块的准入门槛：三件套 + doc-check 通过；
- agent 的加载协议：根 AGENTS → ROADMAP → 目录 AGENTS → 目录 README → CMakeLists（按需）；
- 文档过期风险由 `last_verified` + doc-check 警告兜底；
- 历史目录（handoff）保留，但不再承担方向职责。

## 规则
- 模板：`docs/templates/AGENTS.directory.template.md`、`docs/templates/README.directory.template.md`
- 检查器：`tools/doc-check/doc_check.cpp`
- 例外流程：任何例外必须在 `docs/decisions/` 新增记录，不得口头约定。
