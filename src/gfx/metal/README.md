# src/gfx/metal · 开发说明

## 这个目录是什么
macOS 原生 Metal 上下文：device/queue、子 `NSView` 上的 `CAMetalLayer`、
drawable、command buffer、present/resize。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `MetalContext.hpp` | 纯 C++ 接口（pimpl，`void*` 句柄） |
| `MetalContext.mm` | ObjC++ 实现（device/queue/layer/drawable/command buffer） |

## 依赖关系
- 允许：Metal、QuartzCore、AppKit、Foundation、CoreGraphics。
- 禁止：Qt、`render`、`color`。
- 被依赖：`render/metal/MetalRenderer`、`render/skia/SkiaGpuBackendMetal`。

## 构建与测试
- target：`chroma_gfx`（`.mm` 由 `src/gfx/metal/CMakeLists.txt` 追加，开启 ARC）
- 测试：运行 + 截图（需要屏幕录制权限）。

## 怎么加 Metal 能力
- 只加上下文级能力；绘制/管线放 `render/metal`。
- 改动帧生命周期必须同步检查手写 Metal 后端与 Skia Metal 后端两个调用方。

## 相关文档
`docs/handoff/03-metal-plan.md`、`docs/handoff/05-macos-setup.md`、`docs/handoff/11-macos-display-path.md`（计划）
