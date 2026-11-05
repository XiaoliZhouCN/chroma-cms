#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chroma::media {

/// 色彩元数据。码值直接采用 ITU-T H.273（与 FFmpeg 的 AVColorPrimaries /
/// AVColorTransferCharacteristic / AVColorSpace / AVColorRange 一致），
/// 便于和 AVFrame 字段一一对应，不需要中间枚举转换。
struct ColorMetadata {
    int primaries = 2;  ///< 2 = unspecified，9 = BT.2020，1 = BT.709
    int transfer = 2;   ///< 2 = unspecified，16 = PQ，18 = HLG，13 = sRGB
    int matrix = 2;     ///< 0 = identity(RGB)，9 = BT.2020-NCL，1 = BT.709
    int range = 2;      ///< 1 = full，0 = limited
    bool metadataFromFile = false;  ///< 是否真的读到了元数据（否则全是默认值）
    std::string source;             ///< "cICP" / "frame" / "none"：元数据来源，便于排查
};

/// CPU 侧图像数据，格式固定 RGBA16_UNORM（每通道 0..65535）。
struct ImageData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint16_t> rgba16;

    /// true 表示源图满足「10-bit 码左移 6 位存入 16-bit 通道」的存储约定，
    /// 加载时已重映射为 code/1023 的满量程表示。
    bool reinterpretedFrom10Bit = false;

    /// 源像素位深（8 或 16），仅用于状态栏展示与存储约定判定。
    std::uint32_t sourceBitDepth = 0;

    ColorMetadata color;
};

const char* colorPrimariesName(int code);
const char* colorTransferName(int code);
const char* colorMatrixName(int code);
const char* colorRangeName(int code);

} // namespace chroma::media
