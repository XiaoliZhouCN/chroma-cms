# src/io · 开发说明

## 这个目录是什么
文件层：识别容器格式、提取基础结构信息与色彩元数据**存在性**；不解码像素。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `FileProbe.hpp` | `FileKind` / `ColorMetadataEntry` / `FileProbe` / `probeFile` / `probeBytes` / `toJson` |
| `FileProbe.cpp` | PNG/JPEG/TIFF/GIF/WebP/EXR/ISOBMFF（HEIF/AVIF/MP4/MOV）解析 |

## 依赖关系
- 允许：C++20 标准库。
- 禁止：Qt、gfx/render/media、色彩转换逻辑。
- 被依赖：`tools/chroma-probe`、`tests/unit`、后续 `media`（元数据传递）。

## 构建与测试
- target：`chroma_io`
- 测试：`unit.container_probe`（覆盖 `tests/corpus/`）

```powershell
cmake --build --preset windows-x64-debug --target unit_container_probe
ctest --preset windows-x64-debug -R unit.container_probe
```

## 怎么加新格式
1. 在 `FileKind` 加枚举与 `toString` 分支；
2. 增加 `parseXxx(Reader&, FileProbe&)`，在 `probeBytes` 的 switch 中分派；
3. 在 `tests/corpus/` 放小样本，`container_probe_test.cpp` 增加断言；
4. 在 `docs/color/01-container-and-frame-basics.md` 补充该格式的元数据位置说明。

## 相关文档
`docs/color/01-container-and-frame-basics.md`、`docs/color/02/03`（计划）
