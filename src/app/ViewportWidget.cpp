#include "app/ViewportWidget.h"

#include <QColor>
#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>

namespace chroma::app {

ViewportWidget::ViewportWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
}

WId ViewportWidget::nativeHandle()
{
    return winId();
}

void ViewportWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (!m_handleLogged) {
        m_handleLogged = true;
        qInfo().noquote() << "[app] viewport HWND ="
                          << QStringLiteral("0x%1").arg(static_cast<qulonglong>(winId()), 0, 16)
                          << " dpr =" << devicePixelRatioF();
    }
}

void ViewportWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // M0 占位绘制。M1 起此函数不再绘制像素，改为触发 IRenderer::present()。
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 28));
    painter.setPen(QColor(120, 200, 255));
    painter.drawText(rect(), Qt::AlignCenter,
                     QStringLiteral("M0 · 原生表面宿主\nM1 起由 D3D12 接管"));
}

} // namespace chroma::app
