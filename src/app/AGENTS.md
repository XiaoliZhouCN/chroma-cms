---
extends: ../AGENTS.md
scope: src/app
owner: agent
last_verified: 2026-11-13
---
# src/app · AGENTS 本地扩展

## 继承
继承 `src/AGENTS.md` 与根 `AGENTS.md`。未在此声明的规则沿用父级。

## 本地约定
- UI 只做"展示与交互"，**不实现色彩数学**；色彩转换一律调用 `src/color`。
- Qt 对象使用 `new` 后交给 `unique_ptr`/父子所有权管理，禁止裸 `delete` 混用。
- 任何 UI 改动必须提供运行截图（双视图阶段为左/右/叠图三张）。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug --target ChromaCMS
.\build\windows-x64-debug\Debug\ChromaCMS.exe
```

## 本目录专属坑
- `ViewportWidget` 依赖 native window handle；mac 上窗口创建/生命周期与 Windows 不同，改窗口代码要双平台验证。
