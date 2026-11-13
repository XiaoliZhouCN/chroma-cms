---
extends: ../AGENTS.md
scope: src/render
owner: agent
last_verified: 2026-11-13
---
# src/render · AGENTS 本地扩展

## 继承
继承 `src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- 契约层（`IRenderer`/`IViewTransform`/`RendererFactory`）保持稳定；改签名必须先讨论。
- 后端各目录只依赖 `gfx`（以及将来的 `color`/`media`），后端之间不得互相引用。
- 三个后端（手写 D3D12 / 手写 Metal / Skia）功能与精度同步推进；差异必须能解释。
- 新增后端需要：CMake 选项 + 工厂分支 + 三件套文档 + 双平台验证。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug
cmake --build --preset windows-x64-skia
```

## 本目录专属坑
- Skia 后端只能在 Release 预设构建（预编译 Skia 的 CRT 约束）。
- 后端代码不得包含平台头到公共接口（工厂 TU 是纯 CXX）。
