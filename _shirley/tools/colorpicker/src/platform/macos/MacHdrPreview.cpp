#include "ui/HdrPreview.hpp"

#include <QColor>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>

namespace cp::mac
{
namespace
{
class MacPlaceholderPreview final : public QWidget
{
public:
    explicit MacPlaceholderPreview(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        m_color = QColor(16, 16, 20);
    }

    void setColor(float r, float g, float b, bool linear)
    {
        const auto encode = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            return value <= 0.0031308f ? 12.92f * value
                                       : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        };
        const float rr = linear ? encode(r) : std::clamp(r, 0.0f, 1.0f);
        const float gg = linear ? encode(g) : std::clamp(g, 0.0f, 1.0f);
        const float bb = linear ? encode(b) : std::clamp(b, 0.0f, 1.0f);
        m_color = QColor::fromRgbF(rr, gg, bb);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_color);
    }

private:
    QColor m_color;
};

class MacHdrPreview final : public cp::HdrPreview
{
public:
    explicit MacHdrPreview(QWidget *parent)
        : m_widget(std::make_unique<MacPlaceholderPreview>(parent))
    {
    }

    QWidget *widget() override { return m_widget.get(); }
    void setColor(float r, float g, float b, bool linear) override
    {
        m_widget->setColor(r, g, b, linear);
    }
    void refreshSurface() override { m_widget->update(); }

private:
    std::unique_ptr<MacPlaceholderPreview> m_widget;
};
} // namespace
} // namespace cp::mac

namespace cp
{
std::unique_ptr<HdrPreview> createHdrPreview(QWidget *parent)
{
    // TODO: 用 CAMetalLayer + wantsExtendedDynamicRangeContent 实现 macOS EDR 预览。
    return std::make_unique<cp::mac::MacHdrPreview>(parent);
}
} // namespace cp
