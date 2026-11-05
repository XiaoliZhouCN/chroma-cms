#include "media/ImageLoader.h"

// FFmpeg 的头文件不自己声明 C 链接（符号会按 C++ 名字修饰，链接期统统找不到），
// 必须由调用方包 extern "C"。这是 MSVC 下最典型的 FFmpeg 链接错误来源。
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace chroma::media {
namespace {

constexpr uint32_t kRgba16Channels = 4;
constexpr uint32_t kBytesPerRgba16 = 8;

std::string avErrorText(int code)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return std::string(buffer.data());
}

std::string toUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()), nullptr, 0, nullptr,
                                         nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

uint32_t readBigEndian32(const unsigned char* bytes)
{
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

/// PNG 的彩色元数据 chunk。cICP 是 PNG 第三版（ISO/IEC 15948:2025）新增的，
/// FFmpeg 的解码器不一定把它映射到 AVFrame，所以这里自己读一遍作为权威来源。
struct PngChunkInfo {
    bool hasCicp = false;
    std::array<unsigned char, 4> cicp{};
    bool hasIccp = false;
    uint32_t ihdrBitDepth = 0;
};

PngChunkInfo readPngChunkInfo(const std::string& utf8Path)
{
    PngChunkInfo info;

    std::ifstream stream(utf8Path, std::ios::binary);
    if (!stream) {
        return info;
    }

    const std::array<unsigned char, 8> signature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::array<unsigned char, 8> header{};
    if (!stream.read(reinterpret_cast<char*>(header.data()), header.size())) {
        return info;
    }
    if (header != signature) {
        return info;
    }

    while (stream.read(reinterpret_cast<char*>(header.data()), header.size())) {
        const uint32_t length = readBigEndian32(header.data());
        char type[5] = {};
        std::memcpy(type, header.data() + 4, 4);

        if (std::strcmp(type, "IHDR") == 0) {
            unsigned char data[13] = {};
            if (!stream.read(reinterpret_cast<char*>(data), sizeof(data))) {
                break;
            }
            info.ihdrBitDepth = data[8];
            stream.seekg(static_cast<std::streamoff>(length) - 13 + 4, std::ios::cur);
        }
        else if (std::strcmp(type, "cICP") == 0) {
            std::array<unsigned char, 4> payload{};
            if (!stream.read(reinterpret_cast<char*>(payload.data()), payload.size())) {
                break;
            }
            info.cicp = payload;
            info.hasCicp = true;
            stream.seekg(static_cast<std::streamoff>(length) - 4 + 4, std::ios::cur);
        }
        else if (std::strcmp(type, "iCCP") == 0) {
            info.hasIccp = true;
            stream.seekg(static_cast<std::streamoff>(length) + 4, std::ios::cur);
        }
        else if (std::strcmp(type, "IDAT") == 0 || std::strcmp(type, "IEND") == 0) {
            break;  // 元数据 chunk 一定在 IDAT 之前
        }
        else {
            stream.seekg(static_cast<std::streamoff>(length) + 4, std::ios::cur);
        }

        if (!stream) {
            break;
        }
    }

    return info;
}

/// 「10-bit 码左移 6 位」的存储约定：RGB 通道低 6 位都是 0。
/// 只检查 RGB：alpha 是 65535（低 6 位 0x3F），不能参与判断。
bool looksLikeShifted10Bit(const std::vector<uint16_t>& rgba16)
{
    const size_t pixelCount = rgba16.size() / kRgba16Channels;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        for (uint32_t channel = 0; channel < 3; ++channel) {
            if ((rgba16[pixel * kRgba16Channels + channel] & 0x3Fu) != 0u) {
                return false;
            }
        }
    }
    return true;
}

void remapShifted10BitToFullRange(std::vector<uint16_t>& rgba16)
{
    const size_t pixelCount = rgba16.size() / kRgba16Channels;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        for (uint32_t channel = 0; channel < 3; ++channel) {
            uint16_t& value = rgba16[pixel * kRgba16Channels + channel];
            const uint32_t code10 = static_cast<uint32_t>(value) >> 6;
            value = static_cast<uint16_t>((code10 * 65535u + 511u) / 1023u);
        }
    }
}

int pickFirstKnown(int preferred, int fallback, int unspecified)
{
    return (preferred != unspecified) ? preferred : fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// 元数据名称（状态栏用；返回中文，便于直接展示）
// ---------------------------------------------------------------------------

const char* colorPrimariesName(int code)
{
    switch (code) {
    case 1: return "BT.709";
    case 4: return "BT.470M";
    case 5: return "BT.470BG";
    case 6: return "BT.601(525)";
    case 7: return "SMPTE240M";
    case 8: return "Film";
    case 9: return "BT.2020";
    case 10: return "SMPTE428";
    case 11: return "SMPTE431(P3)";
    case 12: return "SMPTE432(P3-D65)";
    case 22: return "EBU3213";
    default: return "未声明";
    }
}

const char* colorTransferName(int code)
{
    switch (code) {
    case 1: return "BT.709";
    case 4: return "Gamma2.2";
    case 5: return "Gamma2.8";
    case 6: return "BT.601";
    case 7: return "SMPTE240M";
    case 8: return "Linear";
    case 11: return "xvYCC";
    case 12: return "BT.1361";
    case 13: return "sRGB";
    case 14: return "BT.2020(10bit)";
    case 15: return "BT.2020(12bit)";
    case 16: return "PQ";
    case 17: return "SMPTE428";
    case 18: return "HLG";
    default: return "未声明";
    }
}

const char* colorMatrixName(int code)
{
    switch (code) {
    case 0: return "Identity(RGB)";
    case 1: return "BT.709";
    case 4: return "FCC";
    case 5: return "BT.470BG";
    case 6: return "BT.601";
    case 7: return "SMPTE240M";
    case 8: return "YCgCo";
    case 9: return "BT.2020-NCL";
    case 10: return "BT.2020-CL";
    case 14: return "ICtCp";
    default: return "未声明";
    }
}

const char* colorRangeName(int code)
{
    switch (code) {
    case 0: return "limited";
    case 1: return "full";
    default: return "未声明";
    }
}

// ---------------------------------------------------------------------------
// 解码
// ---------------------------------------------------------------------------

bool loadImage(const std::wstring& path, ImageData& out, std::string& error)
{
    out = ImageData{};
    error.clear();

    const std::string utf8Path = toUtf8(path);
    if (utf8Path.empty()) {
        error = "路径转换失败";
        return false;
    }

    AVFormatContext* format = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* sws = nullptr;
    bool ok = false;

    do {
        int result = avformat_open_input(&format, utf8Path.c_str(), nullptr, nullptr);
        if (result < 0) {
            error = "FFmpeg: 打不开文件（" + avErrorText(result) + "）";
            break;
        }
        if (avformat_find_stream_info(format, nullptr) < 0) {
            error = "FFmpeg: 读不到流信息";
            break;
        }

        const int streamIndex =
            av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (streamIndex < 0) {
            error = "FFmpeg: 文件里没有视频/图像流";
            break;
        }
        AVStream* stream = format->streams[streamIndex];

        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (decoder == nullptr) {
            error = "FFmpeg: 找不到解码器";
            break;
        }

        codecContext = avcodec_alloc_context3(decoder);
        if (codecContext == nullptr || avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
            error = "FFmpeg: 创建解码上下文失败";
            break;
        }
        if (avcodec_open2(codecContext, decoder, nullptr) < 0) {
            error = "FFmpeg: 打开解码器失败";
            break;
        }

        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (packet == nullptr || frame == nullptr) {
            error = "FFmpeg: 分配 packet/frame 失败";
            break;
        }

        bool gotFrame = false;
        while (!gotFrame && av_read_frame(format, packet) >= 0) {
            if (packet->stream_index != streamIndex) {
                av_packet_unref(packet);
                continue;
            }
            if (avcodec_send_packet(codecContext, packet) >= 0 &&
                avcodec_receive_frame(codecContext, frame) == 0) {
                gotFrame = true;
            }
            av_packet_unref(packet);
        }

        if (!gotFrame) {
            // 有些单帧图像需要 flush 才吐帧
            avcodec_send_packet(codecContext, nullptr);
            if (avcodec_receive_frame(codecContext, frame) < 0) {
                error = "FFmpeg: 解不出任何一帧";
                break;
            }
        }

        const int width = frame->width;
        const int height = frame->height;
        if (width <= 0 || height <= 0) {
            error = "FFmpeg: 图像尺寸无效";
            break;
        }
        out.width = static_cast<uint32_t>(width);
        out.height = static_cast<uint32_t>(height);

        // ---- 色彩元数据 ----
        ColorMetadata& color = out.color;
        color.primaries = pickFirstKnown(frame->color_primaries, codecContext->color_primaries,
                                         AVCOL_PRI_UNSPECIFIED);
        color.transfer = pickFirstKnown(frame->color_trc, codecContext->color_trc,
                                        AVCOL_TRC_UNSPECIFIED);
        color.matrix = pickFirstKnown(frame->colorspace, codecContext->colorspace,
                                      AVCOL_SPC_UNSPECIFIED);
        color.range = pickFirstKnown(frame->color_range, codecContext->color_range,
                                     AVCOL_RANGE_UNSPECIFIED);
        color.source = "frame";

        // PNG 容器层优先：cICP 是权威值，且 FFmpeg 不保证映射
        const PngChunkInfo png = readPngChunkInfo(utf8Path);
        if (png.hasCicp) {
            color.primaries = png.cicp[0];
            color.transfer = png.cicp[1];
            color.matrix = png.cicp[2];
            color.range = png.cicp[3];
            color.source = "cICP";
        }
        else if (png.hasIccp) {
            color.source = "iCCP(未解析)";
        }
        color.metadataFromFile =
            (color.primaries != AVCOL_PRI_UNSPECIFIED) || (color.transfer != AVCOL_TRC_UNSPECIFIED) ||
            (color.matrix != AVCOL_SPC_UNSPECIFIED) || (color.range != AVCOL_RANGE_UNSPECIFIED);

        // ---- 像素：统一转成 RGBA16 ----
        sws = sws_getContext(width, height, static_cast<AVPixelFormat>(frame->format), width,
                             height, AV_PIX_FMT_RGBA64LE, SWS_POINT, nullptr, nullptr, nullptr);
        if (sws == nullptr) {
            error = "FFmpeg: 不支持的像素格式";
            break;
        }

        out.rgba16.resize(static_cast<size_t>(width) * static_cast<size_t>(height) *
                          kRgba16Channels);
        uint8_t* destination[1] = {reinterpret_cast<uint8_t*>(out.rgba16.data())};
        const int destinationStride[1] = {width * static_cast<int>(kBytesPerRgba16)};
        sws_scale(sws, frame->data, frame->linesize, 0, height, destination, destinationStride);

        // ---- 位深与存储约定 ----
        const AVPixFmtDescriptor* descriptor =
            av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
        out.sourceBitDepth =
            (descriptor != nullptr && descriptor->comp[0].depth > 8) ? 16u : 8u;

        if (out.sourceBitDepth == 16 && looksLikeShifted10Bit(out.rgba16)) {
            remapShifted10BitToFullRange(out.rgba16);
            out.reinterpretedFrom10Bit = true;
        }

        ok = true;
    } while (false);

    if (sws != nullptr) {
        sws_freeContext(sws);
    }
    if (frame != nullptr) {
        av_frame_free(&frame);
    }
    if (packet != nullptr) {
        av_packet_free(&packet);
    }
    if (codecContext != nullptr) {
        avcodec_free_context(&codecContext);
    }
    if (format != nullptr) {
        avformat_close_input(&format);
    }

    if (!ok) {
        out = ImageData{};
    }
    return ok;
}

} // namespace chroma::media
