#pragma once

#include "core/Types.hpp"

#include <memory>
#include <vector>

namespace cp
{
/// 显示器枚举 / 光标位置查询。UI 和 CLI 都只依赖此接口。
class IDisplayProbe
{
public:
    virtual ~IDisplayProbe() = default;

    virtual std::vector<MonitorInfo> enumerate() = 0;
    virtual MonitorInfo monitorAt(PointI globalPoint) = 0;
    virtual PointI cursorPosition() = 0;
};
} // namespace cp
