#include "MacCaptureSession.hpp"

#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>
#import <ColorSync/ColorSync.h>

#include <array>
#include <cstdint>
#include <cstdio>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

namespace cp::mac
{
namespace
{
std::string cfStringToStd(CFStringRef value)
{
    if (value == nullptr) {
        return {};
    }
    char buffer[256] = {};
    if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return std::string(buffer);
    }
    return {};
}

NSScreen *screenForDisplay(CGDirectDisplayID display)
{
    for (NSScreen *screen in [NSScreen screens]) {
        NSNumber *number = screen.deviceDescription[NSDeviceDescriptionKey(@"NSScreenNumber")];
        if (number != nil && number.unsignedIntValue == display) {
            return screen;
        }
    }
    return nil;
}

bool supportsHdrCapture(CGDirectDisplayID display)
{
#if defined(__arm64__)
    if (@available(macOS 15.0, *)) {
        NSScreen *screen = screenForDisplay(display);
        return screen != nil &&
               screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
    }
#else
    (void)display;
#endif
    return false;
}

constexpr std::uint32_t fourCC(char a, char b, char c, char d)
{
    return (static_cast<std::uint32_t>(a) << 24) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(c) << 8) |
           static_cast<std::uint32_t>(d);
}

double pqCodeToNits(double encoded)
{
    // ST.2084 / PQ EOTF；1.0 = 10000 nits。
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 32.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 128.0;
    constexpr double c3 = 2392.0 / 128.0;
    encoded = std::clamp(encoded, 0.0, 1.0);
    const double p = std::pow(encoded, 1.0 / m2);
    const double numerator = std::max(p - c1, 0.0);
    const double denominator = std::max(c2 - c3 * p, 1e-9);
    return 10000.0 * std::pow(numerator / denominator, 1.0 / m1);
}

double code10(std::uint16_t value)
{
    // xf44 是 10-bit 数据装在 16-bit container；通常左移 6 位。
    if (value <= 1023) {
        return static_cast<double>(value) / 1023.0;
    }
    // 暂时按 16-bit 容器满量程读取；xf44 的 10-bit 对齐方式
    // 不应由测试图生成器的 <<6 存储方式推断，需要实际 raw 数据验证。
    return static_cast<double>(value) / 65535.0;
}

std::string displayIccName(CGDirectDisplayID display)
{
    if (NSScreen *screen = screenForDisplay(display)) {
        NSString *name = screen.colorSpace.localizedName;
        if (name != nil && name.length > 0) {
            return std::string(name.UTF8String);
        }
    }
    CGColorSpaceRef colorSpace = CGDisplayCopyColorSpace(display);
    if (colorSpace != nullptr) {
        CFStringRef name = CGColorSpaceCopyName(colorSpace);
        CGColorSpaceRelease(colorSpace);
        if (name != nullptr) {
            std::string result;
            char buffer[256] = {};
            if (CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                result = buffer;
            }
            CFRelease(name);
            return result;
        }
    }
    return "Display ICC";
}


struct DisplayProfileInfo
{
    std::string gamut;
    std::string transfer;
    std::string whitePoint;
    std::string matrix;
};

double readS15Fixed16(const std::uint8_t *data)
{
    const std::int32_t raw = (static_cast<std::int32_t>(data[0]) << 24) |
                             (static_cast<std::int32_t>(data[1]) << 16) |
                             (static_cast<std::int32_t>(data[2]) << 8) |
                             static_cast<std::int32_t>(data[3]);
    return static_cast<double>(raw) / 65536.0;
}

bool parseXYZ(CFDataRef data, std::array<double, 3> &out)
{
    if (data == nullptr || CFDataGetLength(data) < 20) {
        return false;
    }
    const auto *bytes = CFDataGetBytePtr(data);
    out[0] = readS15Fixed16(bytes + 8);
    out[1] = readS15Fixed16(bytes + 12);
    out[2] = readS15Fixed16(bytes + 16);
    return true;
}

std::pair<double, double> xyzToXy(const std::array<double, 3> &xyz)
{
    const double sum = xyz[0] + xyz[1] + xyz[2];
    if (std::abs(sum) < 1e-9) {
        return {0.0, 0.0};
    }
    return {xyz[0] / sum, xyz[1] / sum};
}

std::string classifyGamut(const std::array<double, 3> &red,
                          const std::array<double, 3> &green,
                          const std::array<double, 3> &blue)
{
    const auto [rx, ry] = xyzToXy(red);
    const auto [gx, gy] = xyzToXy(green);
    const auto [bx, by] = xyzToXy(blue);
    struct KnownGamut
    {
        const char *name;
        double rx, ry, gx, gy, bx, by;
        double tolerance;
    };
    constexpr KnownGamut known[] = {
        {"sRGB / Rec.709", 0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.006},
        {"Display P3", 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.006},
        {"Adobe RGB", 0.640, 0.330, 0.210, 0.710, 0.150, 0.060, 0.006},
        {"Rec.2020", 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.040},
    };
    for (const KnownGamut &g : known) {
        if (std::hypot(rx - g.rx, ry - g.ry) < g.tolerance &&
            std::hypot(gx - g.gx, gy - g.gy) < g.tolerance &&
            std::hypot(bx - g.bx, by - g.by) < g.tolerance) {
            return g.name;
        }
    }
    char buffer[128] = {};
    std::snprintf(buffer, sizeof(buffer),
                  "Custom  R(%.4f,%.4f) G(%.4f,%.4f) B(%.4f,%.4f)",
                  rx, ry, gx, gy, bx, by);
    return buffer;
}

std::string classifyTransfer(ColorSyncProfileRef profile)
{
    CFDataRef trc = ColorSyncProfileCopyTag(profile, CFSTR("rTRC"));
    if (trc == nullptr) {
        return "unknown";
    }
    const auto *bytes = CFDataGetBytePtr(trc);
    const CFIndex length = CFDataGetLength(trc);
    std::string result = "unknown";
    if (length >= 12) {
        const bool para = std::memcmp(bytes, "para", 4) == 0;
        const bool curv = std::memcmp(bytes, "curv", 4) == 0;
        if (curv && length >= 14) {
            const std::uint32_t count =
                (static_cast<std::uint32_t>(bytes[8]) << 24) |
                (static_cast<std::uint32_t>(bytes[9]) << 16) |
                (static_cast<std::uint32_t>(bytes[10]) << 8) |
                static_cast<std::uint32_t>(bytes[11]);
            if (count == 1) {
                const double gamma =
                    static_cast<double>((static_cast<std::uint16_t>(bytes[12]) << 8) |
                                        static_cast<std::uint16_t>(bytes[13])) / 256.0;
                char buffer[64] = {};
                std::snprintf(buffer, sizeof(buffer), "Gamma %.2f", gamma);
                result = buffer;
            }
            else if (count > 1 && length >= 12 + 2 * count) {
                const std::uint16_t mid =
                    (static_cast<std::uint16_t>(bytes[12 + 2 * (count / 2)]) << 8) |
                    static_cast<std::uint16_t>(bytes[13 + 2 * (count / 2)]);
                const double value = static_cast<double>(mid) / 65535.0;
                if (std::abs(value - 0.2140) < 0.010) {
                    result = "sRGB";
                }
                else if (std::abs(value - 0.2180) < 0.010) {
                    result = "Gamma 2.2";
                }
                else {
                    char buffer[64] = {};
                    std::snprintf(buffer, sizeof(buffer), "Curve (mid %.4f)", value);
                    result = buffer;
                }
            }
        }
        else if (para) {
            const std::uint16_t functionType = length >= 12
                ? (static_cast<std::uint16_t>(bytes[8]) << 8) |
                      static_cast<std::uint16_t>(bytes[9])
                : 0;
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "Parametric TRC (type %u)", functionType);
            result = buffer;
        }
    }
    CFRelease(trc);
    if (ColorSyncProfileContainsTag(profile, CFSTR("vcgt"))) {
        result += " + VCGT";
    }
    return result;
}

DisplayProfileInfo analyzeDisplayProfile(CGDirectDisplayID displayID)
{
    DisplayProfileInfo info;
    ColorSyncProfileRef profile = ColorSyncProfileCreateWithDisplayID(displayID);
    if (profile == nullptr) {
        return info;
    }
    std::array<double, 3> red = {};
    std::array<double, 3> green = {};
    std::array<double, 3> blue = {};
    std::array<double, 3> white = {};
    CFDataRef rTag = ColorSyncProfileCopyTag(profile, CFSTR("rXYZ"));
    CFDataRef gTag = ColorSyncProfileCopyTag(profile, CFSTR("gXYZ"));
    CFDataRef bTag = ColorSyncProfileCopyTag(profile, CFSTR("bXYZ"));
    CFDataRef wTag = ColorSyncProfileCopyTag(profile, CFSTR("wtpt"));
    const bool haveMatrix = parseXYZ(rTag, red) && parseXYZ(gTag, green) && parseXYZ(bTag, blue);
    if (haveMatrix) {
        info.gamut = classifyGamut(red, green, blue);
        char matrix[512] = {};
        std::snprintf(matrix, sizeof(matrix),
                      "X  %+.5f  %+.5f  %+.5f\n"
                      "Y  %+.5f  %+.5f  %+.5f\n"
                      "Z  %+.5f  %+.5f  %+.5f",
                      red[0], green[0], blue[0],
                      red[1], green[1], blue[1],
                      red[2], green[2], blue[2]);
        info.matrix = matrix;
    }
    if (parseXYZ(wTag, white)) {
        const auto [wx, wy] = xyzToXy(white);
        char buffer[128] = {};
        std::snprintf(buffer, sizeof(buffer),
                      "x = %.4f, y = %.4f  (X %.4f Y %.4f Z %.4f)",
                      wx, wy, white[0], white[1], white[2]);
        info.whitePoint = buffer;
    }
    info.transfer = classifyTransfer(profile);
    if (rTag != nullptr) CFRelease(rTag);
    if (gTag != nullptr) CFRelease(gTag);
    if (bTag != nullptr) CFRelease(bTag);
    if (wTag != nullptr) CFRelease(wTag);
    CFRelease(profile);
    return info;
}


bool convertLinearP3ToDisplayLinear(CGColorSpaceRef source,
                                    CGColorSpaceRef destination,
                                    double red,
                                    double green,
                                    double blue,
                                    std::array<float, 3> &out)
{
    if (source == nullptr || destination == nullptr) {
        return false;
    }
    const CGFloat components[4] = {
        static_cast<CGFloat>(red),
        static_cast<CGFloat>(green),
        static_cast<CGFloat>(blue),
        1.0,
    };
    CGColorRef sourceColor = CGColorCreate(source, components);
    if (sourceColor == nullptr) {
        return false;
    }
    CGColorRef converted = CGColorCreateCopyByMatchingToColorSpace(
        destination, kCGRenderingIntentDefault, sourceColor, nullptr);
    CFRelease(sourceColor);
    if (converted == nullptr) {
        return false;
    }
    const CGFloat *result = CGColorGetComponents(converted);
    if (result == nullptr) {
        CFRelease(converted);
        return false;
    }
    out[0] = static_cast<float>(result[0]);
    out[1] = static_cast<float>(result[1]);
    out[2] = static_cast<float>(result[2]);
    CFRelease(converted);
    return true;
}

std::string displayColorSpaceName(CGDirectDisplayID display)
{
    CGColorSpaceRef space = CGDisplayCopyColorSpace(display);
    if (space == nullptr) {
        return {};
    }
    CFStringRef name = CGColorSpaceCopyName(space);
    std::string result = cfStringToStd(name);
    if (name != nullptr) {
        CFRelease(name);
    }
    CGColorSpaceRelease(space);
    return result;
}
} // namespace

struct MacStreamState
{
    std::mutex mutex;
    CVPixelBufferRef latest = nullptr;
    int width = 0;
    int height = 0;
    std::string error;

    void handleSampleBuffer(CMSampleBufferRef sampleBuffer)
    {
        if (sampleBuffer == nullptr) {
            return;
        }
        CVImageBufferRef image = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (image == nullptr) {
            return;
        }

        CVPixelBufferRef pixel = (CVPixelBufferRef)CFRetain(image);
        if (pixel == nullptr) {
            return;
        }
        const int newWidth = static_cast<int>(CVPixelBufferGetWidth(pixel));
        const int newHeight = static_cast<int>(CVPixelBufferGetHeight(pixel));

        std::lock_guard<std::mutex> lock(mutex);
        if (latest != nullptr) {
            CVPixelBufferRelease(latest);
        }
        latest = pixel;
        width = newWidth;
        height = newHeight;
    }
};

// ScreenCaptureKit 的 output 回调对象；只持有 state 的裸指针，
// 生命周期由 MacCaptureSession::Impl 的 shared_ptr 保证。
} // namespace cp::mac

@interface CPStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithState:(std::shared_ptr<cp::mac::MacStreamState>)state;
@end

@implementation CPStreamOutput
{
    std::shared_ptr<cp::mac::MacStreamState> _state;
}

- (instancetype)initWithState:(std::shared_ptr<cp::mac::MacStreamState>)state
{
    self = [super init];
    if (self) {
        _state = std::move(state);
    }
    return self;
}

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type
{
    (void)stream;
    if (type != SCStreamOutputTypeScreen || !_state) {
        return;
    }
    _state->handleSampleBuffer(sampleBuffer);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    (void)stream;
    if (error != nil) {
        std::fprintf(stderr, "[mac] ScreenCaptureKit stopped: %s\n",
                     [[error localizedDescription] UTF8String]);
    }
}
@end

namespace cp::mac
{
struct MacCaptureSession::Impl
{
    std::shared_ptr<MacStreamState> state = std::make_shared<MacStreamState>();
    SCStream *stream = nil;
    CPStreamOutput *output = nil;
    dispatch_queue_t queue = nullptr;
    CGDirectDisplayID displayID = 0;
    bool hdr = false;
    bool customFormat = false;
    bool edrLinear = false;
    bool bt2020Output = false;
    CGColorSpaceRef displayLinearSpace = nullptr;
    CGColorSpaceRef p3LinearSpace = nullptr;

    ~Impl()
    {
        if (displayLinearSpace != nullptr) {
            CGColorSpaceRelease(displayLinearSpace);
        }
        if (p3LinearSpace != nullptr) {
            CGColorSpaceRelease(p3LinearSpace);
        }
    }

    void start(SCShareableContent *content)
    {
        if (content == nil) {
            state->error = "SCShareableContent 为空";
            return;
        }

        SCDisplay *target = nil;
        for (SCDisplay *display in content.displays) {
            if (display.displayID == displayID) {
                target = display;
                break;
            }
        }
        if (target == nil) {
            state->error = "找不到目标 SCDisplay";
            return;
        }

        // 排除本程序，避免右侧取色器面板进入捕获画面。
        NSMutableArray<SCRunningApplication *> *excluded = [NSMutableArray array];
        const pid_t ownPid = getpid();
        for (SCRunningApplication *application in content.applications) {
            if (application.processID == ownPid) {
                [excluded addObject:application];
            }
        }

        SCContentFilter *filter =
            [[SCContentFilter alloc] initWithDisplay:target
                               excludingApplications:excluded
                                    exceptingWindows:@[]];

        SCStreamConfiguration *configuration = nil;
        if (hdr) {
#if defined(__arm64__)
            if (@available(macOS 15.0, *)) {
                const bool canonical =
                    std::getenv("CHROMA_HDR_CANONICAL") != nullptr ||
                    edrLinear;
                configuration = [SCStreamConfiguration
                    streamConfigurationWithPreset:
                        (canonical
                             ? SCStreamConfigurationPresetCaptureHDRStreamCanonicalDisplay
                             : SCStreamConfigurationPresetCaptureHDRStreamLocalDisplay)];
                if (edrLinear ||
                    std::getenv("CHROMA_HDR_BT2020") != nullptr) {
                    configuration.colorSpaceName = kCGColorSpaceITUR_2020_PQ;
                    configuration.colorMatrix =
                        (__bridge CFStringRef)kCVImageBufferYCbCrMatrix_ITU_R_2020;
                    bt2020Output = true;
                }
                if (customFormat) {
                    // 自定义格式：RGhA + ExtendedLinearDisplayP3。
                    // 注意：这条路径在部分 macOS 上会把 EDR 高光压回 1.0，
                    // 保留为可对比的配置项。
                    configuration.pixelFormat = kCVPixelFormatType_64RGBAHalf;
                    configuration.colorSpaceName = kCGColorSpaceExtendedLinearDisplayP3;
                }
                // Apple 模式保留 preset 的 xf44 + Display P3 PQ。
            }
#endif
        }
        if (configuration == nil) {
            configuration = [[SCStreamConfiguration alloc] init];
            configuration.pixelFormat = kCVPixelFormatType_32BGRA;
        }
        {
            char fourcc[5] = {};
            const uint32_t format = static_cast<uint32_t>(configuration.pixelFormat);
            fourcc[0] = static_cast<char>((format >> 24) & 0xff);
            fourcc[1] = static_cast<char>((format >> 16) & 0xff);
            fourcc[2] = static_cast<char>((format >> 8) & 0xff);
            fourcc[3] = static_cast<char>(format & 0xff);
            std::fprintf(stderr,
                         "[mac] SCK config hdr=%d pixelFormat='%s' colorSpace=%s "
                         "colorMatrix=%s dynamicRange=%ld\n",
                         hdr ? 1 : 0, fourcc,
                         configuration.colorSpaceName != nil
                             ? [(__bridge NSString *)configuration.colorSpaceName UTF8String]
                             : "(nil)",
                         configuration.colorMatrix != nil
                             ? [(__bridge NSString *)configuration.colorMatrix UTF8String]
                             : "(nil)",
                         static_cast<long>(configuration.captureDynamicRange));
        }
        configuration.width = static_cast<size_t>(CGDisplayPixelsWide(displayID));
        configuration.height = static_cast<size_t>(CGDisplayPixelsHigh(displayID));
        configuration.showsCursor = NO;
        configuration.queueDepth = 3;
        configuration.minimumFrameInterval = CMTimeMake(1, 30);

        dispatch_queue_t sampleQueue =
            dispatch_queue_create("chroma.colorpicker.sck", DISPATCH_QUEUE_SERIAL);

        output = [[CPStreamOutput alloc] initWithState:state];

        stream = [[SCStream alloc] initWithFilter:filter
                                    configuration:configuration
                                         delegate:output];

        NSError *error = nil;
        [stream addStreamOutput:output
                           type:SCStreamOutputTypeScreen
             sampleHandlerQueue:sampleQueue
                          error:&error];
        if (error != nil) {
            state->error = std::string([[error localizedDescription] UTF8String]);
            std::fprintf(stderr, "[mac] addStreamOutput 失败: %s\n", state->error.c_str());
            return;
        }

        queue = sampleQueue;
        [stream startCaptureWithCompletionHandler:^(NSError *startError) {
            if (startError != nil) {
                std::fprintf(stderr, "[mac] startCapture 失败: %s\n",
                             [[startError localizedDescription] UTF8String]);
            } else {
                std::fprintf(stderr, "[mac] ScreenCaptureKit stream started\n");
            }
        }];
    }

    void stop()
    {
        if (stream != nil) {
            [stream stopCaptureWithCompletionHandler:^(NSError *error) {
                (void)error;
            }];
        }
        stream = nil;
        output = nil;
        queue = nullptr;

        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->latest != nullptr) {
            CVPixelBufferRelease(state->latest);
            state->latest = nullptr;
        }
        state->width = 0;
        state->height = 0;
    }
};

MacCaptureSession::MacCaptureSession() = default;

MacCaptureSession::~MacCaptureSession()
{
    release();
}

bool MacCaptureSession::initialize(const cp::MonitorInfo &monitor, cp::NativeWindow owner)
{
    (void)owner;
    release();

    if (monitor.nativeHandle == 0) {
        return false;
    }

    if (!CGPreflightScreenCaptureAccess()) {
        std::fprintf(stderr, "[mac] 需要屏幕录制权限\n");
        CGRequestScreenCaptureAccess();
        return false;
    }

    m_impl = std::make_shared<Impl>();
    m_impl->displayID = static_cast<CGDirectDisplayID>(monitor.nativeHandle);

    m_display.gdiName = monitor.id;
    m_impl->hdr = supportsHdrCapture(m_impl->displayID);
    m_impl->customFormat = m_hdrMode == cp::HdrCaptureMode::CustomFormat;
    m_impl->edrLinear = m_hdrMode == cp::HdrCaptureMode::ApplePresetEdrLinear;
    m_display.hdr = m_impl->hdr;
    m_display.bitsPerColor = m_impl->hdr ? (m_impl->customFormat ? 16 : 10) : 8;
    m_display.configBitsPerColor = m_display.bitsPerColor;
    m_display.wireFormat = m_impl->hdr
        ? (m_impl->customFormat ? "RGhA（16F）" : "xf44（10-bit 4:4:4）")
        : "BGRA8";
    m_display.displayIcc = displayIccName(m_impl->displayID);
    const DisplayProfileInfo profileInfo = analyzeDisplayProfile(m_impl->displayID);
    m_display.displayGamut = profileInfo.gamut;
    m_display.displayTransfer = profileInfo.transfer;
    m_display.displayWhitePoint = profileInfo.whitePoint;
    m_display.displayMatrix = profileInfo.matrix;
    m_display.colorSpace = displayColorSpaceName(m_impl->displayID);
    if (m_hdrMode == cp::HdrCaptureMode::ApplePresetEdrLinear) {
        m_impl->bt2020Output = true;
        CGColorSpaceRef displaySpace = CGDisplayCopyColorSpace(m_impl->displayID);
        if (displaySpace != nullptr) {
            m_impl->displayLinearSpace = CGColorSpaceCreateExtendedLinearized(displaySpace);
            CGColorSpaceRelease(displaySpace);
        }
        m_impl->p3LinearSpace = m_impl->bt2020Output
            ? CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearITUR_2020)
            : CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
    }
    if (m_impl->hdr) {
        if (m_hdrMode == cp::HdrCaptureMode::ApplePresetEdrLinear) {
            m_display.colorEncoding = "EDRLinear";
            m_display.colorSpace = "Display Native EDR Linear";
        }
        else if (m_hdrMode == cp::HdrCaptureMode::RawYuvToLinearP3) {
            m_display.colorEncoding = "LinearP3RawYuv";
            m_display.colorSpace = "Linear P3 (raw YUV)";
        }
        else {
            m_display.colorEncoding = "ExtendedLinearDisplayP3";
            m_display.colorSpace = "Extended Linear Display P3";
        }
        m_display.advancedColorSupported = true;
        m_display.advancedColorEnabled = true;
    }
    else if (m_display.colorSpace.empty()) {
        m_display.colorSpace = "显示器 ICC";
    }
    if (NSScreen *screen = screenForDisplay(m_impl->displayID)) {
        m_display.edrHeadroomCurrent =
            screen.maximumExtendedDynamicRangeColorComponentValue;
        m_display.edrHeadroomPotential =
            screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
    }
    std::fprintf(stderr, "[mac] ScreenCaptureKit mode = %s / %s\n",
                 m_impl->hdr ? "HDR" : "SDR",
                 m_impl->hdr
                     ? (m_impl->customFormat
                            ? "custom RGhA + ExtendedLinearP3"
                            : (m_impl->edrLinear
                                   ? "raw xf44 YCbCr -> BT.709 -> PQ -> linear P3"
                                   : "Apple preset xf44 + P3 PQ -> Extended Linear P3"))
                     : "BGRA8");

    std::shared_ptr<Impl> impl = m_impl;
    const CGDirectDisplayID displayID = m_impl->displayID;
    (void)displayID;

    [SCShareableContent
        getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *error) {
            if (error != nil) {
                impl->state->error = std::string([[error localizedDescription] UTF8String]);
                std::fprintf(stderr, "[mac] ShareableContent 失败: %s\n",
                             impl->state->error.c_str());
                return;
            }
            impl->start(content);
        }];

    return true;
}

void MacCaptureSession::release()
{
    if (m_impl) {
        m_impl->stop();
        m_impl.reset();
    }
}

bool MacCaptureSession::captureFrame()
{
    if (!m_impl) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_impl->state->mutex);
    return m_impl->state->latest != nullptr;
}

void MacCaptureSession::refreshDisplayState()
{
    if (!m_impl) {
        return;
    }
    if (NSScreen *screen = screenForDisplay(m_impl->displayID)) {
        m_display.edrHeadroomCurrent =
            screen.maximumExtendedDynamicRangeColorComponentValue;
        m_display.edrHeadroomPotential =
            screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
    }
    if (!m_impl->hdr) {
        const std::string colorSpace = displayColorSpaceName(m_impl->displayID);
        if (!colorSpace.empty()) {
            m_display.colorSpace = colorSpace;
        }
    }
}

cp::ColorSample MacCaptureSession::sampleRect(int x, int y, int width, int height)
{
    cp::ColorSample result;
    if (!m_impl || width <= 0 || height <= 0) {
        return result;
    }

    std::lock_guard<std::mutex> lock(m_impl->state->mutex);
    CVPixelBufferRef pixel = m_impl->state->latest;
    if (pixel == nullptr) {
        return result;
    }

    const int imageWidth = static_cast<int>(CVPixelBufferGetWidth(pixel));
    const int imageHeight = static_cast<int>(CVPixelBufferGetHeight(pixel));
    if (imageWidth <= 0 || imageHeight <= 0) {
        return result;
    }

    const int startX = std::clamp(x, 0, imageWidth);
    const int startY = std::clamp(y, 0, imageHeight);
    const int sampleWidth = std::min(width, imageWidth - startX);
    const int sampleHeight = std::min(height, imageHeight - startY);
    if (sampleWidth <= 0 || sampleHeight <= 0) {
        return result;
    }

    if (CVPixelBufferLockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return result;
    }

    const OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixel);
    const bool hdrXf44 = m_impl->hdr && pixelFormat == fourCC('x', 'f', '4', '4') &&
                         CVPixelBufferGetPlaneCount(pixel) >= 2;
    const bool hdrRgba = m_impl->hdr && pixelFormat == kCVPixelFormatType_64RGBAHalf;
    const auto *base = static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddress(pixel));
    const std::size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixel);
    result.rgba.resize(static_cast<std::size_t>(sampleWidth) * sampleHeight * 4);

    if (hdrXf44) {
        const auto *yPlane = static_cast<const std::uint16_t *>(
            CVPixelBufferGetBaseAddressOfPlane(pixel, 0));
        const auto *cbcrPlane = static_cast<const std::uint16_t *>(
            CVPixelBufferGetBaseAddressOfPlane(pixel, 1));
        const std::size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pixel, 0) / sizeof(std::uint16_t);
        const std::size_t cbcrStride = CVPixelBufferGetBytesPerRowOfPlane(pixel, 1) / sizeof(std::uint16_t);

        for (int row = 0; row < sampleHeight; ++row) {
            for (int column = 0; column < sampleWidth; ++column) {
                const int sourceX = startX + column;
                const int sourceY = startY + row;
                const double y = code10(yPlane[static_cast<std::size_t>(sourceY) * yStride +
                                                static_cast<std::size_t>(sourceX)]);
                const std::size_t chromaIndex =
                    static_cast<std::size_t>(sourceY) * cbcrStride +
                    static_cast<std::size_t>(sourceX) * 2;
                const double cb = code10(cbcrPlane[chromaIndex + 0]) - 0.5;
                const double cr = code10(cbcrPlane[chromaIndex + 1]) - 0.5;

                // BT.709 或 BT.2020 full-range YCbCr -> RGB（PQ）
                const double rCode = m_impl->bt2020Output
                    ? std::clamp(y + 1.4746 * cr, 0.0, 1.0)
                    : std::clamp(y + 1.5748 * cr, 0.0, 1.0);
                const double gCode = m_impl->bt2020Output
                    ? std::clamp(y - 0.16455 * cb - 0.57135 * cr, 0.0, 1.0)
                    : std::clamp(y - 0.187324 * cb - 0.468124 * cr, 0.0, 1.0);
                const double bCode = m_impl->bt2020Output
                    ? std::clamp(y + 1.8814 * cb, 0.0, 1.0)
                    : std::clamp(y + 1.8556 * cb, 0.0, 1.0);

                double r = 0.0;
                double g = 0.0;
                double b = 0.0;
                // 先统一到“100 nits SDR 白”的 P3 线性，
                // 这样 EDR 值和 Digital Color Meter 的 EDR 线性参考一致。
                constexpr double kSdrWhiteNits = 100.0;
                r = pqCodeToNits(rCode) / kSdrWhiteNits;
                g = pqCodeToNits(gCode) / kSdrWhiteNits;
                b = pqCodeToNits(bCode) / kSdrWhiteNits;

                // raw yuv to linear p3 模式 / EDR linear：
                // 先做 xf44 YCbCr -> BT.709 RGB -> PQ -> linear P3。
                // EDR 模式再转到显示器原生 Extended Linear。
                // 1.0 = 100 nits SDR 白，>1 = EDR 高光。

                if (m_impl->edrLinear) {
                    std::array<float, 3> converted = {};
                    if (convertLinearP3ToDisplayLinear(m_impl->p3LinearSpace,
                                                       m_impl->displayLinearSpace,
                                                       r, g, b, converted)) {
                        r = converted[0];
                        g = converted[1];
                        b = converted[2];
                    }
                }
                const std::size_t destination =
                    (static_cast<std::size_t>(row) * sampleWidth + column) * 4;
                result.rgba[destination + 0] = static_cast<float>(r);
                result.rgba[destination + 1] = static_cast<float>(g);
                result.rgba[destination + 2] = static_cast<float>(b);
                result.rgba[destination + 3] = 1.0f;
            }
        }
    }
    else if (hdrRgba) {
        const std::size_t bytesPerPixel = sizeof(__fp16) * 4;
        for (int row = 0; row < sampleHeight; ++row) {
            const auto *source = reinterpret_cast<const __fp16 *>(
                base + static_cast<std::size_t>(startY + row) * bytesPerRow +
                static_cast<std::size_t>(startX) * bytesPerPixel);
            for (int column = 0; column < sampleWidth; ++column) {
                const std::size_t destination =
                    (static_cast<std::size_t>(row) * sampleWidth + column) * 4;
                const std::size_t sourceIndex = static_cast<std::size_t>(column) * 4;
                result.rgba[destination + 0] = static_cast<float>(source[sourceIndex + 0]);
                result.rgba[destination + 1] = static_cast<float>(source[sourceIndex + 1]);
                result.rgba[destination + 2] = static_cast<float>(source[sourceIndex + 2]);
                result.rgba[destination + 3] = static_cast<float>(source[sourceIndex + 3]);
            }
        }
    }
    else {
        for (int row = 0; row < sampleHeight; ++row) {
            const auto *source = base + static_cast<std::size_t>(startY + row) * bytesPerRow +
                                 static_cast<std::size_t>(startX) * 4;
            for (int column = 0; column < sampleWidth; ++column) {
                const std::size_t destination =
                    (static_cast<std::size_t>(row) * sampleWidth + column) * 4;
                result.rgba[destination + 0] = static_cast<float>(source[column * 4 + 2]) / 255.0f;
                result.rgba[destination + 1] = static_cast<float>(source[column * 4 + 1]) / 255.0f;
                result.rgba[destination + 2] = static_cast<float>(source[column * 4 + 0]) / 255.0f;
                result.rgba[destination + 3] = static_cast<float>(source[column * 4 + 3]) / 255.0f;
            }
        }
    }

    CVPixelBufferUnlockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);

    result.ok = true;
    result.x = startX;
    result.y = startY;
    result.width = sampleWidth;
    result.height = sampleHeight;
    if (hdrXf44) {
        result.format = format();
    }
    else if (hdrRgba) {
        result.format = format();
    }
    else {
        result.format.encoding = cp::PixelEncoding::Bgra8;
        result.format.bitsPerChannel = 8;
        result.format.linear = false;
        result.format.name = "BGRA8";
        result.format.pixelFormat = "BGRA8";
        result.format.colorSpaceName = "display color space";
        result.format.colorMatrix = "n/a";
    }
    return result;
}

cp::CaptureFormat MacCaptureSession::format() const
{
    cp::CaptureFormat result;
    const bool hdr = m_impl && m_impl->hdr;
    const bool custom = m_impl && m_impl->customFormat;
    if (!hdr) {
        result.encoding = cp::PixelEncoding::Bgra8;
        result.bitsPerChannel = 8;
        result.linear = false;
        result.name = "BGRA8";
        result.pixelFormat = "BGRA8";
        result.colorSpaceName = "display color space";
        result.colorMatrix = "n/a";
        return result;
    }
    if (custom) {
        result.encoding = cp::PixelEncoding::Rgba16Float;
        result.bitsPerChannel = 16;
        result.linear = true;
        result.name = "RGhA";
        result.pixelFormat = "RGhA";
        result.colorSpaceName = "kCGColorSpaceExtendedLinearDisplayP3";
        result.colorMatrix = "n/a";
    }
    else {
        result.encoding = cp::PixelEncoding::YCbCr10;
        result.bitsPerChannel = 10;
        result.linear = true;
        result.name = "xf44";
        result.pixelFormat = "xf44";
        if (m_impl->bt2020Output) {
            result.colorSpaceName = "kCGColorSpaceITUR_2100_PQ";
            result.colorMatrix = "ITU_R_2020";
        }
        else {
            result.colorSpaceName = "kCGColorSpaceDisplayP3_PQ";
            result.colorMatrix = "ITU_R_709_2";
        }
    }
    return result;
}

cp::CaptureBackendKind MacCaptureSession::backend() const
{
    return cp::CaptureBackendKind::ScreenCaptureKit;
}

const char *MacCaptureSession::backendName() const
{
    return "ScreenCaptureKit";
}

int MacCaptureSession::width() const
{
    if (!m_impl) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_impl->state->mutex);
    return m_impl->state->width;
}

int MacCaptureSession::height() const
{
    if (!m_impl) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_impl->state->mutex);
    return m_impl->state->height;
}
} // namespace cp::mac
