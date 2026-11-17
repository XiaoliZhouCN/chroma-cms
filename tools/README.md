# tools · 开发说明

## 这个目录是什么
开发/测试辅助工具：不随 app 发布，不被 `src/` 反向依赖。

## 文件与职责
| 路径 | 职责 |
|---|---|
| `doc-check/doc_check.cpp` | 目录文档规范检查（AGENTS/README/front-matter/链接） |
| `chroma-probe/main.cpp` | 文件容器/元数据探测 CLI（JSON 输出） |
| `scripts/gen-corpus.py` | 生成 `tests/corpus` 小样本（开发依赖 Pillow） |
| `scripts/fetch-corpus.ps1` | 下载外部大样本并校验 SHA-256（当前列表为空） |

## 依赖关系
- 允许依赖：C++20 标准库。
- 禁止：Qt、gfx/render/color 的实现细节（工具应尽量自包含）。
- 被依赖：仅 ctest / CI。

## 构建与测试
- target：`doc_check`、`chroma-probe`
- ctest：`docs.structure`

```powershell
cmake --build --preset windows-x64-debug --target doc_check
ctest --preset windows-x64-debug -R docs.structure
```

## 怎么加新工具
1. 新建 `tools/<name>/<name>.cpp` 与 `tools/<name>/README.md`（说明用法与输出）。
2. 在 `tools/CMakeLists.txt` 注册 `add_executable(<name> ...)`。
3. 需要门禁的在 `tests/CMakeLists.txt` 增加 `add_test`。

## 相关文档
`AGENTS.md` §12（目录规范）、`docs/decisions/0001-directory-docs-convention.md`
