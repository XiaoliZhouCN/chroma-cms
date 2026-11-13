---
extends: ../AGENTS.md
scope: src/io
owner: agent
last_verified: 2026-11-13
---
# src/io · AGENTS 本地扩展

## 继承
继承 `src/AGENTS.md` 与根 `AGENTS.md`。未在此声明的规则沿用父级。

## 本地约定
- 只做**容器/元数据探测**：不解码像素、不做色彩转换（解码在 `media`，转换在 `color`）。
- 纯 C++，不依赖 Qt / gfx / render / media。
- 所有解析必须做边界检查：损坏/截断文件只能返回"未知/不完整"，不能崩溃、不能越界读。
- 必须先识别容器，再按容器解析；未知格式返回 `FileKind::Unknown` + note。
- 默认假设（如"无 ICC 按 sRGB 解释"）必须写进 `notes`，不能当事实。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug --target chroma_io
ctest --preset windows-x64-debug -R unit.container_probe
```

## 本目录专属坑
- ISOBMFF box 的 size 字段有 32-bit/64-bit/0（到文件尾）三种情况，全部要处理。
- TIFF 的多页通过 IFD 链遍历，必须有页数上限，防止恶意环。
