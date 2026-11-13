# src/render · 开发说明

## 这个目录是什么
渲染后端层：平台无关契约 + 三个后端实现。

## 文件与职责
| 路径 | 职责 |
|---|---|
| `IRenderer.hpp` | 渲染器契约：initialize/resize/present/backendName/setImage |
| `IViewTransform.hpp` | 可选交互契约：setQuarterTurns/setZoom |
| `RendererFactory.*` | 按平台/环境变量选择后端；`createRenderer(BackendKind)` 供双视图显式指定 |
| `d3d12/` | 手写 D3D12 后端 |
| `metal/` | 手写 Metal 后端 |
| `skia/` | Skia（Ganesh）桥接后端（D3D12/Metal） |

## 依赖关系
- 允许：`gfx`、`color`（后续）、`media`（后续，纹理源）。
- 禁止：Qt、app 层。
- 被依赖：`app`。

## 构建与测试
- target：`chroma_render`（Skia 为可选独立 target `chroma_render_skia`）
- 测试：运行 + 截图；后续 e2e 做三后端 offscreen 对照。

## 怎么加后端
1. 新建 `src/render/<api>/` + 三件套；
2. 在 `src/render/CMakeLists.txt` 平台/开关分支里追加源文件与链接；
3. 在 `RendererFactory.cpp` 增加门控分支；
4. 双平台构建 + 双视图截图 + 与 oracle 对照。

## 相关文档
`docs/handoff/09-skia-backend.md`、`docs/handoff/10-skia-refactor-cmake.md`、`docs/ROADMAP.md` §5
