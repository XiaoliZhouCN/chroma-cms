# docs · 文档地图

## 这个目录是什么
仓库文档根。**方向以 `ROADMAP.md` 为准**；本目录只做索引与说明。

## 子目录
| 目录 | 内容 | 规则 |
|---|---|---|
| `ROADMAP.md` | 方向、阶段、验收（唯一来源） | 变更需在 `decisions/` 登记 |
| `ARCHITECTURE_DESIGN.md` | 人工维护架构决策与设计边界 | 人工维护，AI 只读 |
| `ARCHITECTURE_IMPLEMENTATION.md` | 对 `ARCHITECTURE_DESIGN.md` 的实现补充 | AI 根据代码和架构决策维护实现事实 |
| `features/` | 单个功能点的行为、边界与验收 | 一功能一文件；见 `features/README.md` |
| `_templates/` | 文档创建模板 | 创建对应文档时使用对应模板 |
| `color/` | 色彩管理理论学习手册 | 文章五段式，见 `00-index.md` |
| `testing/` | 测试方法手册 | 三层测试与阈值 |
| `handoff/` | 历史交接记录 | 只读参考，不再更新方向 |
| `third-party/` | 第三方依赖清单（Skia/FFmpeg/lcms2） | 记录来源/构建/消费约束 |
| `decisions/` | 决策记录（ADR-lite） | 规则/架构变化必须登记 |

## 目录文档规则
- 本目录及子目录：**必须有 README.md**；
- `AGENTS.md` 可选（只有存在本地规则时才写）；
- **不要求 CMakeLists.txt**（docs 不参与构建）；
- 所有相对链接必须有效（`ctest -R docs.structure` 校验）。

## 相关文档
根 `README.md`、根 `AGENTS.md` §12、`docs/decisions/0001-directory-docs-convention.md`

## 常用入口
- [功能说明索引](features/README.md)
- [Probe Card Capture](features/probe_card_capture.md)
