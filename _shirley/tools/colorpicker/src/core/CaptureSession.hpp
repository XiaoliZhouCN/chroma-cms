#pragma once

#include "core/Types.hpp"

namespace cp
{
enum class HdrCaptureMode
{
    ApplePreset,          // Apple HDR preset，PQ → Extended Linear P3
    ApplePresetEdrLinear, // Apple HDR preset，EDR Linear（继续开发中）
    RawYuvToLinearP3,     // raw xf44 YCbCr → BT.709 → PQ → linear P3（诊断模式）
    CustomFormat,         // 自定义 RGhA / Extended Linear P3
};

/// 捕获后端：只在当前显示器内取色，坐标是"显示器本地物理像素"。
class ICaptureSession
{
public:
    virtual ~ICaptureSession() = default;

    virtual bool initialize(const MonitorInfo &monitor, NativeWindow owner) = 0;
    virtual void release() = 0;

    virtual bool captureFrame() = 0;
    virtual void refreshDisplayState() = 0;
    virtual ColorSample sampleRect(int x, int y, int width, int height) = 0;

    virtual const DisplayInfo &display() const = 0;
    virtual bool hdr() const { return display().hdr; }
    virtual CaptureFormat format() const = 0;
    virtual CaptureBackendKind backend() const = 0;
    virtual const char *backendName() const = 0;

    // HDR 配置模式；默认不支持，Windows 后端不实现。
    virtual bool supportsHdrModes() const { return false; }
    virtual void setHdrMode(HdrCaptureMode) {}
    virtual HdrCaptureMode hdrMode() const { return HdrCaptureMode::ApplePreset; }
    virtual int width() const = 0;
    virtual int height() const = 0;
};
} // namespace cp
