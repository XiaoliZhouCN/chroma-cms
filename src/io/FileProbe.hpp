#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chroma::io
{
/// 文件容器类型（"容器"层，不代表编码/像素格式）。
enum class FileKind
{
    Unknown,
    Png,
    Jpeg,
    Tiff,
    Gif,
    WebP,
    Exr,
    Heif,
    Avif,
    Mp4,
    Mov,
};

const char *toString(FileKind kind);

/// 容器里发现的色彩相关元数据条目（只记录"存在性与类别"，内容解析在后续阶段）。
struct ColorMetadataEntry
{
    std::string kind;   // 例如 "iCCP" / "sRGB" / "cICP" / "ICC(APP2)" / "nclx" / "chromaticities"
    std::string detail; // 例如 nclx 的 primaries/transfer 数值、ICC 分块数
    std::uint64_t size = 0;
};

/// 文件探测结果：只描述"容器/帧/元数据存在性"，不做解码、不做色彩转换。
struct FileProbe
{
    FileKind kind = FileKind::Unknown;
    std::uint64_t fileSize = 0;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bitsPerChannel = 0;   // 0 = 未知
    std::string pixelFormatHint;        // 例如 "rgba8" / "gray16" / "yuv420p10le"

    std::uint32_t frames = 1;           // 静态图为 1；动图/多页/视频为帧/页数
    std::uint32_t layers = 0;           // 0 = 未知/不适用；1 = 单层

    bool hasIcc = false;
    bool hasSrgbChunk = false;
    bool hasCicp = false;
    bool hasNclx = false;
    bool hasHdrStatic = false;          // clli / mdcv (ISOBMFF) 等 HDR 静态元数据

    std::vector<ColorMetadataEntry> metadata;
    std::vector<std::string> notes;     // 例如 "无 ICC：按 sRGB 解释（默认假设）"
};

/// 探测文件（UTF-8 路径）。读不到文件时返回 Unknown + note。
FileProbe probeFile(const std::string &utf8Path);

/// 探测内存中的字节（单元测试用）。
FileProbe probeBytes(const std::uint8_t *data, std::size_t size);

/// 序列化为 JSON（供 chroma-probe 输出、集成测试解析）。
std::string toJson(const FileProbe &probe);
} // namespace chroma::io
