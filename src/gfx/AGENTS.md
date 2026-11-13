---
extends: ../AGENTS.md
scope: src/gfx
owner: agent
last_verified: 2026-11-13
---
# src/gfx · AGENTS 本地扩展

## 继承
继承 `src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- 本层只负责**原生上下文**（device/queue/swapchain/surface/同步/帧生命周期）。
- **不承载色彩数学**（在 `src/color/`）与绘制决策（在 `src/render/`）。
- 公共头文件必须保持纯 C++：平台类型（ID3D12*/id<MTL*>）只出现在平台子目录的 `.cpp/.mm`。
- 帧生命周期约定：交换链状态只能由 context 的 `beginFrame/endFrame` 修改。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug
```

## 本目录专属坑
- `INTERFACE` 库只能用 INTERFACE 属性；`chroma_set_warnings()` 只对真实 target 调用。
- 平台目录只在对应平台 `add_subdirectory`。
