#include "Capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace cp
{
namespace
{
float halfToFloat(std::uint16_t value)
{
    const std::uint32_t sign = (value >> 15) & 0x1;
    const std::uint32_t exponent = (value >> 10) & 0x1F;
    const std::uint32_t mantissa = value & 0x3FF;

    if (exponent == 0) {
        if (mantissa == 0) {
            return sign ? -0.0f : 0.0f;
        }
        const float subnormal = static_cast<float>(mantissa) / 1024.0f;
        return (sign ? -1.0f : 1.0f) * std::ldexp(subnormal, -14);
    }
    if (exponent == 0x1F) {
        return mantissa == 0 ? (sign ? -INFINITY : INFINITY) : NAN;
    }

    const float result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                    static_cast<int>(exponent) - 15);
    return sign ? -result : result;
}

BOOL CALLBACK monitorEnumProc(HMONITOR handle, HDC, LPRECT, LPARAM context)
{
    auto *monitors = reinterpret_cast<std::vector<MonitorInfo> *>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(handle, &info)) {
        return TRUE;
    }
    MonitorInfo monitor;
    monitor.handle = handle;
    monitor.rect = info.rcMonitor;
    monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    monitor.gdiName = info.szDevice;
    monitors->push_back(monitor);
    return TRUE;
}

std::string wideToUtf8(const std::wstring &value)
{
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length - 1, nullptr, nullptr);
    return result;
}
} // namespace

std::vector<MonitorInfo> enumerateMonitors()
{
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

MonitorInfo monitorFromPoint(POINT point)
{
    const HMONITOR handle = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MonitorInfo monitor;
    monitor.handle = handle;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(handle, &info)) {
        monitor.rect = info.rcMonitor;
        monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        monitor.gdiName = info.szDevice;
    }
    return monitor;
}

std::string formatToString(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    default:
        return "DXGI_FORMAT_" + std::to_string(static_cast<int>(format));
    }
}

std::string colorEncodingToString(DISPLAYCONFIG_COLOR_ENCODING encoding)
{
    switch (encoding) {
    case DISPLAYCONFIG_COLOR_ENCODING_RGB:
        return "RGB";
    case DISPLAYCONFIG_COLOR_ENCODING_YCBCR444:
        return "YCbCr444";
    case DISPLAYCONFIG_COLOR_ENCODING_YCBCR422:
        return "YCbCr422";
    case DISPLAYCONFIG_COLOR_ENCODING_YCBCR420:
        return "YCbCr420";
    default:
        return "encoding_" + std::to_string(static_cast<int>(encoding));
    }
}

std::string colorSpaceToString(DXGI_COLOR_SPACE_TYPE colorSpace)
{
    switch (colorSpace) {
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        return "RGB_FULL_G22_NONE_P709 (SDR sRGB)";
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        return "RGB_FULL_G2084_NONE_P2020 (HDR10/PQ)";
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        return "RGB_FULL_G10_NONE_P709 (scRGB linear)";
    default:
        return "DXGI_COLOR_SPACE_" + std::to_string(static_cast<int>(colorSpace));
    }
}

bool ScreenCapture::initialize(HMONITOR monitor)
{
    release();
    m_monitor = monitor;
    if (!createDeviceAndDuplication(monitor)) {
        release();
        return false;
    }
    return true;
}

void ScreenCapture::release()
{
    m_duplication.Reset();
    m_copyTexture.Reset();
    m_output6.Reset();
    m_output5.Reset();
    m_output.Reset();
    m_adapter.Reset();
    m_context.Reset();
    m_device.Reset();
    m_monitor = nullptr;
    m_hasFrame = false;
    m_frameFormat = DXGI_FORMAT_UNKNOWN;
    m_width = 0;
    m_height = 0;
    m_desktopRect = {};
}

bool ScreenCapture::ensureCopyTexture(const D3D11_TEXTURE2D_DESC &sourceDesc)
{
    if (m_copyTexture) {
        D3D11_TEXTURE2D_DESC current{};
        m_copyTexture->GetDesc(&current);
        if (current.Format == sourceDesc.Format && current.Width == sourceDesc.Width &&
            current.Height == sourceDesc.Height) {
            return true;
        }
        m_copyTexture.Reset();
    }

    D3D11_TEXTURE2D_DESC desc = sourceDesc;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    return SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, &m_copyTexture));
}

bool ScreenCapture::createDeviceAndDuplication(HMONITOR monitor)
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    ComPtr<IDXGIAdapter1> selectedAdapter;
    ComPtr<IDXGIOutput> selectedOutput;
    RECT desktopRect = {};

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) {
                continue;
            }
            if (desc.Monitor == monitor) {
                selectedAdapter = adapter;
                selectedOutput = output;
                desktopRect = desc.DesktopCoordinates;
                break;
            }
        }
        if (selectedOutput) {
            break;
        }
    }

    if (!selectedOutput) {
        return false;
    }

    m_adapter = selectedAdapter;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    if (SUCCEEDED(selectedAdapter->GetDesc1(&adapterDesc))) {
        m_display.adapterName = adapterDesc.Description;
    }
    m_desktopRect = desktopRect;
    m_width = desktopRect.right - desktopRect.left;
    m_height = desktopRect.bottom - desktopRect.top;

    if (FAILED(D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 &m_device, nullptr, &m_context))) {
        return false;
    }

    if (FAILED(selectedOutput.As(&m_output))) {
        return false;
    }
    selectedOutput.As(&m_output5);
    selectedOutput.As(&m_output6);   // 必须 QI 到 IDXGIOutput6，GetDesc1 才能拿到 HDR 色彩空间
    readOutputDesc1();
    queryDisplayState(monitor);

    if (!recreateDuplication()) {
        return false;
    }
    return true;
}

bool ScreenCapture::recreateDuplication()
{
    m_duplication.Reset();
    m_hasFrame = false;

    if (m_output5) {
        // 先尝试 FP16 + BGRA8（HDR 桌面希望拿到 FP16）。
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT,
                                       DXGI_FORMAT_B8G8R8A8_UNORM};
        if (SUCCEEDED(m_output5->DuplicateOutput1(
                m_device.Get(), 0, static_cast<UINT>(std::size(formats)), formats,
                &m_duplication))) {
            refreshDisplayState();
            return true;
        }
        // 有些驱动不接受 FP16 作为 scan-out 格式，退回纯 BGRA8。
        const DXGI_FORMAT bgraFormats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
        if (SUCCEEDED(m_output5->DuplicateOutput1(
                m_device.Get(), 0, static_cast<UINT>(std::size(bgraFormats)), bgraFormats,
                &m_duplication))) {
            refreshDisplayState();
            return true;
        }
    }

    if (m_output) {
        if (SUCCEEDED(m_output->DuplicateOutput(m_device.Get(), &m_duplication))) {
            refreshDisplayState();
            return true;
        }
    }
    return false;
}

void ScreenCapture::readOutputDesc1()
{
    if (!m_output6) {
        return;
    }
    DXGI_OUTPUT_DESC1 desc1{};
    if (FAILED(m_output6->GetDesc1(&desc1))) {
        return;
    }
    m_display.hdr = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    m_display.colorSpace = desc1.ColorSpace;
    m_display.bitsPerColor = desc1.BitsPerColor;
    m_display.minLuminance = desc1.MinLuminance;
    m_display.maxLuminance = desc1.MaxLuminance;
    m_display.maxFullFrameLuminance = desc1.MaxFullFrameLuminance;
    std::memcpy(m_display.red, desc1.RedPrimary, sizeof(m_display.red));
    std::memcpy(m_display.green, desc1.GreenPrimary, sizeof(m_display.green));
    std::memcpy(m_display.blue, desc1.BluePrimary, sizeof(m_display.blue));
    std::memcpy(m_display.white, desc1.WhitePoint, sizeof(m_display.white));
}

void ScreenCapture::refreshDisplayState()
{
    readOutputDesc1();
    queryDisplayState(m_monitor);
}

bool ScreenCapture::captureFrame()
{
    if (!m_duplication) {
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> resource;
    const HRESULT result = m_duplication->AcquireNextFrame(0, &frameInfo, &resource);

    if (result == DXGI_ERROR_WAIT_TIMEOUT) {
        return m_hasFrame; // 桌面没有更新，继续使用上一次的副本
    }
    if (result == DXGI_ERROR_ACCESS_LOST || result == DXGI_ERROR_INVALID_CALL) {
        if (recreateDuplication()) {
            return m_hasFrame;
        }
        return false;
    }
    if (FAILED(result)) {
        return m_hasFrame;
    }

    ComPtr<ID3D11Texture2D> texture;
    const HRESULT queryResult = resource.As(&texture);
    bool copied = false;
    if (SUCCEEDED(queryResult) && texture) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (ensureCopyTexture(desc)) {
            m_context->CopyResource(m_copyTexture.Get(), texture.Get());
            m_frameFormat = desc.Format;
            m_width = static_cast<int>(desc.Width);
            m_height = static_cast<int>(desc.Height);
            m_hasFrame = true;
            copied = true;
        }
    }
    m_duplication->ReleaseFrame();
    return copied;
}

ScreenCapture::Sample ScreenCapture::sampleRect(int x, int y, int width, int height)
{
    Sample sample;
    if (!m_hasFrame || !m_copyTexture || m_device == nullptr || m_context == nullptr) {
        return sample;
    }

    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(m_width, x + width);
    const int y1 = std::min(m_height, y + height);
    if (x1 <= x0 || y1 <= y0) {
        return sample;
    }

    D3D11_TEXTURE2D_DESC desc{};
    m_copyTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &staging))) {
        return sample;
    }

    const D3D11_BOX box{static_cast<UINT>(x0), static_cast<UINT>(y0), 0,
                        static_cast<UINT>(x1), static_cast<UINT>(y1), 1};
    m_context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, m_copyTexture.Get(), 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return sample;
    }

    const int sampleWidth = x1 - x0;
    const int sampleHeight = y1 - y0;
    sample.ok = true;
    sample.format = m_frameFormat;
    sample.x = x0;
    sample.y = y0;
    sample.width = sampleWidth;
    sample.height = sampleHeight;
    sample.rgba.resize(static_cast<std::size_t>(sampleWidth) * sampleHeight * 4);

    for (int row = 0; row < sampleHeight; ++row) {
        const auto *sourceRow =
            static_cast<const std::uint8_t *>(mapped.pData) + row * mapped.RowPitch;
        for (int col = 0; col < sampleWidth; ++col) {
            const std::size_t base =
                static_cast<std::size_t>(row * sampleWidth + col) * 4;
            if (m_frameFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                const auto *pixel = reinterpret_cast<const std::uint16_t *>(sourceRow) + col * 4;
                sample.rgba[base + 0] = halfToFloat(pixel[0]);
                sample.rgba[base + 1] = halfToFloat(pixel[1]);
                sample.rgba[base + 2] = halfToFloat(pixel[2]);
                sample.rgba[base + 3] = halfToFloat(pixel[3]);
            }
            else if (m_frameFormat == DXGI_FORMAT_B8G8R8A8_UNORM) {
                const auto *pixel = sourceRow + col * 4;
                sample.rgba[base + 0] = pixel[2] / 255.0f; // R
                sample.rgba[base + 1] = pixel[1] / 255.0f; // G
                sample.rgba[base + 2] = pixel[0] / 255.0f; // B
                sample.rgba[base + 3] = pixel[3] / 255.0f; // A
            }
            else if (m_frameFormat == DXGI_FORMAT_R10G10B10A2_UNORM) {
                const auto *pixel = reinterpret_cast<const std::uint32_t *>(sourceRow) + col;
                const std::uint32_t value = *pixel;
                sample.rgba[base + 0] = (value & 0x3FF) / 1023.0f;
                sample.rgba[base + 1] = ((value >> 10) & 0x3FF) / 1023.0f;
                sample.rgba[base + 2] = ((value >> 20) & 0x3FF) / 1023.0f;
                sample.rgba[base + 3] = ((value >> 30) & 0x3) / 3.0f;
            }
            else {
                sample.ok = false;
                break;
            }
        }
    }

    m_context->Unmap(staging.Get(), 0);
    return sample;
}

void ScreenCapture::queryDisplayState(HMONITOR monitor)
{
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        m_display.gdiName = monitorInfo.szDevice;
    }

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                           modes.data(), nullptr) != ERROR_SUCCESS) {
        return;
    }

    for (UINT32 index = 0; index < pathCount; ++index) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = paths[index].sourceInfo.adapterId;
        sourceName.header.id = paths[index].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
            continue;
        }
        if (m_display.gdiName != sourceName.viewGdiDeviceName) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = paths[index].targetInfo.adapterId;
        targetName.header.id = paths[index].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&targetName.header) == ERROR_SUCCESS) {
            m_display.friendlyName = targetName.monitorFriendlyDeviceName;
        }

        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
        colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        colorInfo.header.size = sizeof(colorInfo);
        colorInfo.header.adapterId = paths[index].targetInfo.adapterId;
        colorInfo.header.id = paths[index].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&colorInfo.header) == ERROR_SUCCESS) {
            m_display.advancedColorSupported = (colorInfo.value & 0x1) != 0;
            m_display.advancedColorEnabled = (colorInfo.value & 0x2) != 0;
            m_display.wideColorEnforced = (colorInfo.value & 0x4) != 0;
            m_display.advancedColorForceDisabled = (colorInfo.value & 0x8) != 0;
            m_display.colorEncoding = colorInfo.colorEncoding;
            m_display.configBitsPerColor = colorInfo.bitsPerColorChannel;
        }

        DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevel{};
        whiteLevel.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        whiteLevel.header.size = sizeof(whiteLevel);
        whiteLevel.header.adapterId = paths[index].targetInfo.adapterId;
        whiteLevel.header.id = paths[index].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&whiteLevel.header) == ERROR_SUCCESS) {
            m_display.sdrWhiteNits = whiteLevel.SDRWhiteLevel / 1000.0 * 80.0;
        }
        break;
    }
}
} // namespace cp
