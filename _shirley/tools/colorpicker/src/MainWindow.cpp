#include "MainWindow.hpp"

#include <QColor>
#include <QFontDatabase>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygon>
#include <QTimer>


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

QString formatLabelText(DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        return QStringLiteral("捕获格式：R16G16B16A16_FLOAT（scRGB 线性，1.0 = 80 nits）");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        return QStringLiteral("捕获格式：B8G8R8A8_UNORM（sRGB 8-bit）");
    }
    return QStringLiteral("捕获格式：%1").arg(QString::fromStdString(formatToString(format)));
}

} // namespace

MagnifierWidget::MagnifierWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(100, 80);
    setMaximumSize(100, 80);
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
        painter.setPen(QColor(160, 160, 160));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待捕获…"));
        return;
    }

    const QImage scaled =
        m_image.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    const QPoint origin((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
    painter.drawImage(origin, scaled);

    // 16× 像素网格
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
    painter.drawLine(centerX - 4, centerY, centerX + 4, centerY);
    painter.drawLine(centerX, centerY - 4, centerX, centerY + 4);
}

SwatchWidget::SwatchWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(50, 50);
    setMaximumSize(50, 50);
}

void SwatchWidget::setColor(const QColor &color)
{
    m_color = color;
    update();
}

void SwatchWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_color);
    painter.setPen(QPen(QColor(220, 220, 220), 2));
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Chroma ColorPicker — HDR raw（1×1）"));
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setMinimumSize(560, 320);
    resize(580, 340);

    setStyleSheet(QStringLiteral("QWidget { background-color: #1e1e22; color: #e8e8e8; }"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);

    m_magnifier = new MagnifierWidget(this);
    m_swatch = new SwatchWidget(this);

    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto createValueLabel = [&](const QString &name) {
        auto *label = new QLabel(QStringLiteral("%1 = …").arg(name), this);
        label->setFont(fixedFont);
        return label;
    };
    m_rLabel = createValueLabel(QStringLiteral("R"));
    m_gLabel = createValueLabel(QStringLiteral("G"));
    m_bLabel = createValueLabel(QStringLiteral("B"));

    m_formatLabel = new QLabel(QStringLiteral("捕获格式：初始化中…"), this);
    m_formatLabel->setWordWrap(true);
    m_formatLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_warningLabel->setStyleSheet(QStringLiteral("color: #ff9060;"));
    m_warningLabel->hide();
    m_infoLabel = new QLabel(QStringLiteral("显示器状态：初始化中…"), this);
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *topGrid = new QGridLayout();
    topGrid->setHorizontalSpacing(10);
    topGrid->setVerticalSpacing(4);
    topGrid->addWidget(m_magnifier, 0, 0, 2, 1, Qt::AlignLeft | Qt::AlignTop);
    topGrid->addWidget(m_swatch, 0, 1, 2, 1, Qt::AlignLeft | Qt::AlignTop);
    topGrid->addWidget(m_rLabel, 0, 2);
    topGrid->addWidget(m_gLabel, 1, 2);
    topGrid->addWidget(m_bLabel, 2, 2);
    topGrid->setColumnStretch(2, 1);

    root->addLayout(topGrid);
    root->addWidget(m_formatLabel);
    root->addWidget(m_warningLabel);
    root->addWidget(m_infoLabel);
    root->addStretch(1);

    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer->start();

    tick();
}

void MainWindow::initCapture(HMONITOR monitor)
{
    m_monitor = monitor;
    m_captureReady = false;
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
    if (!m_captureReady) {
        m_formatLabel->setText(QStringLiteral("捕获初始化失败（HDR/独占全屏/受保护内容？）"));
        m_infoLabel->setText(QStringLiteral("无法初始化 Desktop Duplication。"));
        return;
    }

    m_formatLabel->setText(formatLabelText(m_capture.frameFormat()));
    updateDisplayInfo();
}

void MainWindow::tick()
{
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (monitor != m_monitor || !m_captureReady) {
        initCapture(monitor);
    }
    if (!m_captureReady) {
        return;
    }

    m_capture.captureFrame();

    const int localX = cursor.x - m_monitorRect.left;
    const int localY = cursor.y - m_monitorRect.top;
    const ScreenCapture::Sample sample = m_capture.sampleRect(localX, localY, 1, 1);
    const ScreenCapture::Sample magnifierSample =
        m_capture.sampleRect(localX - 3, localY - 2, 6, 5);

    if (sample.ok) {
        updateReadout(sample, magnifierSample, cursor);
    }
    else {
        m_formatLabel->setText(QStringLiteral("等待捕获帧…"));
    }

    if (++m_tickCounter % 40 == 0) {
        m_capture.refreshDisplayState();
        m_formatLabel->setText(formatLabelText(m_capture.frameFormat()));
        updateDisplayInfo();
    }
}

void MainWindow::updateReadout(const ScreenCapture::Sample &sample,
                               const ScreenCapture::Sample &magSample, POINT cursor)
{
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

    m_swatch->setColor(previewColor(r, g, b, sample.format));
    updateMagnifier(magSample);

    const DisplayState &display = m_capture.display();
    const bool clipped = isFp16 && (r > 1.0f || g > 1.0f || b > 1.0f);
    m_formatLabel->setText(formatLabelText(sample.format) +
                          (clipped ? QStringLiteral("　预览色块已裁剪到 [0,1]") : QString()));

    if (display.hdr && !isFp16) {
        m_warningLabel->setText(
            QStringLiteral("HDR 已开启但捕获到 BGRA8：HDR 数值可能被截断（需补 WGC 后端）。"));
        m_warningLabel->show();
    }
    else {
        m_warningLabel->hide();
    }

    setWindowTitle(QStringLiteral("Chroma ColorPicker — (%1, %2)").arg(cursor.x).arg(cursor.y));
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

void MainWindow::updateDisplayInfo()
{
    const DisplayState &display = m_capture.display();
    QString text;
    text += QStringLiteral("HDR：%1（%2）　SDR 白点：%3 nits\n")
                .arg(display.hdr ? QStringLiteral("开") : QStringLiteral("关"),
                     display.hdr ? QStringLiteral("HDR10/PQ") : QStringLiteral("SDR/sRGB"),
                     QString::number(display.sdrWhiteNits, 'f', 1));
    text += QStringLiteral("最大亮度：%1 nits　位数：%2 bit　Advanced Color：%3\n")
                .arg(QString::number(display.maxLuminance, 'f', 0))
                .arg(display.bitsPerColor)
                .arg(display.advancedColorEnabled ? QStringLiteral("已启用")
                                                 : QStringLiteral("未启用"));
    text += QStringLiteral("显示器：%1（%2）")
                .arg(QString::fromStdWString(display.friendlyName),
                     QString::fromStdWString(display.gdiName));
    m_infoLabel->setText(text);
}
} // namespace cp
