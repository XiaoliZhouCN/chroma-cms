#include "MacDisplayProbe.hpp"

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <string>

namespace cp::mac
{
namespace
{
cp::MonitorInfo toCoreDisplay(CGDirectDisplayID display)
{
    cp::MonitorInfo monitor;
    const CGRect bounds = CGDisplayBounds(display);
    monitor.id = "display:" + std::to_string(display);
    monitor.name = monitor.id;
    monitor.bounds.left = static_cast<int>(bounds.origin.x);
    monitor.bounds.top = static_cast<int>(bounds.origin.y);
    monitor.bounds.right = static_cast<int>(bounds.origin.x + bounds.size.width);
    monitor.bounds.bottom = static_cast<int>(bounds.origin.y + bounds.size.height);
    monitor.primary = CGDisplayIsMain(display) != 0;
    monitor.nativeHandle = static_cast<std::uintptr_t>(display);
    return monitor;
}
} // namespace

std::vector<cp::MonitorInfo> MacDisplayProbe::enumerate()
{
    std::uint32_t count = 0;
    if (CGGetOnlineDisplayList(0, nullptr, &count) != kCGErrorSuccess || count == 0) {
        return {};
    }
    std::vector<CGDirectDisplayID> displays(count);
    if (CGGetOnlineDisplayList(count, displays.data(), &count) != kCGErrorSuccess) {
        return {};
    }
    std::vector<cp::MonitorInfo> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        result.push_back(toCoreDisplay(displays[index]));
    }
    return result;
}

cp::MonitorInfo MacDisplayProbe::monitorAt(cp::PointI globalPoint)
{
    const std::vector<cp::MonitorInfo> monitors = enumerate();
    for (const cp::MonitorInfo &monitor : monitors) {
        if (globalPoint.x >= monitor.bounds.left && globalPoint.x < monitor.bounds.right &&
            globalPoint.y >= monitor.bounds.top && globalPoint.y < monitor.bounds.bottom) {
            return monitor;
        }
    }
    if (monitors.empty()) {
        return {};
    }
    const auto distance = [&](const cp::MonitorInfo &monitor) {
        const int centerX = (monitor.bounds.left + monitor.bounds.right) / 2;
        const int centerY = (monitor.bounds.top + monitor.bounds.bottom) / 2;
        const int dx = centerX - globalPoint.x;
        const int dy = centerY - globalPoint.y;
        return dx * dx + dy * dy;
    };
    return *std::min_element(monitors.begin(), monitors.end(),
                             [&](const cp::MonitorInfo &a, const cp::MonitorInfo &b) {
                                 return distance(a) < distance(b);
                             });
}

cp::PointI MacDisplayProbe::cursorPosition()
{
    // TODO: CGEventGetLocation 的 Quartz 坐标与多显示器/Retina 物理像素的换算
    // 需要在 macOS 实机上校验。
    CGEventRef event = CGEventCreate(nullptr);
    if (event == nullptr) {
        return {};
    }
    const CGPoint location = CGEventGetLocation(event);
    CFRelease(event);
    return cp::PointI{static_cast<int>(location.x), static_cast<int>(location.y)};
}
} // namespace cp::mac
