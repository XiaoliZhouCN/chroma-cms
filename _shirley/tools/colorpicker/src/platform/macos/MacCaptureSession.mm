#include "MacCaptureSession.hpp"

#include <cstdio>

namespace cp::mac
{
bool MacCaptureSession::initialize(const cp::MonitorInfo &monitor, cp::NativeWindow owner)
{
    (void)monitor;
    (void)owner;
    std::fprintf(stderr,
                 "[mac] MacCaptureSession 是重构骨架：ScreenCaptureKit 后端尚未实现\n");
    return false;
}

void MacCaptureSession::release() {}

bool MacCaptureSession::captureFrame()
{
    return false;
}

void MacCaptureSession::refreshDisplayState() {}

cp::ColorSample MacCaptureSession::sampleRect(int, int, int, int)
{
    return {};
}

cp::CaptureFormat MacCaptureSession::format() const
{
    return {};
}

cp::CaptureBackendKind MacCaptureSession::backend() const
{
    return cp::CaptureBackendKind::ScreenCaptureKit;
}

const char *MacCaptureSession::backendName() const
{
    return "ScreenCaptureKit (TODO)";
}

int MacCaptureSession::width() const
{
    return 0;
}

int MacCaptureSession::height() const
{
    return 0;
}
} // namespace cp::mac
