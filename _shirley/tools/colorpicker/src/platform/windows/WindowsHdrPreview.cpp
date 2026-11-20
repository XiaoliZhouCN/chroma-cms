#include "WindowsHdrPreview.hpp"

namespace cp::win
{
WindowsHdrPreview::WindowsHdrPreview(QWidget *parent)
    : m_widget(std::make_unique<cp::win::HdrSwatch>(parent))
{
}

void WindowsHdrPreview::setColor(float r, float g, float b, bool linear)
{
    m_widget->setColor(r, g, b, linear);
}

void WindowsHdrPreview::refreshSurface()
{
    m_widget->refreshSurface();
}
} // namespace cp::win

namespace cp
{
std::unique_ptr<HdrPreview> createHdrPreview(QWidget *parent)
{
    return std::make_unique<cp::win::WindowsHdrPreview>(parent);
}
} // namespace cp
