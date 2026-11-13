---
extends: ../AGENTS.md
scope: tests/unit
owner: agent
last_verified: 2026-11-13
---
# tests/unit · AGENTS 本地扩展

## 继承
继承 `tests/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- 只测 CPU 纯逻辑；不得初始化 GPU/窗口/系统服务。
- 每个测试文件一个主题；共享断言用 `TestSupport.hpp`。
- 双精度比较一律给容差；禁止 `==`（整除/枚举除外）。
- 已知参考值必须来自独立计算或标准文档，并在注释里说明来源。

## 验证入口
```powershell
ctest --preset windows-x64-debug -R unit
```

## 本目录专属坑
- 测试链路：`unit_color_*` 依赖 `chroma_color`，不要误链 `chroma_gfx`。
- 矩阵与 shader 手写常量的对拍容差（1e-4）是"常量精度"导致的，不是实现误差。
