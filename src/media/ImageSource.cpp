#include "media/ImageSource.h"

#include "gfx/D3D12Common.h"

#include <wincodec.h>

#include <algorithm>

namespace chroma::media {
namespace {

constexpr uint32_t kRgba16Channels = 4;
constexpr uint32_t kBytesPerRgba16 = 8;

/// 检测「10-bit 码 << 6」存储约定：RGB 通道低 6 位都是 0。
/// 注意只检查 RGB：alpha 由 WIC 补成 65535（低 6 位为 0x3F），不能参与判断。
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
    // value16 = code10 << 6  =>  code10 = value16 >> 6  =>  满量程 16-bit = code10/1023*65535
    // alpha 保持不变（恒为 65535）。
    const size_t pixelCount = rgba16.size() / kRgba16Channels;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        for (uint32_t channel = 0; channel < 3; ++channel) {
            uint16_t& value = rgba16[pixel * kRgba16Channels + channel];
            const uint32_t code10 = static_cast<uint32_t>(value) >> 6;
            value = static_cast<uint16_t>((code10 * 65535u + 511u) / 1023u);
        }
    }
}

/// 读取 PNG 的 IHDR 位深（WIC 解码后的像素格式不保留原始位深，需要自己看文件头）。
uint32_t readPngBitDepth(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    unsigned char header[26] = {};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, header, sizeof(header), &read, nullptr);
    CloseHandle(file);

    const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!ok || read < sizeof(header) ||
        !std::equal(std::begin(signature), std::end(signature), header)) {
        return 0;
    }
    // 第 8..11 字节是 IHDR 长度, 12..15 是 "IHDR", 16..19 宽, 20..23 高, 24 位深
    return header[24];
}

} // namespace

bool loadImage(const std::wstring& path, ImageData& out, std::string& error)
{
    out = ImageData{};
    error.clear();

    gfx::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        error = "CoCreateInstance(IWICImagingFactory2) failed";
        return false;
    }

    gfx::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        error = "WIC: 无法打开文件（路径或格式问题）";
        return false;
    }

    gfx::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        error = "WIC: GetFrame(0) failed";
        return false;
    }

    gfx::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        error = "WIC: CreateFormatConverter failed";
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat64bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        error = "WIC: 转换到 64bppRGBA 失败";
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        error = "WIC: 图像尺寸无效";
        return false;
    }

    out.width = width;
    out.height = height;
    out.sourceBitDepth = readPngBitDepth(path);
    out.rgba16.resize(static_cast<size_t>(width) * height * kRgba16Channels);

    const UINT stride = width * kBytesPerRgba16;
    const UINT bufferSize = stride * height;
    hr = converter->CopyPixels(nullptr, stride, bufferSize,
                               reinterpret_cast<BYTE*>(out.rgba16.data()));
    if (FAILED(hr)) {
        error = "WIC: CopyPixels failed";
        out = ImageData{};
        return false;
    }

    if (out.sourceBitDepth == 16 && looksLikeShifted10Bit(out.rgba16)) {
        remapShifted10BitToFullRange(out.rgba16);
        out.reinterpretedFrom10Bit = true;
    }

    return true;
}

} // namespace chroma::media
