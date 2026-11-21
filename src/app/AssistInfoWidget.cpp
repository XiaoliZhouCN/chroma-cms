#include "app/AssistInfoWidget.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

namespace chroma::app {

AssistInfoWidget::AssistInfoWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFont(QFont(QStringLiteral("Menlo"), 12));
    setInfo(QStringLiteral("ChromaCMS"));
}

void AssistInfoWidget::setInfo(const QString& text)
{
    m_info = text;
    const QFontMetrics metrics(font());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, 420, 1000),
                                               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                                               m_info);
    // 宽度固定，避免 Metal -> Skia 时 widget 变窄，旧的长文本尾部残留在
    // 原区域；固定尺寸后每次绘制都能完整覆盖并清掉旧内容。
    setFixedSize(460, std::max(132, bounds.height() + 20));
    update();
}

void AssistInfoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // WA_NoSystemBackground 下 Qt 不会自动擦除上一帧；
    // 先把整个 info 区域清成透明，再画半透明黑底和新文本。
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    painter.setPen(QPen(QColor(120, 120, 135, 190), 1));
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    painter.setPen(QColor(242, 242, 247));
    painter.drawText(rect().adjusted(12, 10, -12, -10),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_info);
}

} // namespace chroma::app
