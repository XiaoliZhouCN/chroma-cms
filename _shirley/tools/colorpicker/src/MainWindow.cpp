#include "MainWindow.hpp"

#include "HdrSwatch.hpp"

#include <QColor>
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cp
{
namespace
{
QColor previewColor(float r, float g, float b, DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        const auto encode = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            return value <= 0.0031308f ? 12.92f * value
                                       : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        };
        return QColor::fromRgbF(encode(r), encode(g), encode(b));
    }
    return QColor::fromRgbF(std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f),
                            std::clamp(b, 0.0f, 1.0f));
}

QString formatText(DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        return QStringLiteral("R16G16B16A16_FLOAT");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        return QStringLiteral("B8G8R8A8_UNORM");
    }
    return QString::fromStdString(formatToString(format));
}

QString cccsText(DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        return QStringLiteral("scRGB（BT.709，线性）");
    }
    return QStringLiteral("sRGB（BT.709，非线性）");
}

QString wireText(DXGI_COLOR_SPACE_TYPE colorSpace)
{
    switch (colorSpace) {
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        return QStringLiteral("BT.2020 + PQ（ST.2084）");
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        return QStringLiteral("BT.709 + gamma 2.2（sRGB）");
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        return QStringLiteral("BT.709 + 线性（scRGB）");
    default:
        return QString::fromStdString(colorSpaceToString(colorSpace));
    }
}

void makeDraggableLabel(QLabel *label)
{
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
}
} // namespace

MagnifierWidget::MagnifierWidget(QWidget *parent) : QWidget(parent)
{
    setFixedSize(160, 160);
}

void MagnifierWidget::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void MagnifierWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(16, 16, 20));

    if (m_image.isNull()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待捕获…"));
        return;
    }

    const QImage scaled = m_image.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    const QPoint origin((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
    painter.drawImage(origin, scaled);

    if (m_image.width() > 0 && m_image.height() > 0) {
        const qreal cellW = static_cast<qreal>(scaled.width()) / m_image.width();
        const qreal cellH = static_cast<qreal>(scaled.height()) / m_image.height();
        painter.setPen(QColor(0, 0, 0, 70));
        for (int x = 0; x <= m_image.width(); ++x) {
            const int px = origin.x() + static_cast<int>(std::lround(x * cellW));
            painter.drawLine(px, origin.y(), px, origin.y() + scaled.height());
        }
        for (int y = 0; y <= m_image.height(); ++y) {
            const int py = origin.y() + static_cast<int>(std::lround(y * cellH));
            painter.drawLine(origin.x(), py, origin.x() + scaled.width(), py);
        }
    }

    painter.setPen(QPen(QColor(240, 80, 80), 1));
    const int centerX = origin.x() + scaled.width() / 2;
    const int centerY = origin.y() + scaled.height() / 2;
    painter.drawLine(centerX - 5, centerY, centerX + 5, centerY);
    painter.drawLine(centerX, centerY - 5, centerX, centerY + 5);
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setStyleSheet(QStringLiteral("QWidget { background-color: #151517; color: #e8e8e8; }"));
    setFixedSize(560, 460);
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *top = new QHBoxLayout();
    top->setSpacing(10);

    m_magnifier = new MagnifierWidget(this);
    m_magnifier->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_swatch = new HdrSwatch(this);
    m_swatch->setFixedSize(120, 120);

    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto *right = new QVBoxLayout();
    right->setSpacing(6);
    m_cccsLabel = new QLabel(QStringLiteral("CCCS：—"), this);
    m_cccsLabel->setFont(fixedFont);
    m_cccsLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b0b0b0;"));
    makeDraggableLabel(m_cccsLabel);
    right->addWidget(m_cccsLabel);

    const auto makeValueLabel = [&](const QString &name) {
        auto *label = new QLabel(QStringLiteral("%1 = —").arg(name), this);
        label->setFont(fixedFont);
        label->setStyleSheet(QStringLiteral("font-size: 14px;"));
        label->setFixedHeight(26);
        makeDraggableLabel(label);
        return label;
    };
    m_rLabel = makeValueLabel(QStringLiteral("R"));
    m_gLabel = makeValueLabel(QStringLiteral("G"));
    m_bLabel = makeValueLabel(QStringLiteral("B"));
    right->addWidget(m_rLabel);
    right->addWidget(m_gLabel);
    right->addWidget(m_bLabel);
    right->addStretch(1);

    top->addWidget(m_magnifier);
    top->addWidget(m_swatch);
    top->addLayout(right, 1);
    root->addLayout(top);

    const QString infoNames[] = {QStringLiteral("捕获格式"),
                                 QStringLiteral("CCCS色域"),
                                 QStringLiteral("wire format"),
                                 QStringLiteral("SDR内容亮度滑杆值"),
                                 QStringLiteral("SDR白点"),
                                 QStringLiteral("1.0对应实际nits"),
                                 QStringLiteral("最大亮度"),
                                 QStringLiteral("位数"),
                                 QStringLiteral("Advanced Color")};
    for (const QString &name : infoNames) {
        auto *label = new QLabel(QStringLiteral("%1：—").arg(name), this);
        label->setStyleSheet(QStringLiteral("font-size: 11px; color: #b8b8b8;"));
        label->setFixedHeight(18);
        makeDraggableLabel(label);
        m_infoLabels.push_back(label);
        root->addWidget(label);
    }

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                m_paused = state != Qt::ApplicationActive;
                if (!m_paused) {
                    // 从 Windows 设置切回来：SDR 白点/HDR 模式可能已变化。
                    // 重新初始化捕获，避免 HDR 模式切换后 WGC/Duplication 会话失效。
                    if (m_monitor != nullptr) {
                        initCapture(m_monitor);
                    }
                    m_capture.refreshDisplayState();
                    updateInfoLines(m_hasLastSample ? m_lastSample : ScreenCapture::Sample{});
                    m_swatch->refreshSurface();
                }
            });

    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer->start();

    // 独立于采集：拖动 Windows HDR "SDR 内容亮度"滑杆时工具是 inactive，
    // 采集会暂停，但显示元数据仍需刷新（WM_SETTINGCHANGE / ImmersiveColorSet）。
    m_infoTimer = new QTimer(this);
    m_infoTimer->setInterval(500);
    connect(m_infoTimer, &QTimer::timeout, this, &MainWindow::refreshMetadata);
    m_infoTimer->start();

    tick();
}

void MainWindow::initCapture(HMONITOR monitor)
{
    m_monitor = monitor;
    m_captureReady = false;
    m_hasLastSample = false;
    m_monitorRect = {};
    if (monitor == nullptr) {
        return;
    }

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        m_monitorRect = info.rcMonitor;
    }

    m_captureReady = m_capture.initialize(monitor, reinterpret_cast<HWND>(winId()));
    m_hasLastSample = false;
    if (!m_captureReady) {
        m_infoLabels[0]->setText(QStringLiteral("捕获格式：初始化失败"));
        return;
    }
    updateInfoLines(ScreenCapture::Sample{});
}

void MainWindow::refreshMetadata()
{
    if (!m_captureReady) {
        return;
    }
    m_capture.refreshDisplayState();
    updateInfoLines(m_hasLastSample ? m_lastSample : ScreenCapture::Sample{});
}

void MainWindow::tick()
{
    if (m_paused) {
        return;
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (!m_captureReady) {
        initCapture(monitor);
    }
    else if (monitor != m_monitor && m_capture.backend() != ScreenCapture::Backend::Wgc) {
        initCapture(monitor);
    }

    if (!m_captureReady) {
        return;
    }
    if (monitor != m_monitor) {
        return;
    }

    m_capture.captureFrame();

    const int localX = cursor.x - m_monitorRect.left;
    const int localY = cursor.y - m_monitorRect.top;
    const ScreenCapture::Sample sample = m_capture.sampleRect(localX, localY, 1, 1);
    const ScreenCapture::Sample magnifierSample =
        m_capture.sampleRect(localX - 8, localY - 8, 16, 16);

    if (sample.ok) {
        m_lastSample = sample;
        m_lastMagnifierSample = magnifierSample;
        m_hasLastSample = true;
        updateReadout(sample, magnifierSample, cursor);
    }
}

void MainWindow::updateReadout(const ScreenCapture::Sample &sample,
                               const ScreenCapture::Sample &magSample, POINT cursor)
{
    Q_UNUSED(cursor);
    const bool isFp16 = sample.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const float r = sample.rgba[0];
    const float g = sample.rgba[1];
    const float b = sample.rgba[2];

    if (isFp16) {
        m_rLabel->setText(QStringLiteral("R = %1").arg(QString::number(r, 'f', 4)));
        m_gLabel->setText(QStringLiteral("G = %1").arg(QString::number(g, 'f', 4)));
        m_bLabel->setText(QStringLiteral("B = %1").arg(QString::number(b, 'f', 4)));
    }
    else {
        m_rLabel->setText(QStringLiteral("R = %1").arg(std::lround(r * 255.0f)));
        m_gLabel->setText(QStringLiteral("G = %1").arg(std::lround(g * 255.0f)));
        m_bLabel->setText(QStringLiteral("B = %1").arg(std::lround(b * 255.0f)));
    }

    m_cccsText = cccsText(sample.format);
    m_cccsLabel->setText(QStringLiteral("CCCS：%1").arg(m_cccsText));
    m_swatch->setColor(r, g, b, isFp16);
    updateMagnifier(magSample);
    updateInfoLines(sample);
}

void MainWindow::updateMagnifier(const ScreenCapture::Sample &sample)
{
    if (!sample.ok || sample.width <= 0 || sample.height <= 0) {
        return;
    }
    QImage image(sample.width, sample.height, QImage::Format_RGB888);
    for (int y = 0; y < sample.height; ++y) {
        for (int x = 0; x < sample.width; ++x) {
            const std::size_t base = static_cast<std::size_t>(y * sample.width + x) * 4;
            const QColor color = previewColor(sample.rgba[base + 0], sample.rgba[base + 1],
                                              sample.rgba[base + 2], sample.format);
            image.setPixelColor(x, y, color);
        }
    }
    m_magnifier->setImage(image);
}

void MainWindow::updateInfoLines(const ScreenCapture::Sample &sample)
{
    const DisplayState &display = m_capture.display();
    const DXGI_FORMAT format = sample.ok ? sample.format : m_capture.frameFormat();
    const bool isFp16 = format == DXGI_FORMAT_R16G16B16A16_FLOAT;

    const std::uint32_t sdrWhiteLevelRaw = display.sdrWhiteLevelRaw;
    const double oneNits = (m_capture.hdr() && isFp16) ? 80.0 : display.sdrWhiteNits;

    m_infoLabels[0]->setText(QStringLiteral("捕获格式：%1").arg(formatText(format)));
    m_infoLabels[1]->setText(QStringLiteral("CCCS色域：%1").arg(cccsText(format)));
    m_infoLabels[2]->setText(QStringLiteral("wire format：%1").arg(wireText(display.colorSpace)));
    m_infoLabels[3]->setText(QStringLiteral("SDR内容亮度滑杆值：%1（SDRWhiteLevel 原始值）")
                                 .arg(sdrWhiteLevelRaw));
    m_infoLabels[4]->setText(QStringLiteral("SDR白点：%1 nits")
                                 .arg(QString::number(display.sdrWhiteNits, 'f', 1)));
    m_infoLabels[5]->setText(QStringLiteral("1.0对应实际nits：%1 nits")
                                 .arg(QString::number(oneNits, 'f', 1)));
    m_infoLabels[6]->setText(QStringLiteral("最大亮度：%1 nits")
                                 .arg(QString::number(display.maxLuminance, 'f', 1)));
    m_infoLabels[7]->setText(QStringLiteral("位数：%1").arg(display.bitsPerColor));
    m_infoLabels[8]->setText(QStringLiteral("Advanced Color：%1")
                                 .arg(display.advancedColorEnabled ? QStringLiteral("已启用")
                                                                  : QStringLiteral("未启用")));
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::ScreenChangeInternal) {
        if (m_swatch) {
            m_swatch->refreshSurface();
        }
    }
    return QWidget::event(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}
} // namespace cp
