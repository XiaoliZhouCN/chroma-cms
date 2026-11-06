#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chroma::media {

/// 解码后的图像：RGBA16_UNORM（每通道 0..65535）
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint16_t> rgba16;
};

/// 用 FFmpeg 解码图片（只取第一帧）。成功返回 true，失败把原因写进 error。
bool loadImage(const std::wstring& path, Image& out, std::string& error);

} // namespace chroma::media