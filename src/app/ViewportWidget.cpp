#include "app/ViewportWidget.hpp"

#include "app/AssistInfoWidget.hpp"
#include "app/CSMViewerWidget.hpp"

#include <QResizeEvent>

namespace chroma::app {

ViewportWidget::ViewportWidget(QWidget* parent)
    : ViewportWidget(render::BackendKind::Skia, nullptr, parent)
{
}

ViewportWidget::ViewportWidget(render::BackendKind defaultBackend,
                               std::shared_ptr<const media::Image> image,
                               QWidget* parent)
    : QWidget(parent)
    , m_backend(defaultBackend)
{
    setObjectName(QStringLiteral("viewport"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#viewport { background-color: #000000; }"));

    m_nativeViewer = new CSMViewerWidget(render::BackendKind::Native, image, this);
    m_skiaViewer = new CSMViewerWidget(render::BackendKind::Skia, image, this);
    m_nativeViewer->setGeometry(rect());
    m_skiaViewer->setGeometry(rect());
    m_nativeViewer->show();
    m_skiaViewer->show();

    m_info = new AssistInfoWidget(this);

    connect(m_nativeViewer, &CSMViewerWidget::rendererReady, this, &ViewportWidget::rendererReady);
    connect(m_skiaViewer, &CSMViewerWidget::rendererReady, this, &ViewportWidget::rendererReady);
    connect(m_nativeViewer, &CSMViewerWidget::rendererFailed, this, &ViewportWidget::rendererFailed);
    connect(m_skiaViewer, &CSMViewerWidget::rendererFailed, this, &ViewportWidget::rendererFailed);

    raiseCurrent();
}

void ViewportWidget::setImage(std::shared_ptr<const media::Image> image)
{
    m_nativeViewer->setImage(image);
    m_skiaViewer->setImage(std::move(image));
}

void ViewportWidget::raiseCurrent()
{
    CSMViewerWidget* selected =
        (m_backend == render::BackendKind::Native) ? m_nativeViewer : m_skiaViewer;
    if (selected != nullptr) {
        selected->raise();
    }
    if (m_info != nullptr) {
        m_info->raise();
    }
}

void ViewportWidget::showBackend(render::BackendKind kind)
{
    m_backend = kind;
    raiseCurrent();

    // 同步触发一次渲染；清屏由 renderer 的每个 render pass 自己完成。
    if (auto* selected = viewer()) {
        selected->repaint();
    }
}

void ViewportWidget::setApiTransform(render::BackendKind kind, int quarterTurns, float zoom)
{
    CSMViewerWidget* target =
        (kind == render::BackendKind::Native) ? m_nativeViewer : m_skiaViewer;
    if (target != nullptr) {
        target->setViewTransform(quarterTurns, zoom);
    }
}

CSMViewerWidget* ViewportWidget::viewer() const
{
    return (m_backend == render::BackendKind::Native) ? m_nativeViewer : m_skiaViewer;
}

void ViewportWidget::setInfoText(const QString& text)
{
    m_info->setInfo(text);
    m_info->move(12, 12);
    m_info->raise();
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_nativeViewer) m_nativeViewer->setGeometry(rect());
    if (m_skiaViewer) m_skiaViewer->setGeometry(rect());
    if (m_info) {
        m_info->move(12, 12);
        m_info->raise();
    }
}

} // namespace chroma::app
