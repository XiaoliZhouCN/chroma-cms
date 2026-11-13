---
extends: ../../AGENTS.md
scope: src/render/metal
owner: agent
last_verified: 2026-11-13
---
# src/render/metal · AGENTS 本地扩展

## 继承
继承 `src/render/AGENTS.md`、`src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- **仅 APPLE**；ObjC++ 只在 `.mm`/实现头里。
- 公共头纯 C++；`MetalRendererImpl.hpp` 由 `.cpp` 与 `.mm` 共享，不得出现 ObjC 类型。
- 色彩输出为 Display-P3 线性（`outputScale=1.0`）；与 Windows 刻意不同，改色彩要同时说明两端差异。

## 验证入口
```bash
cmake --build --preset macos-arm64-debug
open build/macos-arm64-debug/ChromaCMS.app
```

## 本目录专属坑
- MSL 运行时编译：改 `.metal` 后需重新构建（POST_BUILD 拷贝）。
- `CAMetalLayer` 颜色格式与离屏/blit 目标必须一致。
