#include "io/FileProbe.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace chroma::io
{
namespace
{
// ==================== 安全读取器 ====================
class Reader
{
public:
    Reader(const std::uint8_t *data, std::size_t size) : m_data(data), m_size(size) {}

    bool ok() const { return m_ok; }
    std::size_t pos() const { return m_pos; }
    std::size_t size() const { return m_size; }
    const std::uint8_t *data() const { return m_data; }

    bool seek(std::size_t pos)
    {
        if (pos > m_size) {
            m_ok = false;
            return false;
        }
        m_pos = pos;
        return true;
    }

    bool skip(std::size_t count)
    {
        if (count > m_size || m_pos > m_size - count) {
            m_ok = false;
            return false;
        }
        m_pos += count;
        return true;
    }

    bool need(std::size_t count) const { return m_ok && m_pos <= m_size && count <= m_size - m_pos; }

    std::uint8_t u8()
    {
        if (!need(1)) {
            m_ok = false;
            return 0;
        }
        return m_data[m_pos++];
    }

    std::uint16_t u16be()
    {
        const std::uint8_t a = u8();
        const std::uint8_t b = u8();
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(a) << 8) | b);
    }

    std::uint32_t u32be()
    {
        const std::uint32_t a = u16be();
        const std::uint32_t b = u16be();
        return (a << 16) | b;
    }

    std::uint64_t u64be()
    {
        const std::uint64_t a = u32be();
        const std::uint64_t b = u32be();
        return (a << 32) | b;
    }

    std::uint16_t u16le()
    {
        const std::uint8_t a = u8();
        const std::uint8_t b = u8();
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b) << 8) | a);
    }

    std::uint32_t u32le()
    {
        const std::uint32_t a = u16le();
        const std::uint32_t b = u16le();
        return (b << 16) | a;
    }

    std::uint64_t u64le()
    {
        const std::uint64_t a = u32le();
        const std::uint64_t b = u32le();
        return (b << 32) | a;
    }

    bool readBytes(void *destination, std::size_t count)
    {
        if (!need(count)) {
            m_ok = false;
            return false;
        }
        std::memcpy(destination, m_data + m_pos, count);
        m_pos += count;
        return true;
    }

    /// 读取 NUL 结尾字符串（不消费越界）。
    std::string cstr(std::size_t maxLength = 512)
    {
        std::string result;
        while (result.size() < maxLength && ok()) {
            const std::uint8_t value = u8();
            if (value == 0) {
                return result;
            }
            result.push_back(static_cast<char>(value));
        }
        m_ok = false;
        return result;
    }

private:
    const std::uint8_t *m_data = nullptr;
    std::size_t m_size = 0;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

bool startsWith(const std::uint8_t *data, std::size_t size, const char *signature, std::size_t n)
{
    return size >= n && std::memcmp(data, signature, n) == 0;
}

void addMetadata(FileProbe &probe, const std::string &kind, std::uint64_t size,
                 const std::string &detail = {})
{
    probe.metadata.push_back({kind, detail, size});
}

std::string hex4(std::uint32_t value)
{
    static const char *digits = "0123456789abcdef";
    std::string result(4, '0');
    for (int i = 3; i >= 0; --i) {
        result[static_cast<std::size_t>(i)] = digits[value & 0xF];
        value >>= 4;
    }
    return result;
}

// ==================== PNG ====================
void parsePng(Reader &r, FileProbe &probe)
{
    r.seek(8);
    bool sawIhdr = false;
    while (r.need(12)) {
        const std::uint32_t length = r.u32be();
        char type[5] = {};
        if (!r.readBytes(type, 4)) {
            break;
        }
        const std::size_t dataStart = r.pos();
        if (length > r.size() || dataStart > r.size() || length > r.size() - dataStart) {
            break;
        }
        const std::string chunk(type);
        if (chunk == "IHDR" && length >= 13) {
            probe.width = r.u32be();
            probe.height = r.u32be();
            probe.bitsPerChannel = r.u8();
            const std::uint8_t colorType = r.u8();
            r.skip(3);
            sawIhdr = true;
            switch (colorType) {
            case 0:
                probe.pixelFormatHint = (probe.bitsPerChannel == 16) ? "gray16" : "gray8";
                break;
            case 2:
                probe.pixelFormatHint = (probe.bitsPerChannel == 16) ? "rgb16" : "rgb8";
                break;
            case 3:
                probe.pixelFormatHint = "palette8";
                probe.bitsPerChannel = 8;
                break;
            case 4:
                probe.pixelFormatHint = "gray+alpha";
                break;
            case 6:
                probe.pixelFormatHint = (probe.bitsPerChannel == 16) ? "rgba16" : "rgba8";
                break;
            default:
                probe.pixelFormatHint = "unknown";
                break;
            }
        }
        else if (chunk == "iCCP") {
            probe.hasIcc = true;
            addMetadata(probe, "iCCP", length);
        }
        else if (chunk == "sRGB") {
            probe.hasSrgbChunk = true;
            addMetadata(probe, "sRGB", length);
        }
        else if (chunk == "gAMA") {
            addMetadata(probe, "gAMA", length);
        }
        else if (chunk == "cHRM") {
            addMetadata(probe, "cHRM", length);
        }
        else if (chunk == "cICP" && length >= 4) {
            probe.hasCicp = true;
            const std::uint8_t primaries = r.u8();
            const std::uint8_t transfer = r.u8();
            const std::uint8_t matrix = r.u8();
            const std::uint8_t fullRange = r.u8();
            addMetadata(probe, "cICP", length,
                        "primaries=" + std::to_string(primaries) +
                            " transfer=" + std::to_string(transfer) +
                            " matrix=" + std::to_string(matrix) +
                            " full_range=" + std::to_string(fullRange));
        }
        else if (chunk == "acTL" && length >= 8) {
            probe.frames = r.u32be();
            addMetadata(probe, "acTL", length,
                        "num_frames=" + std::to_string(probe.frames));
        }
        else if (chunk == "IEND") {
            break;
        }
        if (!r.seek(dataStart + length + 4)) { // 数据 + CRC
            break;
        }
    }
    if (!sawIhdr) {
        probe.notes.push_back("PNG 但未找到 IHDR");
    }
    if (probe.frames == 1 && !probe.hasIcc && !probe.hasSrgbChunk && !probe.hasCicp) {
        probe.notes.push_back("PNG 无 iCCP/sRGB/cICP：按 sRGB 解释（默认假设）");
    }
}

// ==================== JPEG ====================
bool isSofMarker(std::uint8_t marker)
{
    return (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
           (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
}

void parseJpeg(Reader &r, FileProbe &probe)
{
    r.seek(0);
    if (r.u16be() != 0xFFD8) {
        probe.notes.push_back("JPEG 缺少 SOI");
        return;
    }
    std::uint32_t iccChunks = 0;
    while (r.need(2)) {
        const std::uint8_t prefix = r.u8();
        if (prefix != 0xFF) {
            continue;
        }
        std::uint8_t marker = r.u8();
        while (marker == 0xFF && r.need(1)) {
            marker = r.u8();
        }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (marker == 0xD9) {
            break;
        }
        if (!r.need(2)) {
            break;
        }
        const std::uint16_t length = r.u16be();
        if (length < 2) {
            break;
        }
        const std::size_t segmentEnd = r.pos() + length - 2;
        if (segmentEnd > r.size()) {
            break;
        }
        if (isSofMarker(marker) && r.need(6)) {
            const std::uint8_t precision = r.u8();
            probe.height = r.u16be();
            probe.width = r.u16be();
            const std::uint8_t components = r.u8();
            probe.bitsPerChannel = precision;
            probe.pixelFormatHint = (components == 1) ? "gray8"
                                    : (components == 3) ? "yuv8"
                                    : (components == 4) ? "yuv8/cmyk8"
                                                        : "unknown";
        }
        else if (marker == 0xE2 && length > 16) {
            char header[12] = {};
            if (r.readBytes(header, 12) && std::memcmp(header, "ICC_PROFILE", 11) == 0) {
                ++iccChunks;
                probe.hasIcc = true;
                addMetadata(probe, "ICC(APP2)", length - 2, "chunks=" + std::to_string(iccChunks));
            }
        }
        else if (marker == 0xE1) {
            char header[6] = {};
            if (r.readBytes(header, 6) && std::memcmp(header, "Exif", 4) == 0) {
                addMetadata(probe, "Exif(APP1)", length - 2);
            }
        }
        else if (marker == 0xDA) {
            break; // SOS 之后是熵编码数据，P0 不解析
        }
        if (!r.seek(segmentEnd)) {
            break;
        }
    }
    if (!probe.hasIcc) {
        probe.notes.push_back("JPEG 无 ICC：按 sRGB 解释（默认假设）");
    }
}

// ==================== TIFF ====================
std::uint32_t tiffValue16Or32(bool littleEndian, std::uint16_t type, std::uint32_t count,
                              const std::uint8_t *valueField)
{
    if (type == 3 && count >= 1) { // SHORT
        return littleEndian ? static_cast<std::uint32_t>(valueField[0] | (valueField[1] << 8))
                            : static_cast<std::uint32_t>((valueField[0] << 8) | valueField[1]);
    }
    if (type == 4 && count >= 1) { // LONG
        if (littleEndian) {
            return static_cast<std::uint32_t>(valueField[0]) |
                   (static_cast<std::uint32_t>(valueField[1]) << 8) |
                   (static_cast<std::uint32_t>(valueField[2]) << 16) |
                   (static_cast<std::uint32_t>(valueField[3]) << 24);
        }
        return (static_cast<std::uint32_t>(valueField[0]) << 24) |
               (static_cast<std::uint32_t>(valueField[1]) << 16) |
               (static_cast<std::uint32_t>(valueField[2]) << 8) |
               static_cast<std::uint32_t>(valueField[3]);
    }
    return 0;
}

void parseTiff(Reader &r, FileProbe &probe)
{
    const bool littleEndian = r.u8() == 'I';
    r.skip(1);
    const std::uint16_t magic = littleEndian ? r.u16le() : r.u16be();
    if (magic != 42) {
        probe.notes.push_back("TIFF magic 不是 42（BigTIFF 暂不支持）");
        return;
    }
    std::uint32_t ifdOffset = littleEndian ? r.u32le() : r.u32be();
    std::uint32_t pages = 0;
    bool foundIcc = false;
    while (ifdOffset != 0 && pages < 512 && ifdOffset < r.size()) {
        if (!r.seek(ifdOffset)) {
            break;
        }
        const std::uint16_t entries = littleEndian ? r.u16le() : r.u16be();
        if (!r.ok()) {
            break;
        }
        for (std::uint16_t i = 0; i < entries && r.need(12); ++i) {
            const std::uint16_t tag = littleEndian ? r.u16le() : r.u16be();
            const std::uint16_t type = littleEndian ? r.u16le() : r.u16be();
            const std::uint32_t count = littleEndian ? r.u32le() : r.u32be();
            std::uint8_t valueField[4] = {};
            r.readBytes(valueField, 4);
            if (tag == 256) { // ImageWidth
                probe.width = tiffValue16Or32(littleEndian, type, count, valueField);
            }
            else if (tag == 257) { // ImageLength
                probe.height = tiffValue16Or32(littleEndian, type, count, valueField);
            }
            else if (tag == 258) { // BitsPerSample
                if (type == 3 && count == 1) {
                    probe.bitsPerChannel = tiffValue16Or32(littleEndian, type, count, valueField);
                }
                else if (type == 3 && count >= 3) {
                    const std::uint32_t offset = tiffValue16Or32(littleEndian, 4, 1, valueField);
                    if (offset < r.size()) {
                        const std::size_t saved = r.pos();
                        if (r.seek(offset) && r.need(2)) {
                            probe.bitsPerChannel = littleEndian ? r.u16le() : r.u16be();
                        }
                        r.seek(saved);
                    }
                }
            }
            else if (tag == 262) { // PhotometricInterpretation
                const std::uint32_t photometric =
                    tiffValue16Or32(littleEndian, type, count, valueField);
                probe.pixelFormatHint = (photometric == 2) ? "rgb"
                                        : (photometric == 3) ? "palette"
                                                             : "gray";
            }
            else if (tag == 34675) { // ICC
                foundIcc = true;
                addMetadata(probe, "ICC(TIFF tag 34675)", count);
            }
        }
        if (!r.need(4)) {
            break;
        }
        ++pages;
        ifdOffset = littleEndian ? r.u32le() : r.u32be();
    }
    probe.frames = std::max<std::uint32_t>(pages, 1);
    probe.hasIcc = foundIcc;
    if (!foundIcc) {
        probe.notes.push_back("TIFF 无 ICC tag：按文件内 PhotometricInterpretation 解释，"
                              "缺失时默认 sRGB");
    }
}

// ==================== GIF ====================
void skipSubBlocks(Reader &r)
{
    while (r.need(1)) {
        const std::uint8_t size = r.u8();
        if (size == 0 || !r.skip(size)) {
            return;
        }
    }
}

void parseGif(Reader &r, FileProbe &probe)
{
    r.seek(6);
    probe.width = r.u16le();
    probe.height = r.u16le();
    const std::uint8_t packed = r.u8();
    r.skip(2);
    if ((packed & 0x80) != 0) {
        const std::size_t tableSize = static_cast<std::size_t>(3) << ((packed & 0x07) + 1);
        r.skip(tableSize);
    }
    std::uint32_t frames = 0;
    while (r.need(1)) {
        const std::uint8_t block = r.u8();
        if (block == 0x3B) {
            break;
        }
        if (block == 0x21) {
            r.u8(); // 扩展标签
            skipSubBlocks(r);
        }
        else if (block == 0x2C) {
            ++frames;
            r.skip(8); // left/top/w/h
            const std::uint8_t imagePacked = r.u8();
            if ((imagePacked & 0x80) != 0) {
                const std::size_t tableSize = static_cast<std::size_t>(3) << ((imagePacked & 0x07) + 1);
                r.skip(tableSize);
            }
            r.u8(); // LZW minimum code size
            skipSubBlocks(r);
        }
        else {
            break;
        }
    }
    probe.frames = std::max<std::uint32_t>(frames, 1);
    probe.bitsPerChannel = 8;
    probe.pixelFormatHint = "palette8";
}

// ==================== WebP ====================
void parseWebp(Reader &r, FileProbe &probe)
{
    r.seek(0);
    r.skip(12); // RIFF + size + WEBP
    std::uint32_t animationFrames = 0;
    std::uint32_t canvasWidth = 0;
    std::uint32_t canvasHeight = 0;
    while (r.need(8)) {
        char fourcc[5] = {};
        r.readBytes(fourcc, 4);
        const std::uint32_t size = r.u32le();
        const std::size_t dataStart = r.pos();
        if (size > r.size() || dataStart > r.size() || size > r.size() - dataStart) {
            break;
        }
        const std::string chunk(fourcc);
        if (chunk == "VP8X" && size >= 10 && r.need(10)) {
            const std::uint8_t flags = r.u8();
            r.skip(3);
            canvasWidth = 1 + (static_cast<std::uint32_t>(r.u8()) |
                               (static_cast<std::uint32_t>(r.u8()) << 8) |
                               (static_cast<std::uint32_t>(r.u8()) << 16));
            canvasHeight = 1 + (static_cast<std::uint32_t>(r.u8()) |
                                (static_cast<std::uint32_t>(r.u8()) << 8) |
                                (static_cast<std::uint32_t>(r.u8()) << 16));
            if ((flags & 0x02) != 0) {
                probe.notes.push_back("WebP 动画（VP8X animation flag）");
            }
        }
        else if (chunk == "VP8L" && size >= 5 && r.need(5)) {
            r.u8(); // signature 0x2F
            const std::uint8_t b0 = r.u8();
            const std::uint8_t b1 = r.u8();
            const std::uint8_t b2 = r.u8();
            const std::uint8_t b3 = r.u8();
            probe.width = 1 + (b0 | ((b1 & 0x3F) << 8));
            probe.height = 1 + ((b1 >> 6) | (b2 << 2) | ((b3 & 0x0F) << 10));
        }
        else if (chunk == "VP8 " && size >= 10 && r.need(10)) {
            r.skip(3); // frame tag
            const std::uint8_t b0 = r.u8();
            const std::uint8_t b1 = r.u8();
            const std::uint8_t b2 = r.u8();
            if (b0 == 0x9D && b1 == 0x01 && b2 == 0x2A) {
                probe.width = static_cast<std::uint32_t>(r.u16le() & 0x3FFF);
                probe.height = static_cast<std::uint32_t>(r.u16le() & 0x3FFF);
            }
        }
        else if (chunk == "ANIM") {
            probe.notes.push_back("WebP 动画（ANIM）");
        }
        else if (chunk == "ANMF") {
            ++animationFrames;
        }
        if (!r.seek(dataStart + size + (size & 1))) { // chunk 奇数长度会 pad
            break;
        }
    }
    if (probe.width == 0) {
        probe.width = canvasWidth;
    }
    if (probe.height == 0) {
        probe.height = canvasHeight;
    }
    if (animationFrames != 0) {
        probe.frames = animationFrames;
    }
    probe.bitsPerChannel = 8;
    probe.pixelFormatHint = "rgba8";
}

// ==================== EXR ====================
void parseExr(Reader &r, FileProbe &probe)
{
    r.seek(0);
    const std::uint32_t magic = r.u32le();
    if (magic != 20000630U) {
        probe.notes.push_back("EXR magic 不正确");
        return;
    }
    const std::uint32_t version = r.u32le();
    if ((version & 0x200) != 0) {
        probe.notes.push_back("EXR tiled（P0 只读取头信息）");
    }
    if ((version & 0x1000) != 0) {
        probe.notes.push_back("EXR multipart");
    }
    int bits = 0;
    std::uint32_t channels = 0;
    while (r.need(1)) {
        const std::string name = r.cstr();
        if (!r.ok() || name.empty()) {
            break;
        }
        const std::string type = r.cstr();
        const std::uint32_t size = r.u32le();
        if (size > r.size() || !r.need(size)) {
            break;
        }
        const std::size_t dataStart = r.pos();
        if (name == "displayWindow" || (name == "dataWindow" && probe.width == 0)) {
            const std::uint32_t xMin = r.u32le();
            const std::uint32_t yMin = r.u32le();
            const std::uint32_t xMax = r.u32le();
            const std::uint32_t yMax = r.u32le();
            if (xMax >= xMin && yMax >= yMin) {
                probe.width = xMax - xMin + 1;
                probe.height = yMax - yMin + 1;
            }
        }
        else if (name == "channels") {
            while (r.need(1)) {
                const std::string channelName = r.cstr();
                if (channelName.empty() || !r.ok()) {
                    break;
                }
                const std::uint32_t pixelType = r.u32le();
                r.u8(); // pLinear
                r.skip(3);
                r.u32le(); // xSampling
                r.u32le(); // ySampling
                ++channels;
                bits = std::max(bits, pixelType == 1 ? 16 : pixelType == 2 ? 32 : 32);
            }
        }
        else if (name == "chromaticities") {
            addMetadata(probe, "EXR chromaticities", size);
            probe.notes.push_back("EXR 用 chromaticities 描述色域（不是 ICC）");
        }
        else if (name == "compression") {
            addMetadata(probe, "EXR compression", size);
        }
        if (!r.seek(dataStart + size)) {
            break;
        }
    }
    if (bits != 0) {
        probe.bitsPerChannel = static_cast<std::uint32_t>(bits);
    }
    probe.layers = std::max<std::uint32_t>(channels, 1);
    probe.pixelFormatHint = (bits == 16) ? "half" : "float";
    probe.notes.push_back("EXR 为线性浮点数据（未应用传递函数）");
}

// ==================== ISOBMFF（HEIF/AVIF/MP4/MOV） ====================
std::uint32_t readU32be(const std::uint8_t *p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void parseBoxes(const std::uint8_t *data, std::size_t size, FileProbe &probe, int depth);

void parseTkhd(const std::uint8_t *data, std::size_t size, FileProbe &probe)
{
    if (size < 8) {
        return;
    }
    const std::uint32_t width = readU32be(data + size - 8);
    const std::uint32_t height = readU32be(data + size - 4);
    probe.width = (width + 0x8000) >> 16;
    probe.height = (height + 0x8000) >> 16;
}

void parseIspe(const std::uint8_t *data, std::size_t size, FileProbe &probe)
{
    if (size < 12) {
        return;
    }
    probe.width = readU32be(data + 4);
    probe.height = readU32be(data + 8);
}

void parseColr(const std::uint8_t *data, std::size_t size, FileProbe &probe)
{
    if (size < 4) {
        return;
    }
    const std::string type(reinterpret_cast<const char *>(data), 4);
    if (type == "nclx" && size >= 11) {
        probe.hasNclx = true;
        const std::uint16_t primaries = static_cast<std::uint16_t>((data[4] << 8) | data[5]);
        const std::uint16_t transfer = static_cast<std::uint16_t>((data[6] << 8) | data[7]);
        const std::uint16_t matrix = static_cast<std::uint16_t>((data[8] << 8) | data[9]);
        addMetadata(probe, "colr/nclx", size,
                    "primaries=" + std::to_string(primaries) +
                        " transfer=" + std::to_string(transfer) +
                        " matrix=" + std::to_string(matrix));
    }
    else if (type == "prof" || type == "rICC") {
        probe.hasIcc = true;
        addMetadata(probe, "colr/" + type, size);
    }
}

void parsePixi(const std::uint8_t *data, std::size_t size, FileProbe &probe)
{
    if (size < 5) {
        return;
    }
    const std::uint8_t channels = data[4];
    if (size >= 5 + static_cast<std::size_t>(channels)) {
        probe.bitsPerChannel = data[5];
        probe.pixelFormatHint = "pixi/" + std::to_string(channels) + "ch";
    }
}

void parseBoxes(const std::uint8_t *data, std::size_t size, FileProbe &probe, int depth)
{
    if (depth > 8) {
        return;
    }
    std::size_t offset = 0;
    while (offset + 8 <= size) {
        const std::uint64_t boxSize32 = readU32be(data + offset);
        const std::string type(reinterpret_cast<const char *>(data + offset + 4), 4);
        std::size_t headerSize = 8;
        std::uint64_t boxSize = boxSize32;
        if (boxSize == 1) {
            if (offset + 16 > size) {
                return;
            }
            boxSize = (static_cast<std::uint64_t>(readU32be(data + offset + 8)) << 32) |
                      readU32be(data + offset + 12);
            headerSize = 16;
        }
        else if (boxSize == 0) {
            boxSize = size - offset;
        }
        if (boxSize < headerSize || boxSize > size - offset) {
            return;
        }
        const std::uint8_t *payload = data + offset + headerSize;
        const std::size_t payloadSize = static_cast<std::size_t>(boxSize - headerSize);

        if (type == "meta") {
            if (payloadSize > 4) {
                parseBoxes(payload + 4, payloadSize - 4, probe, depth + 1);
            }
        }
        else if (type == "iprp" || type == "ipco" || type == "moov" || type == "trak" ||
                 type == "mdia" || type == "minf" || type == "stbl") {
            parseBoxes(payload, payloadSize, probe, depth + 1);
        }
        else if (type == "ispe") {
            parseIspe(payload, payloadSize, probe);
        }
        else if (type == "colr") {
            parseColr(payload, payloadSize, probe);
        }
        else if (type == "pixi") {
            parsePixi(payload, payloadSize, probe);
        }
        else if (type == "clli" || type == "mdcv") {
            probe.hasHdrStatic = true;
            addMetadata(probe, type, payloadSize);
        }
        else if (type == "tkhd") {
            parseTkhd(payload, payloadSize, probe);
        }
        offset += static_cast<std::size_t>(boxSize);
    }
}

void parseIsobmff(Reader &r, FileProbe &probe)
{
    // ftyp：确定 kind
    std::size_t offset = 0;
    bool sawFtyp = false;
    while (r.need(8)) {
        r.seek(offset);
        const std::uint64_t boxSize32 = r.u32be();
        char type[5] = {};
        r.readBytes(type, 4);
        std::uint64_t boxSize = boxSize32;
        std::size_t headerSize = 8;
        if (boxSize == 1) {
            boxSize = r.u64be();
            headerSize = 16;
        }
        else if (boxSize == 0) {
            boxSize = r.size() - offset;
        }
        if (boxSize < headerSize || boxSize > r.size() - offset) {
            break;
        }
        const std::size_t payloadStart = offset + headerSize;
        const std::size_t payloadSize = static_cast<std::size_t>(boxSize - headerSize);
        if (std::string(type) == "ftyp" && payloadSize >= 8) {
            sawFtyp = true;
            r.seek(payloadStart);
            const std::uint32_t majorBrand = r.u32be();
            r.skip(4); // minor version
            std::vector<std::string> brands;
            brands.push_back(hex4(majorBrand));
            while (r.pos() + 4 <= payloadStart + payloadSize) {
                brands.push_back(hex4(r.u32be()));
            }
            const std::uint8_t major[4] = {static_cast<std::uint8_t>(majorBrand >> 24),
                                           static_cast<std::uint8_t>(majorBrand >> 16),
                                           static_cast<std::uint8_t>(majorBrand >> 8),
                                           static_cast<std::uint8_t>(majorBrand)};
            const std::string brand(reinterpret_cast<const char *>(major), 4);
            if (brand == "avif" || brand == "avis") {
                probe.kind = FileKind::Avif;
            }
            else if (brand == "heic" || brand == "heix" || brand == "hevc" || brand == "hevx" ||
                     brand == "mif1" || brand == "msf1") {
                probe.kind = FileKind::Heif;
            }
            else if (brand == "qt  ") {
                probe.kind = FileKind::Mov;
            }
            else {
                probe.kind = FileKind::Mp4;
            }
            addMetadata(probe, "ftyp", payloadSize, "major=" + brand);
        }
        offset += static_cast<std::size_t>(boxSize);
        if (offset >= r.size()) {
            break;
        }
    }
    if (!sawFtyp) {
        probe.kind = FileKind::Mp4;
        probe.notes.push_back("ISOBMFF 但未找到 ftyp");
    }
    // 完整遍历顶层 box（meta/moov）
    offset = 0;
    r.seek(0);
    while (r.need(8)) {
        r.seek(offset);
        const std::uint64_t boxSize32 = r.u32be();
        char type[5] = {};
        r.readBytes(type, 4);
        std::uint64_t boxSize = boxSize32;
        std::size_t headerSize = 8;
        if (boxSize == 1) {
            boxSize = r.u64be();
            headerSize = 16;
        }
        else if (boxSize == 0) {
            boxSize = r.size() - offset;
        }
        if (boxSize < headerSize || boxSize > r.size() - offset) {
            break;
        }
        const std::string typeString(type);
        const std::size_t payloadStart = offset + headerSize;
        const std::size_t payloadSize = static_cast<std::size_t>(boxSize - headerSize);
        if (typeString == "meta" || typeString == "moov") {
            const std::uint8_t *payload = r.data() + payloadStart;
            if (typeString == "meta") {
                if (payloadSize > 4) {
                    parseBoxes(payload + 4, payloadSize - 4, probe, 0);
                }
            }
            else {
                parseBoxes(payload, payloadSize, probe, 0);
            }
        }
        offset += static_cast<std::size_t>(boxSize);
        if (offset >= r.size()) {
            break;
        }
    }
}

// ==================== 检测 ====================
FileKind detectKind(const std::uint8_t *data, std::size_t size)
{
    if (startsWith(data, size, "\x89PNG\r\n\x1a\n", 8)) {
        return FileKind::Png;
    }
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        return FileKind::Jpeg;
    }
    if (startsWith(data, size, "II*\0", 4) || startsWith(data, size, "MM\0*", 4)) {
        return FileKind::Tiff;
    }
    if (startsWith(data, size, "GIF87a", 6) || startsWith(data, size, "GIF89a", 6)) {
        return FileKind::Gif;
    }
    if (startsWith(data, size, "RIFF", 4) && size >= 12 &&
        std::memcmp(data + 8, "WEBP", 4) == 0) {
        return FileKind::WebP;
    }
    if (size >= 4 && data[0] == 0x76 && data[1] == 0x2F && data[2] == 0x31 && data[3] == 0x01) {
        return FileKind::Exr;
    }
    if (size >= 12 && std::memcmp(data + 4, "ftyp", 4) == 0) {
        return FileKind::Mp4; // 具体 HEIF/AVIF/MOV 由 parseIsobmff 细化
    }
    return FileKind::Unknown;
}

std::string jsonEscape(const std::string &value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result.push_back(c);
            break;
        }
    }
    return result;
}
} // namespace

const char *toString(FileKind kind)
{
    switch (kind) {
    case FileKind::Png:
        return "png";
    case FileKind::Jpeg:
        return "jpeg";
    case FileKind::Tiff:
        return "tiff";
    case FileKind::Gif:
        return "gif";
    case FileKind::WebP:
        return "webp";
    case FileKind::Exr:
        return "exr";
    case FileKind::Heif:
        return "heif";
    case FileKind::Avif:
        return "avif";
    case FileKind::Mp4:
        return "mp4";
    case FileKind::Mov:
        return "mov";
    case FileKind::Unknown:
        break;
    }
    return "unknown";
}

FileProbe probeFile(const std::string &utf8Path)
{
    std::ifstream file(utf8Path, std::ios::binary);
    if (!file) {
        FileProbe probe;
        probe.notes.push_back("无法打开文件: " + utf8Path);
        return probe;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    file.close();
    FileProbe probe = probeBytes(bytes.data(), bytes.size());
    probe.fileSize = bytes.size();
    return probe;
}

FileProbe probeBytes(const std::uint8_t *data, std::size_t size)
{
    FileProbe probe;
    if (data == nullptr || size == 0) {
        probe.notes.push_back("空文件");
        return probe;
    }
    probe.fileSize = size;
    probe.kind = detectKind(data, size);

    Reader reader(data, size);
    switch (probe.kind) {
    case FileKind::Png:
        parsePng(reader, probe);
        break;
    case FileKind::Jpeg:
        parseJpeg(reader, probe);
        break;
    case FileKind::Tiff:
        parseTiff(reader, probe);
        break;
    case FileKind::Gif:
        parseGif(reader, probe);
        break;
    case FileKind::WebP:
        parseWebp(reader, probe);
        break;
    case FileKind::Exr:
        parseExr(reader, probe);
        break;
    case FileKind::Mp4:
    case FileKind::Mov:
    case FileKind::Heif:
    case FileKind::Avif:
        parseIsobmff(reader, probe);
        break;
    case FileKind::Unknown:
        probe.notes.push_back("未识别的容器格式");
        break;
    }

    if (probe.layers == 0) {
        probe.layers = 1;
    }
    if (probe.kind != FileKind::Exr && probe.kind != FileKind::Unknown && probe.bitsPerChannel == 0) {
        probe.notes.push_back("位深未知（容器头未给出）");
    }
    return probe;
}

std::string toJson(const FileProbe &probe)
{
    std::string json = "{\n";
    json += "  \"kind\": \"" + jsonEscape(toString(probe.kind)) + "\",\n";
    json += "  \"file_size\": " + std::to_string(probe.fileSize) + ",\n";
    json += "  \"width\": " + std::to_string(probe.width) + ",\n";
    json += "  \"height\": " + std::to_string(probe.height) + ",\n";
    json += "  \"bits_per_channel\": " + std::to_string(probe.bitsPerChannel) + ",\n";
    json += "  \"pixel_format\": \"" + jsonEscape(probe.pixelFormatHint) + "\",\n";
    json += "  \"frames\": " + std::to_string(probe.frames) + ",\n";
    json += "  \"layers\": " + std::to_string(probe.layers) + ",\n";
    json += "  \"has_icc\": " + std::string(probe.hasIcc ? "true" : "false") + ",\n";
    json += "  \"has_srgb_chunk\": " + std::string(probe.hasSrgbChunk ? "true" : "false") + ",\n";
    json += "  \"has_cicp\": " + std::string(probe.hasCicp ? "true" : "false") + ",\n";
    json += "  \"has_nclx\": " + std::string(probe.hasNclx ? "true" : "false") + ",\n";
    json += "  \"has_hdr_static\": " + std::string(probe.hasHdrStatic ? "true" : "false") + ",\n";
    json += "  \"metadata\": [";
    for (std::size_t i = 0; i < probe.metadata.size(); ++i) {
        const ColorMetadataEntry &entry = probe.metadata[i];
        json += (i == 0 ? "\n" : ",\n");
        json += "    {\"kind\": \"" + jsonEscape(entry.kind) + "\", \"size\": " +
                std::to_string(entry.size);
        if (!entry.detail.empty()) {
            json += ", \"detail\": \"" + jsonEscape(entry.detail) + "\"";
        }
        json += "}";
    }
    json += probe.metadata.empty() ? "],\n" : "\n  ],\n";
    json += "  \"notes\": [";
    for (std::size_t i = 0; i < probe.notes.size(); ++i) {
        json += (i == 0 ? "\n" : ",\n");
        json += "    \"" + jsonEscape(probe.notes[i]) + "\"";
    }
    json += probe.notes.empty() ? "]\n" : "\n  ]\n";
    json += "}\n";
    return json;
}
} // namespace chroma::io
