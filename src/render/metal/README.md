# src/render/metal · 开发说明

## 这个目录是什么
手写 Metal 渲染后端：MSL 运行时编译、PSO、纹理上传、全屏三角形、交互矩阵、Display-P3 色彩管线。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `MetalRenderer.hpp` | 类声明（IRenderer + IViewTransform） |
| `MetalRenderer.cpp` | 平台无关胶水逻辑 |
| `MetalRenderer.mm` | ObjC 部分（device/texture/pipeline 绑定） |
| `MetalRendererImpl.hpp` | 跨 `.cpp/.mm` 共享的 Impl 字段（纯 C++ 类型） |

## 依赖关系
- 允许：`gfx/metal`（MetalContext）、Metal/QuartzCore/AppKit。
- 禁止：Qt、Skia、其他后端。
- 被依赖：`app`（通过工厂）。

## 构建与测试
- target：`chroma_render`
- 测试：运行 + 截图（屏幕录制权限）。

## 怎么加功能
- 绘制逻辑放本目录；上下文/图层生命周期放 `gfx/metal`。
- 改 `.metal` 后重新构建；色彩常量与 `src/color`/docs 同步。

## 相关文档
`docs/handoff/03-metal-plan.md`、`docs/handoff/09-skia-backend.md`
