# tests · 开发说明

## 这个目录是什么
测试根：unit / integration / e2e 三层；外加目录文档规范门禁（docs.structure）。

## 文件与职责
| 路径 | 职责 |
|---|---|
| `CMakeLists.txt` | 注册测试子目录与 `docs.structure` |
| `unit/` | 纯逻辑测试（color/io 等） |
| `integration/`（计划） | 文件→解码→色彩→像素，对 golden/oracle |
| `e2e/`（计划） | 三后端 offscreen 渲染对照、双视图 |
| `corpus/`（计划） | 小样本测试资产 |
| `golden/`（计划） | 参考输出与容差 |

## 依赖关系
- 允许：`chroma_color`、`chroma_media`、`chroma_render`、`chroma_gfx`（按测试层）。
- 禁止：直接依赖 app UI（e2e 通过离屏/截图，不驱动窗口）。

## 构建与测试
```powershell
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure
```

## 怎么加测试
1. 选层：数学/解析→unit；文件链路→integration；后端/系统→e2e；
2. 文件名 `<主题>_test.cpp`，一个主题一个文件；
3. 在对应 CMakeLists 注册 `add_executable` + `add_test`；
4. 失败信息必须包含用例名、实际值、期望值、阈值。

## 相关文档
`docs/testing/00-index.md`、`AGENTS.md` §9
