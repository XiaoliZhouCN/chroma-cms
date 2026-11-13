# src/color · 开发说明

## 这个目录是什么
CPU 色彩管理核心（oracle）：传递函数、primaries/白点、RGB↔XYZ、白点适应、输出编码。
后续 integration/e2e 测试都以这里的输出为参考。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `Types.hpp` | `Rgb`/`Xyz`（double）、裁剪工具 |
| `Transfer.*` | 传递函数：linear/sRGB/gamma 2.2/2.4/PQ/HLG 的 decode/encode |
| `Primaries.*` | sRGB/P3/BT.2020 primaries、白点、`ColorSpace` |
| `Matrix.*` | 由 primaries 推导 RGB↔XYZ、求逆、Bradford 白点适应 |
| `Output.*` | 相对亮度（1.0=10000 nits）↔ scRGB 单位（1.0=80 nits） |

## 依赖关系
- 允许：C++ 标准库。
- 禁止：Qt / gfx / render / media / FFmpeg。
- 被依赖：`render`（将来的 GPU 对照）、`tests/unit`、`app`（元数据/诊断展示）。

## 构建与测试
- target：`chroma_color`
- 测试：`unit.color_math` / `unit.color_transfer` / `unit.color_matrix`

```powershell
cmake --build --preset windows-x64-debug --target unit_color_math
ctest --preset windows-x64-debug -R unit.color
```

## 怎么加新东西
- 新数学工具：`src/color/<topic>.hpp/.cpp`，并在 `CMakeLists.txt` 注册；
- 新测试：`tests/unit/color_<topic>_test.cpp`，并在 `tests/unit/CMakeLists.txt` 注册 `add_test`；
- 新知识点：同步写 `docs/color/<编号>-<标题>.md` 并在 `00-index.md` 登记。

## 相关文档
`docs/color/00-index.md`、`docs/testing/00-index.md`、`docs/ROADMAP.md`
