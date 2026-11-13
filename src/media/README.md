# src/media · 开发说明

## 这个目录是什么
媒体解码：FFmpeg 封装，把图片/视频文件解成可用的帧数据。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `ImageLoader.hpp/.cpp` | 图片解码（当前输出 RGBA16 UNORM，取第一帧） |

## 依赖关系
- 允许：FFmpeg（libavformat/libavcodec/libswscale）。
- 禁止：Qt、gfx/render、色彩转换逻辑。
- 被依赖：`app`（后续 `src/io` 元数据层会与其协作）。

## 构建与测试
- target：`chroma_media`
- 测试：后续 integration 测试用 `tests/corpus/` 样本覆盖。

## 怎么加格式支持
1. 先确认 FFmpeg 能解（probe 输出 pixel format）；
2. 在 `ImageLoader`/新的 decoder 中保留格式与元数据；
3. 用合成样本写 integration 测试，断言尺寸/格式/首帧内容。

## 相关文档
`docs/ROADMAP.md` §1、`docs/color/04-decode-pixel-formats.md`（计划）
