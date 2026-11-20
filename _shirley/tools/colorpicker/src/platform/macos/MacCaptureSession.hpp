#pragma once

#include "core/CaptureSession.hpp"

namespace cp::mac
{
/// macOS 捕获骨架。
/// TODO: 用 ScreenCaptureKit (SCStream / SCScreenshotManager) 实现：
///   - SDR: 32BGRA + CGColorSpace；
///   - HDR (macOS 15+): captureHDRStreamLocalDisplay / CanonicalDisplay，
///     10-bit YCbCr + Display P3 PQ/HLG，并做 YCbCr→RGB / PQ→nits 转换。
class MacCaptureSession final : public cp::ICaptureSession
{
public:
    bool initialize(const cp::MonitorInfo &monitor, cp::NativeWindow owner) override;
    void release() override;

    bool captureFrame() override;
    void refreshDisplayState() override;
    cp::ColorSample sampleRect(int x, int y, int width, int height) override;

    const cp::DisplayInfo &display() const override { return m_display; }
    cp::CaptureFormat format() const override;
    cp::CaptureBackendKind backend() const override;
    const char *backendName() const override;
    int width() const override;
    int height() const override;

private:
    cp::DisplayInfo m_display;
};
} // namespace cp::mac
