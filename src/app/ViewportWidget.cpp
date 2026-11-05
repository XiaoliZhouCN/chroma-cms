#include "app/ViewportWidget.hpp"

#include "render/D3D12Renderer.hpp"

#include <QColor>
#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>

#include <cmath>
#include <exception>

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

ViewportWidget::~ViewportWidget() = default;

std::uint32_t ViewportWidget::physicalWidth() const
{
    return static_cast<std::uint32_t>(std::lround(width() * devicePixelRatioF()));
}

std::uint32_t ViewportWidget::physicalHeight() const
{
    return static_cast<std::uint32_t>(std::lround(height() * devicePixelRatioF()));
}

void ViewportWidget::ensureRenderer()
{
    if (m_renderer || m_initAttempted) {
        return;
    }
    m_initAttempted = true;

    try {
        auto renderer = std::make_unique<render::D3D12Renderer>();

        // QWidget::winId() 返回的是整数形式的 HWND
        // Q: 为什么要 先创建renderer 再 move 到 m_renderer？
        // A: 先创建 renderer 并进行初始化，可以确保在初始化过程中捕获任何异常。如果直接在 m_renderer 中创建并初始化，一旦初始化失败，m_renderer 可能会处于不稳定状态。通过先创建一个局部的 renderer 对象，可以在初始化成功后再将其移动到 m_renderer，从而保证 m_renderer 始终处于有效状态。
        renderer->initialize(reinterpret_cast<void*>(winId()), physicalWidth(), physicalHeight());

        const char* backend = renderer->backendName();
        m_renderer = std::move(renderer);

        qInfo().noquote() << "[gfx] backend =" << backend
                          << " swapchain =" << physicalWidth() << "x" << physicalHeight()
                          << " dpr =" << devicePixelRatioF();
        emit rendererReady(QString::fromUtf8(backend));
    }
    catch (const std::exception& error) {
        qCritical().noquote() << "[gfx] 初始化失败:" << error.what();
        emit rendererFailed(QString::fromUtf8(error.what()));
    }
}

void ViewportWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureRenderer();
    update();
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (m_renderer) {
        try {
            m_renderer->resize(physicalWidth(), physicalHeight());
        }
        catch (const std::exception& error) {
            qCritical().noquote() << "[gfx] resize 失败:" << error.what();
        }
    }
    update();
}

void ViewportWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_renderer) {
        try {
            m_renderer->present();
            return;
        }
        catch (const std::exception& error) {
            qCritical().noquote() << "[gfx] present 失败:" << error.what();
        }
    }

    // 渲染器没起来时的占位，避免全黑看不出状态
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 28));
    painter.setPen(QColor(230, 120, 120));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("D3D12 未就绪\n看控制台日志"));
}

} // namespace chroma::app