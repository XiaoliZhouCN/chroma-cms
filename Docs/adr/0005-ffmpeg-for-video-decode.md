---
status: accepted
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 0005. 视频解码选 FFmpeg

- **状态**：accepted
- **日期**：2026-11-04
- **决策者**：XiaoliZhouCN

## 背景

M4 需要播放视频，并把"解码器声明的色彩元数据"贯通到色彩管线。

关键需求不是解码性能，而是**元数据可达性**：我们需要拿到 `color_primaries`、`color_transfer`、`color_space`（YUV 矩阵）、`color_range`，并把这些值原样展示、原样送进 Pass 1。

## 决策

使用 **FFmpeg**（libavformat / libavcodec / libavutil，必要时 swscale）：

1. 通过 vcpkg 获取，manifest 模式声明；
2. 元数据来源：`AVCodecParameters` 与 `AVFrame` 上的色彩字段，配合 `AVFrameSideData` 中的 HDR 元数据（二期）；
3. 解码输出保持原始像素格式（NV12/P010/YUV420P），**不做 CPU 侧的 YUV→RGB**——转换留给 GPU Pass 1；
4. 解码在独立线程，通过双缓冲把 `FrameData` 交给 UI 线程。

## 后果

### 正面

- 色彩元数据字段完整、语义清晰，是学习"元数据如何驱动色彩管线"的最佳素材；
- 格式覆盖面广，测试素材易得；
- 与 M2 的 YUV 路径天然衔接（601/709、limited/full 都能真实遇到）。

### 负面 / 代价

- 依赖体积大（若干 DLL），部署需注意；
- **许可是真问题**：启用 `x264` 等 GPL 组件会使整体成为 GPL。本仓库学习用途、不分发二进制，暂可接受；一旦要分发必须重新评估并写新 ADR。
- 硬件解码（D3D11VA/DXVA）接入会增加复杂度，一期用软件解码即可。

### 需要跟进

- [ ] **【待验证】** vcpkg `ffmpeg` 在 `x64-windows` 下的默认特性与 DLL 拷贝方式。
- [ ] 若后续要分发二进制：新建 ADR 决定"仅 LGPL 组件"或"接受 GPL"。
- [ ] 二期：HDR10 静态元数据（`MasteringDisplayMetadata`、`ContentLightLevel`）读取。

## 替代方案

| 方案 | 否决理由 |
|---|---|
| Media Foundation | 元数据获取更绕、API 冗长；知识可迁移性低于 FFmpeg |
| 自己写容器解析 + 调用系统解码器 | 工作量与收益不成比例 |
| 只用图片，不做视频 | 放弃"元数据驱动色彩"的最好练习场景（可作降级方案） |

## 参考

- FFmpeg `libavutil/pixfmt.h`、`AVFrame::color_*` 字段
- [../ARCHITECTURE.md](../ARCHITECTURE.md) §5
