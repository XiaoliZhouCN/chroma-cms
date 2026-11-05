#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chroma::media {

/// CPU 侧的图像数据，格式固定为 RGBA16_UNORM（每通道 0..65535）。
struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint16_t> rgba16;

    /// true 表示源图满足「10-bit 码左移 6 位存入 16-bit 通道」的存储约定，
    /// 加载时已重映射为 code/1023 的满量程表示。
    bool reinterpretedFrom10Bit = false;

    /// 源文件通道位深（仅用于状态栏展示，不参与计算）。
    uint32_t sourceBitDepth = 0;
};

/// 用 WIC 解码 PNG 为 RGBA16。
///
/// 约定（来自 tools/gen_ptmr.py 的文档头）：
///   16-bit 通道里存的是 10-bit 码左移 6 位，即 value16 = code10 << 6，
///   低 6 位恒为 0。直接按 65535 归一化会引入 9.6e-4 偏差（1023 -> 0.99904）。
///   因此这里检测「所有通道低 6 位为 0」后，按 code/1023 重映射到 65535，
///   让后续 UNORM 采样等于 code/1023，量化到 10-bit 时能精确还原。
///
/// 这一步是**存储约定的解码**，不是色彩管理。
bool loadImage(const std::wstring& path, ImageData& out, std::string& error);

} // namespace chroma::media
