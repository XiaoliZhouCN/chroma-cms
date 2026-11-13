# src/core · 开发说明

## 这个目录是什么
最小公共设施（当前只有版本号）。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `Version.hpp` | 项目版本号宏/常量 |

## 依赖关系
- 允许：C++ 标准库。
- 被依赖：`app`、`tests/unit`。

## 构建与测试
- target：`chroma_core`（INTERFACE）
- 无独立测试；由 `unit.smoke` 覆盖。

## 相关文档
`src/README.md`、根 `AGENTS.md`
