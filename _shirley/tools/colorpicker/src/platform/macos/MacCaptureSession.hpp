#pragma once

#include "core/CaptureSession.hpp"

#include <memory>

namespace cp::mac
{
/// macOS ScreenCaptureKit 捕获。
///
/// 当前实现：SDR 32BGRA 连续流；HDR/EDR 后续接入。
/// ScreenCaptureKit 的初始化/startCapture 是异步的，initialize() 返回
/// “已发起初始化”，UI 在最新帧到达前显示占位值。
class MacCaptureSession final : public cp::ICaptureSession
{
public:
    MacCaptureSession();
    ~MacCaptureSession() override;

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

    bool supportsHdrModes() const override { return true; }
    void setHdrMode(cp::HdrCaptureMode mode) override { m_hdrMode = mode; }
    cp::HdrCaptureMode hdrMode() const override { return m_hdrMode; }

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
    cp::DisplayInfo m_display;
    cp::HdrCaptureMode m_hdrMode = cp::HdrCaptureMode::ApplePreset;
};
} // namespace cp::mac
