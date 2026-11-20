#include "WindowsConversions.hpp"

#include <windows.h>

namespace cp::win
{
std::string toUtf8(const std::wstring &value)
{
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr,
                                           nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length - 1, nullptr, nullptr);
    return result;
}

cp::CaptureFormat captureFormatFromDxgi(DXGI_FORMAT format)
{
    cp::CaptureFormat result;
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        result.encoding = cp::PixelEncoding::Fp16;
        result.bitsPerChannel = 16;
        result.linear = true;
        result.name = "R16G16B16A16_FLOAT";
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        result.encoding = cp::PixelEncoding::Bgra8;
        result.bitsPerChannel = 8;
        result.linear = false;
        result.name = "B8G8R8A8_UNORM";
        break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        result.encoding = cp::PixelEncoding::R10G10B10A2;
        result.bitsPerChannel = 10;
        result.linear = false;
        result.name = "R10G10B10A2_UNORM";
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        result.encoding = cp::PixelEncoding::Bgra8;
        result.bitsPerChannel = 8;
        result.linear = false;
        result.name = "R8G8B8A8_UNORM";
        break;
    default:
        result.encoding = cp::PixelEncoding::Unknown;
        result.name = "DXGI_FORMAT_" + std::to_string(static_cast<int>(format));
        break;
    }
    return result;
}
} // namespace cp::win
