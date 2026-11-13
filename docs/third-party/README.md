# docs/third-party · 第三方依赖

## 这个目录是什么
第三方依赖清单：来源、构建参数、产物布局、消费约束（被 CMake 模块引用）。

## 规则
- 新增依赖前必须问用户（见根 `AGENTS.md` §1.8）；
- 每份清单必须记录：版本/commit、构建配置、被谁链接、Release/Debug 约束；
- 二进制不入库（`external/` 已 gitignore）；复现步骤必须可执行。

## 当前条目
| 文件 | 依赖 |
|---|---|
| `skia.md` | Skia（预编译 Ganesh，Windows D3D12 / macOS Metal） |

## 相关文档
`cmake/FindSkia.cmake`、`docs/handoff/09-skia-backend.md`
