---
extends: ../../../AGENTS.md
scope: src/gfx/metal
owner: agent
last_verified: 2026-11-13
---
# src/gfx/metal · AGENTS 本地扩展

## 继承
继承 `src/gfx/AGENTS.md`、`src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- **仅 APPLE 编译**；Objective-C++ 只出现在 `.mm`。
- `MetalContext.hpp` 必须保持纯 C++（pimpl + `void*` 句柄），否则纯 CXX 的工厂 TU 编译失败。
- `.mm` 使用 ARC（`-fobjc-arc`）；ObjC 对象生命周期归 Context 持有。

## 验证入口
```bash
cmake --build --preset macos-arm64-debug
ctest --preset macos-arm64-debug
```

## 本目录专属坑
- `CAMetalLayer` 的 `pixelFormat` 与 drawable 尺寸必须和交换链/离屏目标一致（当前 RGBA16Float）。
- `nextDrawable` 可能返回 nil：`beginFrame()` 返回 false 时调用方必须跳过该帧。
