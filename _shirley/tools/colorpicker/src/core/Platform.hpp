#pragma once

#include "core/CaptureSession.hpp"
#include "core/DisplayProbe.hpp"

#include <memory>

namespace cp
{
/// --dump 模式需要物理像素坐标；GUI 模式下由 Qt 管理 DPI。
void prepareProcessForDump();

std::unique_ptr<IDisplayProbe> createDisplayProbe();
std::unique_ptr<ICaptureSession> createCaptureSession();
} // namespace cp
