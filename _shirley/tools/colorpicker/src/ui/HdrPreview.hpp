#pragma once

#include <QWidget>

#include <memory>

namespace cp
{
/// HDR 色块预览的平台抽象。
///
/// 是否真正按 HDR 显示由平台实现决定：
///   - Windows：D3D11 scRGB FP16 swapchain；
///   - macOS：Metal EDR（后续实现）；
///   - 占位平台：Qt SDR 近似。
class HdrPreview
{
public:
    virtual ~HdrPreview() = default;

    virtual QWidget *widget() = 0;
    virtual void setColor(float r, float g, float b, bool linear) = 0;
    virtual void refreshSurface() = 0;
};

std::unique_ptr<HdrPreview> createHdrPreview(QWidget *parent);
} // namespace cp
