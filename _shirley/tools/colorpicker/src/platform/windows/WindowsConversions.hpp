#pragma once

#include "core/Types.hpp"

#include "Capture.hpp"

#include <string>

namespace cp::win
{
std::string toUtf8(const std::wstring &value);
cp::CaptureFormat captureFormatFromDxgi(DXGI_FORMAT format);
} // namespace cp::win
