#include "app/CSMViewerWidget.hpp"

#include "render/RendererFactory.hpp"
#include "render/IRenderer.hpp"
#include "render/IViewTransform.hpp"

#include "media/ImageLoader.hpp"

#include <utility>

#include <QColor>
#include <QPointF>
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

CSMViewerWidget::CSMViewerWidget(QWidget* parent)
    : CSMViewerWidget(render::BackendKind::Native, nullptr, parent)
{
}

CSMViewerWidget::CSMViewerWidget(std::shared_ptr<const media::Image> image, QWidget* parent)
    : CSMViewerWidget(render::BackendKind::Native, std::move(image), parent)
{
}

CSMViewerWidget::CSMViewerWidget(render::BackendKind kind,
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

CSMViewerWidget::~CSMViewerWidget() = default;

void CSMViewerWidget::setImage(std::shared_ptr<const media::Image> image)
{
    m_image = std::move(image);
    if (m_renderer && m_image) {
        m_renderer->setImage(m_image->width, m_image->height, m_image->rgba16.data());
        applyViewTransform();
    }
    update();
}

void CSMViewerWidget::setViewTransform(int quarterTurns, float zoom)
{
    m_quarterTurns = ((quarterTurns % 4) + 4) % 4;
    m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    applyViewTransform();
    update();
}

QPointF CSMViewerWidget::imagePointToSurface(const QPointF& imagePoint) const
{
    if (!m_image || m_image->width == 0 || m_image->height == 0 || width() <= 0 ||
        height() <= 0) {
        return QPointF(-1.0, -1.0);
    }

    const double surfaceW = static_cast<double>(physicalWidth());
    const double surfaceH = static_cast<double>(physicalHeight());
    const double imageW = static_cast<double>(m_image->width);
    const double imageH = static_cast<double>(m_image->height);
    const double imageAspect = imageW / imageH;
    const double surfaceAspect = surfaceW / surfaceH;

    const int q = m_quarterTurns & 3;
    const double c = (q == 0) ? 1.0 : (q == 2) ? -1.0 : 0.0;
    const double s = (q == 1) ? 1.0 : (q == 3) ? -1.0 : 0.0;
    const double invZoom = (m_zoom > 0.0f) ? (1.0 / static_cast<double>(m_zoom)) : 1.0;

    const double effectiveAspect = ((q & 1) != 0) ? (1.0 / imageAspect) : imageAspect;
    double viewWidth = surfaceW;
    double viewHeight = surfaceH;
    if (effectiveAspect > surfaceAspect) {
        viewHeight = surfaceW / effectiveAspect;
    }
    else {
        viewWidth = surfaceH * effectiveAspect;
    }
    const double viewLeft = (surfaceW - viewWidth) * 0.5;
    const double viewTop = (surfaceH - viewHeight) * 0.5;

    const double u00 = c * invZoom;
    const double u01 = -s * invZoom / imageAspect;
    const double u10 = s * invZoom * imageAspect;
    const double u11 = c * invZoom;

    const double rx = viewLeft / viewWidth;
    const double ry = viewTop / viewHeight;
    const double a00 = imageW * u00 / viewWidth;
    const double a01 = imageW * u01 / viewHeight;
    const double a10 = imageH * u10 / viewWidth;
    const double a11 = imageH * u11 / viewHeight;
    const double bx = imageW * (0.5 - 0.5 * (u00 + u01) - (u00 * rx + u01 * ry));
    const double by = imageH * (0.5 - 0.5 * (u10 + u11) - (u10 * rx + u11 * ry));

    // 与 SkiaRenderer::drawFrame 的 deviceToImage 矩阵一致，求逆得到
    // image -> device(surface) 映射。
    const double det = a00 * a11 - a01 * a10;
    if (std::abs(det) < 1e-12) {
        return QPointF(-1.0, -1.0);
    }
    const double dx = imagePoint.x() - bx;
    const double dy = imagePoint.y() - by;
    const double surfaceX = (a11 * dx - a01 * dy) / det;
    const double surfaceY = (-a10 * dx + a00 * dy) / det;
    if (surfaceX < 0.0 || surfaceY < 0.0 || surfaceX >= surfaceW || surfaceY >= surfaceH) {
        return QPointF(-1.0, -1.0);
    }
    return QPointF(surfaceX, surfaceY);
}

bool CSMViewerWidget::readPixels(const std::vector<render::PixelPoint>& points,
                                 std::vector<render::RgbaF32>& out)
{
    auto* readback = dynamic_cast<render::IRendererReadback*>(m_renderer.get());
    if (readback == nullptr) {
        return false;
    }
    return readback->readPixels(points, out);
}

void CSMViewerWidget::setBackend(render::BackendKind kind)
{
    if (m_backendKind == kind) {
        return;
    }
    m_renderer.reset();
    m_initAttempted = false;
    m_backendKind = kind;
    if (isVisible()) {
        ensureRenderer();
    }
    update();
}

std::uint32_t CSMViewerWidget::physicalWidth() const
{
    return static_cast<std::uint32_t>(std::lround(width() * devicePixelRatioF()));
}

std::uint32_t CSMViewerWidget::physicalHeight() const
{
    return static_cast<std::uint32_t>(std::lround(height() * devicePixelRatioF()));
}

void CSMViewerWidget::ensureRenderer()
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
            m_renderer->setImage(m_image->width, m_image->height, m_image->rgba16.data());          // 获取该 vector 连续内存的首地址
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
void CSMViewerWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureRenderer();
    setFocus(Qt::OtherFocusReason);
    update();
}

void CSMViewerWidget::applyViewTransform()
{
    if (auto* transform = dynamic_cast<render::IViewTransform*>(m_renderer.get())) {
        transform->setQuarterTurns(m_quarterTurns);
        transform->setZoom(m_zoom);
    }
}

void CSMViewerWidget::keyPressEvent(QKeyEvent* event)
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

void CSMViewerWidget::resizeEvent(QResizeEvent* event)
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
void CSMViewerWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_renderer) {
        try {
            m_renderer->present();
            emit framePresented();
            return;
        }
        catch (const std::exception& error) {
            qCritical().noquote() << "[gfx] present 失败:" << error.what();
        }
    }

    // 渲染器没起来时的占位，避免全黑看不出状态
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0));
    painter.setPen(QColor(230, 120, 120));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("渲染器未就绪\n看控制台日志"));
}

} // namespace chroma::app