# src/render/skia · 开发说明

## 这个目录是什么
Skia（Ganesh）桥接后端：Skia 负责画布/着色/文字，平台上下文仍由 `gfx` 提供。
Windows 用 Ganesh D3D12，macOS 用 Ganesh Metal。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `SkiaRenderer.hpp/.cpp` | 实现 IRenderer + IViewTransform；SkSL 加载、SkSurface 绘制、色彩 uniform、交互矩阵 |
| `SkiaGpuBackend.hpp` | 平台后端接口（Ganesh/Present/离屏目标） |
| `SkiaGpuBackendD3D12.cpp` | Windows：复用 `gfx::D3D12Context` |
| `SkiaGpuBackendMetal.mm` | macOS：复用 `gfx::MetalContext` |

## 依赖关系
- 允许：预编译 Skia（Ganesh）、`gfx` 对应平台上下文。
- 禁止：Qt、手写后端内部实现。
- 被依赖：`app`（通过工厂，`CHROMA_BACKEND=skia`）。

## 构建与测试
- target：`chroma_render_skia`（可选；仅 Release 预设）
- 测试：运行 + 截图；后续 e2e 三后端对照。

## 怎么加 Skia 能力
- 内容绘制/着色：`SkiaRenderer.cpp` + `shaders/fullscreen.sksl`；
- 平台资源/呈现：`SkiaGpuBackend*`；
- 改 uniform 或矩阵时先核对 48 B 布局与 localMatrix 规则（见 AGENTS）。

## 相关文档
`docs/handoff/09-skia-backend.md`、`docs/handoff/10-skia-refactor-cmake.md`
