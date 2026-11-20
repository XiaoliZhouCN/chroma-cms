#pragma once

#include "core/DisplayProbe.hpp"

namespace cp::mac
{
/// macOS 显示器探测骨架。
/// TODO: 用 NSScreen/CGDisplay 补全 Retina、EDR headroom、显示器名称。
class MacDisplayProbe final : public cp::IDisplayProbe
{
public:
    std::vector<cp::MonitorInfo> enumerate() override;
    cp::MonitorInfo monitorAt(cp::PointI globalPoint) override;
    cp::PointI cursorPosition() override;
};
} // namespace cp::mac
