#include "app/ViewportWidget.h"

#include "media/ImageData.h"
#include "render/D3D12Renderer.h"

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

void ViewportWidget::setImage(std::shared_ptr<const media::ImageData> image)
{
    m_image = std::move(image);

    if (m_renderer && m_image) {
        try {
            m_renderer->setImage(*m_image);
        }
        catch (const std::exception& error) {
            qCritical().noquote() << "[gfx] 上传纹理失败:" << error.what();
            emit rendererFailed(QString::fromUtf8(error.what()));
        }
    }
    update();
}

void ViewportWidget::ensureRenderer()
{
    if (m_renderer || m_initAttempted) {
        return;
    }
    m_initAttempted = true;

    try {
        auto renderer = std::make_unique<render::D3D12Renderer>();
        renderer->initialize(reinterpret_cast<void*>(winId()), physicalWidth(), physicalHeight());

        if (m_image) {
            renderer->setImage(*m_image);
        }

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

    if (m_renderer != nullptr) {
        try {
            m_renderer->resize(physicalWidth(), physicalHeight());
        }
        catch (const std::exception& error) {
            qCritical().noquote() << "[gfx] ResizeBuffers 失败:" << error.what();
        }
    }
    update();
}

void ViewportWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_renderer != nullptr && m_image != nullptr) {
        try {
            m_renderer->present();
            return;
        }
        catch (const std::exception& error) {
            qCritical().noquote() << "[gfx] present 失败:" << error.what();
        }
    }

    // 渲染器未就绪或出错时的占位绘制
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 28));
    painter.setPen(QColor(120, 200, 255));
    painter.drawText(rect(), Qt::AlignCenter,
                     QStringLiteral("等待 D3D12 初始化…\n若无图像显示，检查控制台日志"));
}

} // namespace chroma::app
