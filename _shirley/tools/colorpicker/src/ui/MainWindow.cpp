#include "MainWindow.hpp"

#include "ui/HdrPreview.hpp"
#include "core/Platform.hpp"

#include <QApplication>
#include <QColor>
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
bool sameMonitor(const MonitorInfo &left, const MonitorInfo &right)
{
    if (left.nativeHandle != 0 && right.nativeHandle != 0) {
        return left.nativeHandle == right.nativeHandle;
    }
    return left.id == right.id;
}

QColor previewColor(float r, float g, float b, PixelEncoding encoding)
{
    if (encoding == PixelEncoding::Fp16) {
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

QString formatText(const CaptureFormat &format)
{
    return QString::fromStdString(format.name);
}

QString cccsText(const CaptureFormat &format)
{
    if (format.encoding == PixelEncoding::Fp16) {
        return QStringLiteral("scRGB（BT.709，线性）");
    }
    return QStringLiteral("sRGB（BT.709，非线性）");
}

QString optionalNumber(const std::optional<double> &value, int precision)
{
    return value ? QString::number(*value, 'f', precision) : QStringLiteral("n/a");
}

QString optionalUInt(const std::optional<std::uint32_t> &value)
{
    return value ? QString::number(*value) : QStringLiteral("n/a");
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

    painter.setPen(QPen(QColor(240, 80, 80), 1));
    const int centerX = origin.x() + scaled.width() / 2;
    const int centerY = origin.y() + scaled.height() / 2;
    painter.drawLine(centerX - 5, centerY, centerX + 5, centerY);
    painter.drawLine(centerX, centerY - 5, centerX, centerY + 5);
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    m_probe = createDisplayProbe();
    m_capture = createCaptureSession();
    m_swatch = createHdrPreview(this);

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

    if (QWidget *preview = m_swatch ? m_swatch->widget() : nullptr) {
        preview->setFixedSize(120, 120);
    }

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
    if (m_swatch) {
        top->addWidget(m_swatch->widget());
    }
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
                    if (!m_monitor.id.empty()) {
                        initCapture(m_monitor);
                    }
                    if (m_capture) {
                        m_capture->refreshDisplayState();
                    }
                    updateInfoLines(m_hasLastSample ? m_lastSample : ColorSample{});
                    if (m_swatch) {
                        m_swatch->refreshSurface();
                    }
                }
            });

    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer->start();

    m_infoTimer = new QTimer(this);
    m_infoTimer->setInterval(500);
    connect(m_infoTimer, &QTimer::timeout, this, &MainWindow::refreshMetadata);
    m_infoTimer->start();

    tick();
}

void MainWindow::initCapture(const MonitorInfo &monitor)
{
    m_monitor = monitor;
    m_captureReady = false;
    m_hasLastSample = false;
    m_monitorRect = monitor.bounds;
    if (monitor.id.empty()) {
        return;
    }

    m_captureReady = (m_capture != nullptr) &&
                     m_capture->initialize(monitor, static_cast<NativeWindow>(winId()));
    if (!m_captureReady) {
        if (!m_infoLabels.isEmpty()) {
            m_infoLabels[0]->setText(QStringLiteral("捕获格式：初始化失败"));
        }
        return;
    }
    updateInfoLines(ColorSample{});
}

void MainWindow::refreshMetadata()
{
    if (!m_captureReady || !m_capture) {
        return;
    }
    m_capture->refreshDisplayState();
    updateInfoLines(m_hasLastSample ? m_lastSample : ColorSample{});
}

void MainWindow::tick()
{
    if (m_paused || !m_probe || !m_capture) {
        return;
    }

    const PointI cursor = m_probe->cursorPosition();
    const MonitorInfo monitor = m_probe->monitorAt(cursor);
    if (monitor.id.empty()) {
        return;
    }

    if (!m_captureReady) {
        initCapture(monitor);
    }
    else if (!sameMonitor(monitor, m_monitor) &&
             m_capture->backend() != CaptureBackendKind::Wgc) {
        initCapture(monitor);
    }

    if (!m_captureReady || !sameMonitor(monitor, m_monitor)) {
        return;
    }

    m_capture->captureFrame();

    const int localX = cursor.x - m_monitorRect.left;
    const int localY = cursor.y - m_monitorRect.top;
    const ColorSample sample = m_capture->sampleRect(localX, localY, 1, 1);
    const ColorSample magnifierSample = m_capture->sampleRect(localX - 8, localY - 8, 16, 16);

    if (sample.ok) {
        m_lastSample = sample;
        m_lastMagnifierSample = magnifierSample;
        m_hasLastSample = true;
        updateReadout(sample, magnifierSample);
    }
}

void MainWindow::updateReadout(const ColorSample &sample, const ColorSample &magSample)
{
    const bool isFp16 = sample.format.encoding == PixelEncoding::Fp16;
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

    m_cccsLabel->setText(QStringLiteral("CCCS：%1").arg(cccsText(sample.format)));
    if (m_swatch) {
        m_swatch->setColor(r, g, b, isFp16);
    }
    updateMagnifier(magSample);
    updateInfoLines(sample);
}

void MainWindow::updateMagnifier(const ColorSample &sample)
{
    if (!sample.ok || sample.width <= 0 || sample.height <= 0) {
        return;
    }
    QImage image(sample.width, sample.height, QImage::Format_RGB888);
    for (int y = 0; y < sample.height; ++y) {
        for (int x = 0; x < sample.width; ++x) {
            const std::size_t base = static_cast<std::size_t>(y * sample.width + x) * 4;
            const QColor color = previewColor(sample.rgba[base + 0], sample.rgba[base + 1],
                                              sample.rgba[base + 2], sample.format.encoding);
            image.setPixelColor(x, y, color);
        }
    }
    m_magnifier->setImage(image);
}

void MainWindow::updateInfoLines(const ColorSample &sample)
{
    if (!m_capture || m_infoLabels.size() < 9) {
        return;
    }
    const DisplayInfo &display = m_capture->display();
    const CaptureFormat format = sample.ok ? sample.format : m_capture->format();
    const bool isFp16 = format.encoding == PixelEncoding::Fp16;

    const double oneNits =
        (display.hdr && isFp16) ? 80.0 : display.sdrWhiteNits.value_or(80.0);

    m_infoLabels[0]->setText(QStringLiteral("捕获格式：%1").arg(formatText(format)));
    m_infoLabels[1]->setText(QStringLiteral("CCCS色域：%1").arg(cccsText(format)));
    m_infoLabels[2]->setText(QStringLiteral("wire format：%1")
                                 .arg(QString::fromStdString(display.wireFormat.empty() ? display.colorSpace
                                                                    : display.wireFormat)));
    m_infoLabels[3]->setText(QStringLiteral("SDR内容亮度滑杆值：%1（SDRWhiteLevel 原始值）")
                                 .arg(optionalUInt(display.sdrWhiteLevelRaw)));
    m_infoLabels[4]->setText(QStringLiteral("SDR白点：%1 nits")
                                 .arg(optionalNumber(display.sdrWhiteNits, 1)));
    m_infoLabels[5]->setText(QStringLiteral("1.0对应实际nits：%1 nits")
                                 .arg(QString::number(oneNits, 'f', 1)));
    m_infoLabels[6]->setText(QStringLiteral("最大亮度：%1 nits")
                                 .arg(optionalNumber(display.maxLuminance, 1)));
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
