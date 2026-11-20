#include "core/Platform.hpp"

#include "WindowsCaptureSession.hpp"
#include "WindowsDisplayProbe.hpp"

#include <windows.h>

#include <memory>

namespace cp
{
void prepareProcessForDump()
{
    SetProcessDPIAware();
}

std::unique_ptr<IDisplayProbe> createDisplayProbe()
{
    return std::make_unique<cp::win::WindowsDisplayProbe>();
}

std::unique_ptr<ICaptureSession> createCaptureSession()
{
    return std::make_unique<cp::win::WindowsCaptureSession>();
}
} // namespace cp
