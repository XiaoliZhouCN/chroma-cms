---
extends: ../AGENTS.md
scope: src
owner: agent
last_verified: 2026-11-13
---
# src · AGENTS 本地扩展

## 继承
继承根 `AGENTS.md`。未在此声明的规则沿用父级；根 §1 硬规则不可覆写。

## 本地约定
- 分层依赖方向：`app → render → gfx`，`app → media`；`color`、`io` 独立（不得依赖 Qt/gfx/render/media）；`gfx` 不得依赖 `render`。
- 新增一级模块必须同时提交 `CMakeLists.txt` + `AGENTS.md` + `README.md`（见根 §12）。
- 模块实现细节不上浮到父层头文件；跨模块只暴露稳定接口。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

## 本目录专属坑
- 平台目录（`gfx/metal`、`render/metal`）只在 APPLE 下 `add_subdirectory`；公共代码不得 include 平台头。
