# docs/third-party · 第三方依赖

## 这个目录是什么
第三方依赖清单：来源、构建参数、产物布局、消费约束（被 CMake 模块引用）。

## 规则
- 新增依赖前必须问用户（见根 `AGENTS.md` §1.8）；
- 每份清单必须记录：版本/commit、构建配置、被谁链接、Release/Debug 约束；
- 二进制不入库（`external/` 已 gitignore）；复现步骤必须可执行。

## 当前条目
| 依赖 | 版本/来源 | 使用方式 | 状态 | 明细 |
|---|---|---|---|---|
| Qt 6 | 6.11.1（Qt 安装器） | widgets + AUTOMOC | ✅ 已接入 | 构建参数见 `cmake/QtBootstrap.cmake`；需 `QT_ROOT` |
| FFmpeg | `external/` 预编译（Windows/macOS） | 图像解码；视频 P2 | ✅ 图像解码已接入 | `cmake/FindFFmpeg.cmake` |
| Skia | 预编译 Ganesh（仅 Release） | Skia 后端（双视图右侧） | ✅ 已接入 | `docs/third-party/skia.md` |
| lcms2 | 待定 | ICC 解析/交叉校验 | 已批准，未接入（P1） | 接入前需补清单 |

## 相关文档
`cmake/FindSkia.cmake`、`cmake/FindFFmpeg.cmake`、`docs/handoff/09-skia-backend.md`
