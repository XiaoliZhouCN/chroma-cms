# src · 开发说明

## 这个目录是什么
源代码根，按"层"拆分；每个子目录都有各自的 `AGENTS.md`（本地规则）与 `README.md`（开发说明）。

## 文件与职责
| 目录 | 职责 | 允许依赖 |
|---|---|---|
| `core/` | 版本号等最小公共设施 | 无 |
| `color/` | CPU 色彩管理 oracle（传递函数/矩阵/白点/输出编码） | C++ 标准库 |
| `gfx/` | 原生图形上下文（D3D12 / Metal） | 操作系统 API |
| `media/` | FFmpeg 解码封装 | FFmpeg |
| `render/` | 渲染后端（手写 D3D12/Metal、Skia） | `gfx`、`media`、`color` |
| `app/` | Qt 窗口与双视图 UI | `render`、`media`、`color` |

## 依赖方向
```
app ──→ render ──→ gfx
 │        └──→ color
 └──→ media
```
禁止反向依赖（`gfx` 不得依赖 `render`，`color` 不得依赖任何上层）。

## 构建与测试
见根 `README.md`；常用：
```powershell
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

## 怎么加新模块
1. 新建 `src/<module>/`，放 `CMakeLists.txt` + `AGENTS.md` + `README.md`；
2. 在 `src/CMakeLists.txt` 的 `add_subdirectory` 中注册（注意平台条件）；
3. 更新父目录 `README.md` 的职责表；
4. `ctest -R docs.structure` 必须通过。

## 相关文档
`AGENTS.md` §12、`docs/ROADMAP.md`、`docs/decisions/0001-directory-docs-convention.md`
