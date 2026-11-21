#include "app/MainWindow.hpp"

#include "app/AssistInfoWidget.hpp"
#include "app/CSMViewerWidget.hpp"
#include "app/ViewportWidget.hpp"
#include "core/Version.hpp"
#include "probe/ProbeConfig.hpp"
#include "probe/ProbeCsvWriter.hpp"
#include "render/RendererFactory.hpp"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QFileInfo>
#include <QFileInfoList>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QShortcut>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace chroma::app {
namespace {

QString nativeApiName()
{
#if defined(__APPLE__)
    return QStringLiteral("Metal");
#elif defined(_WIN32)
    return QStringLiteral("D3D12");
#else
    return QStringLiteral("Native");
#endif
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(nullptr, QString(), parent)
{
}

MainWindow::MainWindow(std::shared_ptr<const media::Image> image, QString imagePath,
                       QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ChromaCMS %1 — practice")
                       .arg(QString::fromStdString(std::string(kVersion))));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setStyleSheet(QStringLiteral("QMainWindow { background-color: #000000; }"));

    buildLayout(std::move(image), std::move(imagePath));
    updateWindowGeometry();
    applyDisplayMode();
    applyViewTransforms();
    initProbe();

    qApp->installEventFilter(this);

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::ApplicationShortcut);
    connect(esc, &QShortcut::activated, qApp, &QCoreApplication::quit);
}

void MainWindow::buildLayout(std::shared_ptr<const media::Image> image, QString imagePath)
{
    m_image = std::move(image);
    m_imagePath = std::move(imagePath);

    m_central = new QWidget(this);
    m_central->setObjectName(QStringLiteral("root"));
    m_central->setStyleSheet(QStringLiteral("QWidget#root { background-color: #000000; }"));

    m_native = new ViewportWidget(render::BackendKind::Native, m_image, m_central);
    m_skia = new ViewportWidget(render::BackendKind::Skia, m_image, m_central);
    m_native->show();
    m_skia->show();
    setCentralWidget(m_central);

    for (ViewportWidget* viewport : {m_native, m_skia}) {
        viewport->installEventFilter(this);
        for (CSMViewerWidget* viewer : {viewport->nativeViewer(), viewport->skiaViewer()}) {
            if (viewer == nullptr) {
                continue;
            }
            viewer->installEventFilter(this);
            connect(viewer, &CSMViewerWidget::framePresented, this,
                    [this, viewer] { onFramePresented(viewer); });
        }
    }

    connect(m_native, &ViewportWidget::rendererReady, this, [this](const QString&) { updateInfo(); });
    connect(m_skia, &ViewportWidget::rendererReady, this, [this](const QString&) { updateInfo(); });
    connect(m_native, &ViewportWidget::rendererFailed, this, [this](const QString&) { updateInfo(); });
    connect(m_skia, &ViewportWidget::rendererFailed, this, [this](const QString&) { updateInfo(); });
}

void MainWindow::updateWindowGeometry()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        setGeometry(screen->availableGeometry());
    }
}

void MainWindow::layoutViewports()
{
    if (m_central == nullptr || m_native == nullptr || m_skia == nullptr) {
        return;
    }
    const QRect r = m_central->rect();
    if (m_mode == DisplayMode::Both) {
        const int half = r.width() / 2;
        m_native->setGeometry(QRect(0, 0, half, r.height()));
        m_skia->setGeometry(QRect(half, 0, r.width() - half, r.height()));
        m_native->assistInfo()->show();
        m_skia->assistInfo()->show();
        m_native->raise();
        m_skia->raise();
    }
    else {
        m_native->setGeometry(r);
        m_skia->setGeometry(r);
        // 单图时两个 viewport 重叠；只显示当前 viewport 的 info，
        // 否则底层 viewport 的半透明 info 会透出来，形成另一个 API 的灰字。
        if (m_mode == DisplayMode::NativeOnly) {
            m_native->assistInfo()->show();
            m_skia->assistInfo()->hide();
            m_native->raise();
        }
        else {
            m_skia->assistInfo()->show();
            m_native->assistInfo()->hide();
            m_skia->raise();
        }
    }
}

void MainWindow::applyDisplayMode()
{
    if (m_native == nullptr || m_skia == nullptr) {
        return;
    }

    // 两个 ViewportWidget 始终可见，只改变位置/层级；
    // 切模式不 hide/show，避免 native surface 首次显示造成闪帧。
    m_native->show();
    m_skia->show();

    layoutViewports();

    if (m_mode == DisplayMode::NativeOnly) {
        m_native->showBackend(render::BackendKind::Native);
    }
    else if (m_mode == DisplayMode::SkiaOnly) {
        m_skia->showBackend(render::BackendKind::Skia);
    }
    else {
        m_native->showBackend(render::BackendKind::Native);
        m_skia->showBackend(render::BackendKind::Skia);
    }

    updateInfo();

    if (ViewportWidget* view = activeViewport()) {
        if (view->viewer()) {
            view->viewer()->setFocus(Qt::OtherFocusReason);
        }
    }
}

void MainWindow::toggleDisplayMode()
{
    endTabOverride();
    m_mode = static_cast<DisplayMode>((static_cast<int>(m_mode) + 1) % 3);
    applyDisplayMode();
}

ViewportWidget* MainWindow::viewportUnderMouse() const
{
    const QPoint global = QCursor::pos();
    if (m_native != nullptr && m_native->isVisible() &&
        m_native->rect().contains(m_native->mapFromGlobal(global))) {
        return m_native;
    }
    if (m_skia != nullptr && m_skia->isVisible() &&
        m_skia->rect().contains(m_skia->mapFromGlobal(global))) {
        return m_skia;
    }
    return nullptr;
}

ViewportWidget* MainWindow::activeViewport() const
{
    if (m_mode == DisplayMode::NativeOnly) {
        return m_native;
    }
    if (m_mode == DisplayMode::SkiaOnly) {
        return m_skia;
    }
    if (ViewportWidget* view = viewportUnderMouse()) {
        return view;
    }
    if (m_hoverViewport != nullptr) {
        return m_hoverViewport;
    }
    if (m_overrideViewport != nullptr) {
        return m_overrideViewport;
    }
    return nullptr;
}

void MainWindow::beginTabOverride()
{
    if (m_overrideActive) {
        return;
    }
    ViewportWidget* view = activeViewport();
    if (view == nullptr) {
        return;
    }

    m_overrideViewport = view;
    m_overrideOriginalBackend = static_cast<int>(view->backendKind());
    const render::BackendKind other =
        (view->backendKind() == render::BackendKind::Skia)
            ? render::BackendKind::Native
            : render::BackendKind::Skia;
    view->showBackend(other);
    if (view->viewer()) {
        view->viewer()->setFocus(Qt::OtherFocusReason);
    }
    m_overrideActive = true;
    updateInfo();
}

void MainWindow::endTabOverride()
{
    if (!m_overrideActive || m_overrideViewport == nullptr) {
        return;
    }
    m_overrideViewport->showBackend(static_cast<render::BackendKind>(m_overrideOriginalBackend));
    m_overrideActive = false;
    m_overrideViewport = nullptr;
    updateInfo();
}

void MainWindow::applyViewTransforms()
{
    // 两个 viewport、两个 API 使用同一套状态。
    for (ViewportWidget* viewport : {m_native, m_skia}) {
        if (viewport == nullptr) {
            continue;
        }
        viewport->setApiTransform(render::BackendKind::Native, m_quarterTurns, m_zoom);
        viewport->setApiTransform(render::BackendKind::Skia, m_quarterTurns, m_zoom);
    }
}

bool MainWindow::handleTransformKey(int key)
{
    switch (key) {
    case Qt::Key_A:
        m_quarterTurns = (m_quarterTurns + 1) & 3;
        break;
    case Qt::Key_D:
        m_quarterTurns = (m_quarterTurns + 3) & 3;
        break;
    case Qt::Key_Q:
        m_zoom = std::max(0.1f, m_zoom / 1.1f);
        break;
    case Qt::Key_E:
        m_zoom = std::min(8.0f, m_zoom * 1.1f);
        break;
    case Qt::Key_R:
        m_quarterTurns = 0;
        m_zoom = 1.0f;
        break;
    default:
        return false;
    }

    applyViewTransforms();
    return true;
}

void MainWindow::updateHover(QObject* watched)
{
    QObject* object = watched;
    while (object != nullptr) {
        if (auto* viewport = qobject_cast<ViewportWidget*>(object)) {
            m_hoverViewport = viewport;
            if (m_mode == DisplayMode::Both && viewport->viewer() != nullptr) {
                viewport->viewer()->setFocus(Qt::OtherFocusReason);
            }
            return;
        }
        object = object->parent();
    }
}

void MainWindow::updateInfo()
{
    const QString imageName = QFileInfo(m_imagePath).fileName();
    const auto textFor = [&](ViewportWidget* view) {
        if (view == nullptr) return QString();
        const QString api = view->backendKind() == render::BackendKind::Native
            ? nativeApiName()
            : QStringLiteral("Skia");
        return QStringLiteral("ChromaCMS · CSM 对照\n"
                              "API: %1\n"
                              "Image: %2\n"
                              "Enter: Skia / Native / Dual\n"
                              "Tab: switch API\n"
                              "Esc: quit")
            .arg(api, imageName);
    };
    if (m_native) m_native->setInfoText(textFor(m_native));
    if (m_skia) m_skia->setInfoText(textFor(m_skia));
}

void MainWindow::initProbe()
{
    const QString enabled = qEnvironmentVariable("CHROMA_PROBE");
    if (enabled.isEmpty() || enabled == QStringLiteral("0")) {
        return;
    }

    const QString configPath = qEnvironmentVariable(
        "CHROMA_PROBE_CONFIG",
        QStringLiteral(CHROMA_SOURCE_DIR "/src/probe/card_config/17_ramp_config.json"));

    m_probeConfig = std::make_unique<probe::ProbeConfig>();
    QString error;
    if (!m_probeConfig->load(configPath, QStringLiteral(CHROMA_SOURCE_DIR), &error)) {
        qCritical().noquote() << "[probe]" << error;
        m_probeConfig.reset();
        return;
    }

    m_probeOutputPath = qEnvironmentVariable("CHROMA_PROBE_OUTPUT",
                                              m_probeConfig->outputPath);
    m_probePending = true;
    qInfo().noquote() << "[probe] loaded" << m_probeConfig->patches.size()
                      << "patches; output =" << m_probeOutputPath;
}

void MainWindow::onFramePresented(CSMViewerWidget* viewer)
{
    if (!m_probePending || m_probeConfig == nullptr) {
        return;
    }
    ViewportWidget* active = activeViewport();
    if (active == nullptr || active->viewer() != viewer) {
        return;
    }
    runProbe(viewer);
}

void MainWindow::runProbe(CSMViewerWidget* viewer)
{
    m_probePending = false;

    std::vector<render::PixelPoint> points;
    points.reserve(m_probeConfig->patches.size());
    for (const probe::PatchCoordinate& patch : m_probeConfig->patches) {
        const QPointF surface = viewer->imagePointToSurface(patch.image);
        if (surface.x() < 0.0 || surface.y() < 0.0) {
            qCritical().noquote() << "[probe] patch outside surface:" << patch.block;
            return;
        }
        points.push_back({static_cast<std::uint32_t>(std::lround(surface.x())),
                          static_cast<std::uint32_t>(std::lround(surface.y()))});
    }

    std::vector<render::RgbaF32> values;
    if (!viewer->readPixels(points, values)) {
        qCritical().noquote() << "[probe] readPixels 失败，需要 SkiaRenderer readback";
        return;
    }
    if (values.size() != m_probeConfig->patches.size()) {
        qCritical().noquote() << "[probe] 采样数量不匹配:" << values.size()
                              << "!=" << m_probeConfig->patches.size();
        return;
    }

    std::vector<probe::ProbeValue> rows;
    rows.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        rows.push_back({m_probeConfig->patches[i].block,
                        values[i].r, values[i].g, values[i].b});
    }

    QString error;
    if (!probe::ProbeCsvWriter::write(m_probeOutputPath, rows, &error)) {
        qCritical().noquote() << "[probe]" << error;
        return;
    }
    qInfo().noquote() << "[probe] wrote" << rows.size() << "rows ->" << m_probeOutputPath;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter) {
        updateHover(watched);
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            toggleDisplayMode();
            return true;
        }
        if (key->key() == Qt::Key_Tab) {
            beginTabOverride();
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            qApp->quit();
            return true;
        }
        if (handleTransformKey(key->key())) {
            return true;
        }
    }
    else if (event->type() == QEvent::KeyRelease) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab) {
            endTabOverride();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        toggleDisplayMode();
        return;
    }
    if (event->key() == Qt::Key_Tab) {
        beginTabOverride();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        qApp->quit();
        return;
    }
    if (handleTransformKey(event->key())) {
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab) {
        endTabOverride();
        return;
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    layoutViewports();
}

} // namespace chroma::app
