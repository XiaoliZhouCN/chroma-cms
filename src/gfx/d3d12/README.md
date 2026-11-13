# src/gfx/d3d12 · 开发说明

## 这个目录是什么
Windows 原生 D3D12 上下文：设备/队列/工厂、交换链（R16G16B16A16_FLOAT + scRGB）、
RTV 堆、命令录制、fence 同步、`beginFrame/endFrame` 帧生命周期。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `D3D12Context.hpp` | 公开接口（帧生命周期、命令列表、原生句柄、尺寸、backbuffer） |
| `D3D12Context.cpp` | 设备/交换链/RTV/allocator/fence/barrier/Present 实现 |

## 依赖关系
- 允许：`d3d12`、`dxgi`、`chroma_gfx`（同层工具头）。
- 禁止：Qt、`render`、`color`。
- 被依赖：`render/d3d12/D3D12Renderer`、`render/skia/SkiaGpuBackendD3D12`。

## 构建与测试
- target：`chroma_gfx`（源文件由 `src/gfx/d3d12/CMakeLists.txt` 追加）
- 测试：运行 + 截图；GPU 数值对照在 e2e 阶段加入。

## 怎么加 D3D12 能力
- 只加"上下文级"能力（资源池、同步、格式查询）；绘制/管线放 `render/d3d12`。
- 改动帧生命周期必须同步检查 `render/d3d12` 与 `render/skia` 两个调用方。

## 相关文档
`docs/handoff/08-current-progress.md`、`docs/handoff/09-skia-backend.md`、`docs/color/10-windows-display-path.md`（计划）
