#include "media/ImageLoader.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <windows.h>

#include <cstring>

namespace chroma::media {
namespace {

std::string avErrorText(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return std::string(buffer);
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

} // namespace

bool loadImage(const std::wstring& path, Image& out, std::string& error)
{
    out = Image{};
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
        // 1. 打开文件、读流信息
        int result = avformat_open_input(&format, utf8Path.c_str(), nullptr, nullptr);
        if (result < 0) {
            error = "FFmpeg: 打不开文件（" + avErrorText(result) + "）";
            break;
        }
        if (avformat_find_stream_info(format, nullptr) < 0) {
            error = "FFmpeg: 读不到流信息";
            break;
        }

        // 2. 找视频/图像流 + 解码器
        const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (streamIndex < 0) {
            error = "FFmpeg: 文件里没有图像流";
            break;
        }
        AVStream* stream = format->streams[streamIndex];

        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (decoder == nullptr) {
            error = "FFmpeg: 找不到解码器";
            break;
        }

        codecContext = avcodec_alloc_context3(decoder);
        if (codecContext == nullptr ||
            avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
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

        // 3. 解出第一帧
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
            // 单帧图片有时要 flush 才出帧
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
        out.width = static_cast<std::uint32_t>(width);
        out.height = static_cast<std::uint32_t>(height);

        // 4. 统一转成 RGBA16（sws 只做像素布局转换，不做色彩转换）
        sws = sws_getContext(width, height, static_cast<AVPixelFormat>(frame->format), width, height,
                             AV_PIX_FMT_RGBA64LE, SWS_POINT, nullptr, nullptr, nullptr);
        if (sws == nullptr) {
            error = "FFmpeg: 不支持的像素格式";
            break;
        }

        out.rgba16.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        std::uint8_t* destination[1] = {reinterpret_cast<std::uint8_t*>(out.rgba16.data())};
        const int destinationStride[1] = {width * 8};  // RGBA16 = 8 字节/像素
        sws_scale(sws, frame->data, frame->linesize, 0, height, destination, destinationStride);

        // 5. 识别「10-bit 码左移 6 位」的存储约定（PTMR 图就是这种）
        //    value16 = code10 << 6，三个 RGB 通道低 6 位应全为 0（alpha 恒为 0xFFFF，不参与判断）
        bool shifted10Bit = true;
        const size_t pixelCount = static_cast<size_t>(width) * height;
        for (size_t i = 0; i < pixelCount && shifted10Bit; ++i) {
            for (int c = 0; c < 3; ++c) {
                if ((out.rgba16[i * 4 + c] & 0x3Fu) != 0u) {
                    shifted10Bit = false;
                    break;
                }
            }
        }
        if (shifted10Bit) {
            // code10 = value16 >> 6；重映射到满量程 16-bit：code10/1023*65535
            for (size_t i = 0; i < pixelCount; ++i) {
                for (int c = 0; c < 3; ++c) {
                    const std::uint32_t code10 = static_cast<std::uint32_t>(out.rgba16[i * 4 + c]) >> 6;
                    out.rgba16[i * 4 + c] = static_cast<std::uint16_t>((code10 * 65535u + 511u) / 1023u);
                }
            }
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
        out = Image{};
    }
    return ok;
}

} // namespace chroma::media