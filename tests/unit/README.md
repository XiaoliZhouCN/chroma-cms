# tests/unit · 开发说明

## 这个目录是什么
单元测试：色彩数学、元数据解析等纯逻辑；不启动 GPU/窗口。

## 文件与职责
| 文件 | 覆盖 |
|---|---|
| `TestSupport.hpp` | 断言工具（expectNear/expectRgbNear/expectTrue/finish） |
| `smoke_test.cpp` | 构建/运行冒烟 |
| `color_math_test.cpp` | 白点、色域差、输出缩放、裁剪（语义级） |
| `color_transfer_test.cpp` | sRGB/gamma/PQ/HLG 已知值、端点、单调、往返 |
| `color_matrix_test.cpp` | 推导矩阵 vs shader 常量、求逆、Bradford |

## 依赖关系
- 允许：`chroma_color`、`chroma_core`。
- 禁止：ChromaCMS（app）、GPU 后端。

## 构建与测试
```powershell
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug -R unit --output-on-failure
```

## 怎么加新测试
1. `tests/unit/<topic>_test.cpp`，include `TestSupport.hpp`；
2. `tests/unit/CMakeLists.txt` 加 `add_executable` + `target_link_libraries(... chroma_color)` + `add_test`；
3. 测试名统一 `unit.<topic>`。

## 相关文档
`docs/testing/00-index.md` §1.1
