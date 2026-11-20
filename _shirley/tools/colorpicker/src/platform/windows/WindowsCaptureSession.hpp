#pragma once

#include "core/CaptureSession.hpp"

#include "Capture.hpp"

namespace cp::win
{
class WindowsCaptureSession final : public cp::ICaptureSession
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
    void syncDisplay();

    cp::win::ScreenCapture m_capture;
    cp::DisplayInfo m_display;
};
} // namespace cp::win
