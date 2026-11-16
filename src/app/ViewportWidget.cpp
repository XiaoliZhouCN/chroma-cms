#include "app/ViewportWidget.hpp"

#include "render/RendererFactory.hpp"
#include "render/IRenderer.hpp"
#include "render/IViewTransform.hpp"

#include "media/ImageLoader.hpp"

#include <utility>

#include <QColor>
#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QKeyEvent>

#include <cmath>
#include <exception>
#include <stdexcept>
#include <algorithm>

namespace chroma::app {
namespace {
constexpr float kZoomStep = 1.1f;
constexpr float kMinZoom = 0.1f;
constexpr float kMaxZoom = 8.0f;
} // namespace

ViewportWidget::ViewportWidget(QWidget* parent)
    : ViewportWidget(render::BackendKind::Native, nullptr, parent)
{
}

ViewportWidget::ViewportWidget(std::shared_ptr<const media::Image> image, QWidget* parent)
    : ViewportWidget(render::BackendKind::Native, std::move(image), parent)
{
}

ViewportWidget::ViewportWidget(render::BackendKind kind,
                               std::shared_ptr<const media::Image> image, QWidget* parent)
    : QWidget(parent)
    , m_backendKind(kind)
    , m_image(std::move(image))
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
    if (m_renderer || m_initAttempted) {        // 已初始化
        return;
    }
    m_initAttempted = true;

    try {
        // step 1 init renderer
        auto renderer = render::createRenderer(m_backendKind);
        if (!renderer) {
            throw std::runtime_error("Skia 后端未编译（需要 skia Release 预设）");
        }

        // QWidget::winId() 在 Windows 上是 HWND，在 macOS 上是 NSView*
        // Q: 为什么要 先创建renderer 再 move 到 m_renderer？
        // A: 先创建 renderer 并进行初始化，可以确保在初始化过程中捕获任何异常。如果直接在 m_renderer 中创建并初始化，一旦初始化失败，m_renderer 可能会处于不稳定状态。通过先创建一个局部的 renderer 对象，可以在初始化成功后再将其移动到 m_renderer，从而保证 m_renderer 始终处于有效状态。
        renderer->initialize(reinterpret_cast<void*>(winId()), physicalWidth(), physicalHeight());

        const char* backend = renderer->backendName();
        m_renderer = std::move(renderer);
        
        // step 2 set image
        if (m_image) {
            m_renderer->setImage(m_image->width, m_image->height, m_image->rgba16.data());
        }

        // step 3 apply view transform
        applyViewTransform();

        qInfo().noquote() << "[gfx] backend =" << backend
                          << " surface =" << physicalWidth() << "x" << physicalHeight()
                          << " dpr =" << devicePixelRatioF();
        emit rendererReady(QString::fromUtf8(backend));
    }
    catch (const std::exception& error) {
        qCritical().noquote() << "[gfx] 初始化失败:" << error.what();
        emit rendererFailed(QString::fromUtf8(error.what()));
    }
}

/* ==================== Qt Event ==================== */
// ViewportWidget entrance
void ViewportWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureRenderer();
    setFocus(Qt::OtherFocusReason);
    update();
}

void ViewportWidget::applyViewTransform()
{
    if (auto* transform = dynamic_cast<render::IViewTransform*>(m_renderer.get())) {
        transform->setQuarterTurns(m_quarterTurns);
        transform->setZoom(m_zoom);
    }
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_A:
        m_quarterTurns = (m_quarterTurns + 1) & 3;   // 逆时针 90°
        break;
    case Qt::Key_D:
        m_quarterTurns = (m_quarterTurns + 3) & 3;   // 顺时针 90°
        break;
    case Qt::Key_Q:
        m_zoom = std::max(kMinZoom, m_zoom / kZoomStep);   // 缩小
        break;
    case Qt::Key_E:
        m_zoom = std::min(kMaxZoom, m_zoom * kZoomStep);   // 放大
        break;
    case Qt::Key_R:
        m_quarterTurns = 0;
        m_zoom = 1.0f;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    applyViewTransform();
    update();
    event->accept();
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

// Renderer present entrance
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
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("渲染器未就绪\n看控制台日志"));
}

} // namespace chroma::app