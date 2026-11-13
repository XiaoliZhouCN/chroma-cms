---
extends: ../AGENTS.md
scope: src/color
owner: agent
last_verified: 2026-11-13
---
# src/color · AGENTS 本地扩展

## 继承
继承 `src/AGENTS.md` 与根 `AGENTS.md`。未在此声明的规则沿用父级。

## 本地约定
- 纯 C++，**不得**依赖 Qt / gfx / render / media / FFmpeg。
- 这里是 CPU oracle：GPU 结果以本库为准，不允许反过来。
- 数值统一 `double`；不需要为浮点性能妥协精度。
- 新增数学能力必须同时加 unit 测试（`tests/unit/color_*_test.cpp`）。
- 矩阵/传递函数的常量与公式选择必须在 `docs/color/` 对应文章里解释。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug --target chroma_color
ctest --preset windows-x64-debug -R unit.color
```

## 本目录专属坑
- 矩阵按行存储 `m[row][col]`；派生矩阵与 shader 手写常量的对拍容差 1e-4（来源见 `color_matrix_test.cpp` 注释）。
- sRGB 分段阈值是规范四舍五入值，边界往返误差 ~3e-8，属标准误差而非实现错误。
