# src/render/d3d12 · 开发说明

## 这个目录是什么
手写 D3D12 渲染后端：根签名、PSO、SRV 堆、纹理上传、全屏三角形绘制、交互矩阵、色彩管线。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `D3D12Renderer.hpp` | 类声明（实现 IRenderer + IViewTransform） |
| `D3D12Renderer.cpp` | 管线创建、纹理上传、present、交互矩阵 |

## 依赖关系
- 允许：`gfx/d3d12`（D3D12Context）、`d3dcompiler`。
- 禁止：Qt、Skia、其他后端。
- 被依赖：`app`（通过工厂）。

## 构建与测试
- target：`chroma_render`
- 测试：运行 + 截图；后续 e2e 与 CPU oracle 对照。

## 怎么加功能
- 绘制相关逻辑放本目录；上下文/资源生命周期放 `gfx/d3d12`。
- 改 `.hlsl` 后重新构建；改色彩常量同步 `src/color` 测试。

## 相关文档
`docs/handoff/07-windows-interaction.md`、`docs/color/05-transfer-functions.md`（计划）
