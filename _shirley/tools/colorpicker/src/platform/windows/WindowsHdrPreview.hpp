#pragma once

#include "ui/HdrPreview.hpp"

#include "HdrSwatch.hpp"

#include <memory>

namespace cp::win
{
class WindowsHdrPreview final : public cp::HdrPreview
{
public:
    explicit WindowsHdrPreview(QWidget *parent);

    QWidget *widget() override { return m_widget.get(); }
    void setColor(float r, float g, float b, bool linear) override;
    void refreshSurface() override;

private:
    std::unique_ptr<cp::win::HdrSwatch> m_widget;
};
} // namespace cp::win
