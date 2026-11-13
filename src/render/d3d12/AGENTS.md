---
extends: ../../AGENTS.md
scope: src/render/d3d12
owner: agent
last_verified: 2026-11-13
---
# src/render/d3d12 · AGENTS 本地扩展

## 继承
继承 `src/render/AGENTS.md`、`src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- **仅 Windows**；不得包含到公共头。
- 交换链状态/呈现归 `gfx::D3D12Context`；本目录只录制 clear/绘制命令。
- 色彩数学目前内嵌在 `shaders/fullscreen.hlsl`；后续以 `src/color` 为 oracle 对齐，任何常量修改必须同时更新两边并加测试。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug --target ChromaCMS
.\build\windows-x64-debug\Debug\ChromaCMS.exe
```

## 本目录专属坑
- 着色器运行时编译：改 `.hlsl` 后需重新构建（POST_BUILD 拷贝到输出目录）。
- `present()` 的"无图"分支必须保持守卫，否则会绑定未初始化 SRV。
