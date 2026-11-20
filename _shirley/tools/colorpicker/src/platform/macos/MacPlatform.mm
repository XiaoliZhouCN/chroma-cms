#include "core/Platform.hpp"

#include "MacCaptureSession.hpp"
#include "MacDisplayProbe.hpp"

#include <memory>

namespace cp
{
void prepareProcessForDump()
{
    // macOS 屏幕坐标由 CoreGraphics 处理；--dump 的物理像素换算在
    // MacDisplayProbe 中完成（TODO: Retina 校验）。
}

std::unique_ptr<IDisplayProbe> createDisplayProbe()
{
    return std::make_unique<cp::mac::MacDisplayProbe>();
}

std::unique_ptr<ICaptureSession> createCaptureSession()
{
    return std::make_unique<cp::mac::MacCaptureSession>();
}
} // namespace cp
