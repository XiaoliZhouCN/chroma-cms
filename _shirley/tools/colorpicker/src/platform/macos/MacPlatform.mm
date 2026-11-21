#include "core/Platform.hpp"

#include "MacCaptureSession.hpp"
#include "MacDisplayProbe.hpp"

#import <Foundation/Foundation.h>

#include <memory>

namespace cp
{
namespace
{
void preventAppNap()
{
    static id<NSObject> activity = nil;
    if (activity == nil) {
        activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiated |
                                      NSActivityLatencyCritical)
                              reason:@"ChromaColorPicker continuous screen sampling"];
    }
}
} // namespace

void prepareProcessForDump()
{
    // macOS 屏幕坐标由 CoreGraphics 处理；--dump 的物理像素换算在
    // MacDisplayProbe 中完成（TODO: Retina 校验）。
}

std::unique_ptr<IDisplayProbe> createDisplayProbe()
{
    preventAppNap();
    return std::make_unique<cp::mac::MacDisplayProbe>();
}

std::unique_ptr<ICaptureSession> createCaptureSession()
{
    return std::make_unique<cp::mac::MacCaptureSession>();
}
} // namespace cp
