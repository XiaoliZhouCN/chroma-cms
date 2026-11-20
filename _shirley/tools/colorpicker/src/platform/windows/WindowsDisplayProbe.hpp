#pragma once

#include "core/DisplayProbe.hpp"

namespace cp::win
{
class WindowsDisplayProbe final : public cp::IDisplayProbe
{
public:
    std::vector<cp::MonitorInfo> enumerate() override;
    cp::MonitorInfo monitorAt(cp::PointI globalPoint) override;
    cp::PointI cursorPosition() override;
};
} // namespace cp::win
