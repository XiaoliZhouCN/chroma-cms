#include "WindowsCaptureSession.hpp"

#include "WindowsConversions.hpp"

#include <windows.h>

#include <algorithm>
#include <iterator>

namespace cp::win
{
bool WindowsCaptureSession::initialize(const cp::MonitorInfo &monitor, cp::NativeWindow owner)
{
    const auto handle = reinterpret_cast<HMONITOR>(monitor.nativeHandle);
    if (!m_capture.initialize(handle, reinterpret_cast<HWND>(owner))) {
        return false;
    }
    syncDisplay();
    return true;
}

void WindowsCaptureSession::release()
{
    m_capture.release();
    m_display = {};
}

void WindowsCaptureSession::syncDisplay()
{
    const cp::win::DisplayState &source = m_capture.display();
    m_display.hdr = source.hdr;
    m_display.advancedColorSupported = source.advancedColorSupported;
    m_display.advancedColorEnabled = source.advancedColorEnabled;
    m_display.wideColorEnforced = source.wideColorEnforced;
    m_display.advancedColorForceDisabled = source.advancedColorForceDisabled;
    m_display.colorEncoding = cp::win::colorEncodingToString(source.colorEncoding);
    m_display.configBitsPerColor = source.configBitsPerColor;
    m_display.bitsPerColor = source.bitsPerColor;
    m_display.colorSpace = cp::win::colorSpaceToString(source.colorSpace);
    switch (source.colorSpace) {
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        m_display.wireFormat = "BT.2020 + PQ（ST.2084）";
        break;
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        m_display.wireFormat = "BT.709 + gamma 2.2（sRGB）";
        break;
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        m_display.wireFormat = "BT.709 + 线性（scRGB）";
        break;
    default:
        m_display.wireFormat = m_display.colorSpace;
        break;
    }
    m_display.sdrWhiteLevelRaw = source.sdrWhiteLevelRaw;
    m_display.sdrWhiteNits = source.sdrWhiteNits;
    m_display.minLuminance = source.minLuminance;
    m_display.maxLuminance = source.maxLuminance;
    m_display.maxFullFrameLuminance = source.maxFullFrameLuminance;
    std::copy(std::begin(source.red), std::end(source.red), std::begin(m_display.red));
    std::copy(std::begin(source.green), std::end(source.green), std::begin(m_display.green));
    std::copy(std::begin(source.blue), std::end(source.blue), std::begin(m_display.blue));
    std::copy(std::begin(source.white), std::end(source.white), std::begin(m_display.white));
    m_display.gdiName = cp::win::toUtf8(source.gdiName);
    m_display.friendlyName = cp::win::toUtf8(source.friendlyName);
    m_display.adapterName = cp::win::toUtf8(source.adapterName);
    m_display.remoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;
}

bool WindowsCaptureSession::captureFrame()
{
    return m_capture.captureFrame();
}

void WindowsCaptureSession::refreshDisplayState()
{
    m_capture.refreshDisplayState();
    syncDisplay();
}

cp::ColorSample WindowsCaptureSession::sampleRect(int x, int y, int width, int height)
{
    const cp::win::ScreenCapture::Sample source = m_capture.sampleRect(x, y, width, height);
    cp::ColorSample sample;
    sample.ok = source.ok;
    sample.format = cp::win::captureFormatFromDxgi(source.format);
    sample.x = source.x;
    sample.y = source.y;
    sample.width = source.width;
    sample.height = source.height;
    sample.rgba = source.rgba;
    return sample;
}

cp::CaptureFormat WindowsCaptureSession::format() const
{
    return cp::win::captureFormatFromDxgi(m_capture.frameFormat());
}

cp::CaptureBackendKind WindowsCaptureSession::backend() const
{
    switch (m_capture.backend()) {
    case cp::win::ScreenCapture::Backend::Wgc:
        return cp::CaptureBackendKind::Wgc;
    case cp::win::ScreenCapture::Backend::Duplication:
        return cp::CaptureBackendKind::Duplication;
    }
    return cp::CaptureBackendKind::Unknown;
}

const char *WindowsCaptureSession::backendName() const
{
    return m_capture.backendName();
}

int WindowsCaptureSession::width() const
{
    return m_capture.width();
}

int WindowsCaptureSession::height() const
{
    return m_capture.height();
}
} // namespace cp::win
