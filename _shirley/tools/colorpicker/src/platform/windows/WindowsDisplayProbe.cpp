#include "WindowsDisplayProbe.hpp"

#include "WindowsConversions.hpp"

#include "Capture.hpp"

#include <windows.h>

#include <algorithm>

namespace cp::win
{
namespace
{
cp::MonitorInfo toCoreMonitor(const cp::win::MonitorInfo &source)
{
    cp::MonitorInfo monitor;
    monitor.id = toUtf8(source.gdiName);
    monitor.name = monitor.id;
    monitor.bounds.left = source.rect.left;
    monitor.bounds.top = source.rect.top;
    monitor.bounds.right = source.rect.right;
    monitor.bounds.bottom = source.rect.bottom;
    monitor.primary = source.primary;
    monitor.nativeHandle = reinterpret_cast<std::uintptr_t>(source.handle);
    return monitor;
}
} // namespace

std::vector<cp::MonitorInfo> WindowsDisplayProbe::enumerate()
{
    std::vector<cp::MonitorInfo> result;
    for (const cp::win::MonitorInfo &monitor : cp::win::enumerateMonitors()) {
        result.push_back(toCoreMonitor(monitor));
    }
    return result;
}

cp::MonitorInfo WindowsDisplayProbe::monitorAt(cp::PointI globalPoint)
{
    POINT point{globalPoint.x, globalPoint.y};
    const cp::win::MonitorInfo source = cp::win::monitorFromPoint(point);
    if (source.handle != nullptr) {
        return toCoreMonitor(source);
    }

    // 极端情况下 MonitorFromPoint 失败：退回枚举列表里离该点最近的显示器。
    const std::vector<cp::MonitorInfo> monitors = enumerate();
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

cp::PointI WindowsDisplayProbe::cursorPosition()
{
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return {};
    }
    return cp::PointI{cursor.x, cursor.y};
}
} // namespace cp::win
