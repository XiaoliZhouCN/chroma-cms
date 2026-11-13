# docs/templates · 目录文档模板

## 这个目录是什么
新建目录时直接复制这里的模板，保证三件套格式一致、doc-check 可通过。

## 文件
| 文件 | 用途 |
|---|---|
| `AGENTS.directory.template.md` | 子目录 AGENTS.md 模板（含 front-matter 与占位写法） |
| `README.directory.template.md` | 子目录 README.md 模板 |

## 规则
- 复制后必须替换 `<目录>`、`<scope>`、`<extends>`、`<日期>`；
- 无本地规则时保留"无本地扩展"占位段；
- 模板本身不参与 doc-check 的构建目录检查（docs 目录只要求 README）。

## 相关文档
`docs/decisions/0001-directory-docs-convention.md`
