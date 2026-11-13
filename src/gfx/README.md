# src/gfx · 开发说明

## 这个目录是什么
原生图形上下文层：设备、队列、交换链/图层、命令录制、同步、帧生命周期。
两个平台实现同构但接口不共用（各自的 Context 类）。

## 文件与职责
| 路径 | 职责 |
|---|---|
| `GfxCommon.hpp` | 本层共享入口/占位头 |
| `D3D12Common.hpp` | Windows：ComPtr / ThrowIfFailed |
| `d3d12/` | Windows：`D3D12Context` |
| `metal/` | macOS：`MetalContext` |

## 依赖关系
- 允许：操作系统/图形 API（D3D12、Metal、DXGI、QuartzCore）。
- 禁止：依赖 `render`、`color`、Qt。
- 被依赖：`render/d3d12`、`render/metal`、`render/skia`。

## 构建与测试
- target：`chroma_gfx`（Windows/APPLE 为 STATIC，其他平台 INTERFACE）
- 测试：随渲染后端做集成/运行验证（无独立 unit 测试）。

## 怎么加新平台
1. 新建 `src/gfx/<api>/`，放 `CMakeLists.txt`（`target_sources(chroma_gfx ...)`）+ 三件套文档；
2. 在 `src/gfx/CMakeLists.txt` 的平台分支 `add_subdirectory`；
3. 接口与既有 Context 保持"同形"（initialize/resize/beginFrame/endFrame/waitForGpu）。

## 相关文档
`docs/handoff/08-current-progress.md` §3（历史分层说明）、`docs/handoff/10-skia-refactor-cmake.md`
