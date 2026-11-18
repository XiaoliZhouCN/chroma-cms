#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cp
{
/// 显示器与 Advanced Color 状态（全部来自官方 API）。
struct DisplayState
{
    bool hdr = false;                 // DXGI_OUTPUT_DESC1.ColorSpace == G2084_NONE_P2020
    bool advancedColorSupported = false;
    bool advancedColorEnabled = false;
    bool wideColorEnforced = false;
    bool advancedColorForceDisabled = false;
    DISPLAYCONFIG_COLOR_ENCODING colorEncoding = DISPLAYCONFIG_COLOR_ENCODING_RGB;
    std::uint32_t configBitsPerColor = 0;
    std::uint32_t bitsPerColor = 0;
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    double sdrWhiteNits = 80.0;       // DISPLAYCONFIG_SDR_WHITE_LEVEL
    double minLuminance = 0.0;
    double maxLuminance = 0.0;
    double maxFullFrameLuminance = 0.0;
    float red[2] = {0.0f, 0.0f};
    float green[2] = {0.0f, 0.0f};
    float blue[2] = {0.0f, 0.0f};
    float white[2] = {0.0f, 0.0f};
    std::wstring gdiName;
    std::wstring friendlyName;
    std::wstring adapterName;
};

struct MonitorInfo
{
    HMONITOR handle = nullptr;
    RECT rect = {};
    bool primary = false;
    std::wstring gdiName;
    std::wstring friendlyName;
};

std::vector<MonitorInfo> enumerateMonitors();
MonitorInfo monitorFromPoint(POINT point);
std::string formatToString(DXGI_FORMAT format);
std::string colorSpaceToString(DXGI_COLOR_SPACE_TYPE colorSpace);
std::string colorEncodingToString(DISPLAYCONFIG_COLOR_ENCODING encoding);

/// D3D11 + Desktop Duplication 的取色器。
///
/// 约定：
///   - 只在当前显示器内取色；坐标是"显示器本地物理像素"。
///   - 捕获格式由 DuplicateOutput1 决定（优先请求 FP16 以保留 HDR 数值）。
///   - 帧会先 CopyResource 到内部纹理，采样时只读需要的区域，避免全帧 readback。
class ScreenCapture
{
public:
    struct Sample
    {
        bool ok = false;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::vector<float> rgba; // 顺序 R,G,B,A；FP16 为线性 scRGB，BGRA8 为 0..1 的 sRGB 编码值
    };

    bool initialize(HMONITOR monitor);
    void release();

    bool captureFrame();
    void refreshDisplayState();
    Sample sampleRect(int x, int y, int width, int height);

    const DisplayState &display() const { return m_display; }
    bool hdr() const { return m_display.hdr; }
    DXGI_FORMAT frameFormat() const { return m_frameFormat; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    const RECT &desktopRect() const { return m_desktopRect; }

private:
    bool createDeviceAndDuplication(HMONITOR monitor);
    bool recreateDuplication();
    void readOutputDesc1();
    void queryDisplayState(HMONITOR monitor);
    bool ensureCopyTexture(const D3D11_TEXTURE2D_DESC &desc);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_adapter;
    Microsoft::WRL::ComPtr<IDXGIOutput1> m_output;
    Microsoft::WRL::ComPtr<IDXGIOutput5> m_output5;
    Microsoft::WRL::ComPtr<IDXGIOutput6> m_output6;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_copyTexture;

    HMONITOR m_monitor = nullptr;
    RECT m_desktopRect = {};
    DXGI_FORMAT m_frameFormat = DXGI_FORMAT_UNKNOWN;
    int m_width = 0;
    int m_height = 0;
    bool m_hasFrame = false;
    DisplayState m_display;
};
} // namespace cp
