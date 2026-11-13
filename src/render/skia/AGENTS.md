---
extends: ../../AGENTS.md
scope: src/render/skia
owner: agent
last_verified: 2026-11-13
---
# src/render/skia · AGENTS 本地扩展

## 继承
继承 `src/render/AGENTS.md`、`src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- Skia 后端**只能在 Release 预构建**（预编译 Skia 的 CRT 约束）；Debug 不得引入。
- 平台差异只能出现在 `SkiaGpuBackend*.cpp/.mm`；`SkiaRenderer` 保持平台无关（SkSL/画布/色彩 uniform）。
- SkSL 走运行时文件 `shaders/fullscreen.sksl`，不内联字符串。
- uniform 布局以 `effect->uniformSize() == sizeof(struct)` 为硬约束；`float3x3` 是 9 个 float 紧凑排列（36 B）。
- `makeShader` 的 `localMatrix` 必须传"设备像素→图片像素"映射的**逆矩阵**，并 `clipRect` 到 contain-fit 视口。

## 验证入口
```powershell
cmake --build --preset windows-x64-skia
.\build\windows-x64-skia\Release\ChromaCMS.exe
```

## 本目录专属坑
- `makeShader`/`SkSL` 失败会静默黑屏：必须检查返回值并打印原因。
- SkSL 方言：向量/标量不能混参（`pow(float3, float3)`）；局部变量不能随意 `const`。
