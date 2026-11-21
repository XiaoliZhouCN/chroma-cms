#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cp
{
/// 与平台无关的原生窗口句柄：Windows 为 HWND，macOS 为 NSView*。
using NativeWindow = std::uintptr_t;

struct PointI
{
    int x = 0;
    int y = 0;
};

struct RectI
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const { return right - left; }
    int height() const { return bottom - top; }
};

/// 显示器描述：坐标统一为"全局物理像素"。
struct MonitorInfo
{
    std::string id;              // 平台稳定标识，例如 Windows 的 \\.\DISPLAY1
    std::string name;            // 友好名称（平台能提供时）
    RectI bounds;
    double scale = 1.0;          // point → pixel 比例；Windows 通常为 1.0
    bool primary = false;
    std::uintptr_t nativeHandle = 0; // HMONITOR / CGDirectDisplayID
};

enum class PixelEncoding
{
    Unknown,
    Fp16,          // R16G16B16A16_FLOAT：Windows 为 scRGB linear
    Rgba16Float,   // macOS ScreenCaptureKit RGhA：16F RGBA
    Bgra8,         // B8G8R8A8_UNORM：SDR 编码值
    R10G10B10A2,
    YCbCr10,       // macOS ScreenCaptureKit HDR 预留
};

struct CaptureFormat
{
    PixelEncoding encoding = PixelEncoding::Unknown;
    int bitsPerChannel = 0;
    bool linear = false;
    std::string name = "unknown";

    // macOS ScreenCaptureKit 的实际配置；其他后端可为空。
    std::string pixelFormat;
    std::string colorSpaceName;
    std::string colorMatrix;
};

/// 显示器/输出信息。可选字段在平台不提供时为 nullopt。
struct DisplayInfo
{
    bool hdr = false;
    bool advancedColorSupported = false;
    bool advancedColorEnabled = false;
    bool wideColorEnforced = false;
    bool advancedColorForceDisabled = false;
    std::string colorEncoding = "unknown";
    std::uint32_t configBitsPerColor = 0;
    std::uint32_t bitsPerColor = 0;
    std::string colorSpace = "unknown";   // 管理后/捕获输出空间
    std::string displayIcc;              // 显示器实际 ICC profile 名称
    std::string displayGamut;            // 显示器 ICC 色域，例如 Display P3
    std::string displayTransfer;        // 显示器 ICC 传递函数，例如 sRGB
    std::string displayWhitePoint;      // 白点 xy
    std::string displayMatrix;          // 3x3 显示矩阵，多行
    std::string wireFormat; // UI 用短描述；为空时 UI 回退到 colorSpace
    std::optional<std::uint32_t> sdrWhiteLevelRaw;
    std::optional<double> sdrWhiteNits;
    double minLuminance = 0.0;
    std::optional<double> maxLuminance;
    double maxFullFrameLuminance = 0.0;
    float red[2] = {0.0f, 0.0f};
    float green[2] = {0.0f, 0.0f};
    float blue[2] = {0.0f, 0.0f};
    float white[2] = {0.0f, 0.0f};
    std::string gdiName;       // Windows: \\.\DISPLAY1；macOS: 平台标识
    std::string friendlyName;
    std::string adapterName;
    bool remoteSession = false;
    double edrHeadroomCurrent = 1.0;
    double edrHeadroomPotential = 1.0;
};

struct ColorSample
{
    bool ok = false;
    CaptureFormat format;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<float> rgba; // R,G,B,A；编码由 format 决定
};

enum class CaptureBackendKind
{
    Unknown,
    Duplication,
    Wgc,
    ScreenCaptureKit,
};
} // namespace cp
