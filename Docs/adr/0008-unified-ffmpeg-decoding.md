---
status: accepted
owner: XiaoliZhouCN
updated: 2026-11-05
---

# 0008. 图像与视频统一用 FFmpeg 解码

- **状态**：accepted
- **日期**：2026-11-05
- **决策者**：XiaoliZhouCN
- **相关**：[0005](0005-ffmpeg-for-video-decode.md)（视频解码）、[0009](0009-multi-platform-support.md)（多平台）

## 背景

引入 macOS 需求后，原来基于 WIC 的图像解码不再成立：

1. **两套平台 API 拿不到统一的元数据**。WIC 与 ImageIO 都只保证像素，不保证把 PNG 的
   `cICP`（原色/传递函数/矩阵/范围）暴露出来。而元数据是本项目色彩管线的输入——
   读不到它，M2 之后的一切都无从谈起。
2. **ImageIO 有隐式色彩转换风险**。`CGImage` 带色彩空间标签，一旦经 `CGContext`
   取像素，就会被转换到目标空间，直接违反 [0004](0004-self-implemented-color-management.md)。
3. **视频本来就要 FFmpeg**（[0005](0005-ffmpeg-for-video-decode.md)）。图像再单独维护一条
   平台解码路径，等于两条元数据语义不同的链路。

## 决策

1. **图像与视频共用 FFmpeg**（libavformat / libavcodec / libavutil / libswscale），
   删除 WIC 路径。
2. 依赖形态：**预编译包**（BtbN `win64-lgpl-shared`，FFmpeg n9.0.1），
   放在 `external/ffmpeg`（不入库），由 `tools/fetch-ffmpeg.ps1` 复原。
3. **元数据的权威来源是容器本身**：`src/media/ImageLoader.cpp` 内的 PNG chunk 解析器
   读 `cICP`，优先于 `AVFrame` 的 `color_primaries` 等字段。原因：FFmpeg 不保证把
   PNG 的 cICP 映射到 AVFrame，而这两组值必须一致才有意义。
4. `sws_scale` **只用于 RGB→RGBA16 的像素布局转换**，不做任何色彩/范围/传递函数转换。
5. 外部依赖一律通过 `cmake/FindFFmpeg.cmake` 的 imported target `FFmpeg::FFmpeg` 引入，
   头文件必须包在 `extern "C"` 中（见「后果」）。

## 后果

### 正面

- 一条解码路径覆盖 Windows / macOS，元数据语义统一；
- `AVFrame` 的 H.273 码值与本项目的 `ColorMetadata` 一一对应，不需要中间枚举；
- 视频（M4）接入时只需扩展 `loadImage` 之外的时间轴逻辑，解码层不动。

### 负面 / 代价

- 依赖体积：`external/ffmpeg` 约 116 MB（仅 5 个 DLL 就 ~118 MB 的解压前体积），
  其中 `avcodec-63.dll` 单独 90 MB；
- 分发必须带 DLL 且遵守 LGPL：不得静态链接，需保留许可证与替换途径；
- **MSVC 链接陷阱**：FFmpeg 头文件不自己声明 `extern "C"`，不包就会得到
  22 个 `LNK2019`（符号按 C++ 名字修饰）；
- `avcodec-63.dll` 依赖 `swresample-7.dll`，只拷 avcodec/avformat/avutil 会得到
  `0xC0000135`（DLL 找不到）。

### 需要跟进

- [ ] M4 之前补 `Docs/LICENSES.md`，把 FFmpeg 的 LGPL 义务写清楚（动态链接、许可证随包、可替换）。
- [ ] 分发前复核：预编译包是 LGPL v3，源码自建是 LGPL 2.1-or-later，两者不能混写。

## 替代方案

| 方案 | 否决理由 |
|---|---|
| WIC(Windows) + ImageIO(macOS) 双实现 | 元数据语义不一致；ImageIO 有隐式转换；要写两遍且无法共用测试 |
| Skia SkCodec | 要等 M3 的 Skia 构建；对 PNG `cICP` 的暴露程度不明确 |
| stb_image / libpng + libjpeg-turbo | 元数据要自己解析（PNG 部分方案相同），但没有视频能力，等于两套解码栈 |
| 全部自己写解码器 | 与学习目标（渲染链路 + 色彩管理）无关，纯属浪费 |

## 参考

- BtbN FFmpeg-Builds：`win64-lgpl-shared` 变体
- [../BUILD.md](../BUILD.md) §3.1、[../media/ImageLoader.h](../../src/media/ImageLoader.h)
