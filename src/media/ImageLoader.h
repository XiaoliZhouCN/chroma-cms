#pragma once

#include "media/ImageData.h"

#include <string>

namespace chroma::media {

/// 用 FFmpeg 解码一张静态图（PNG/JPEG/... 只要 FFmpeg 的 demuxer+decoder 支持）。
///
/// 为什么用 FFmpeg 而不是平台解码器：
///   1. Windows(WIC) / macOS(ImageIO) 两套 API 拿不到统一的色彩元数据；
///   2. FFmpeg 的 AVFrame 直接给出 color_primaries / color_trc / colorspace / color_range；
///   3. 视频（M4）本来就要用它，图像与视频共用一条解码路径。
/// 见 Docs/adr/0008-unified-ffmpeg-decoding.md。
///
/// 已知边界：
///   - 只解第一帧（静态图语义）；动图取首帧。
///   - 不做色彩管理，只把像素与元数据原样取出。
///   - 10-bit-in-16-bit 的存储约定由本层识别并还原（见 ImageData::reinterpretedFrom10Bit）。
bool loadImage(const std::wstring& path, ImageData& out, std::string& error);

} // namespace chroma::media
