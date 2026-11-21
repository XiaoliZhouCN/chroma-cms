#include "MainWindow.hpp"

#include "core/Platform.hpp"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
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
// 界面基础字号：当前约等于原 8px 的 1.5 倍。
constexpr int kFontSmall = 12;
constexpr int kFontTitle = 14;

bool sameMonitor(const MonitorInfo &left, const MonitorInfo &right)
{
    if (left.nativeHandle != 0 && right.nativeHandle != 0) {
        return left.nativeHandle == right.nativeHandle;
    }
    return left.id == right.id;
}

QColor previewColor(float r, float g, float b, PixelEncoding encoding)
{
    if (encoding == PixelEncoding::Fp16 ||
        encoding == PixelEncoding::Rgba16Float ||
        encoding == PixelEncoding::YCbCr10) {
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
    return format.name.empty() ? QStringLiteral("—") : QString::fromStdString(format.name);
}

QString colorSpaceText(const CaptureFormat &format)
{
    if (format.encoding == PixelEncoding::Fp16) {
        return QStringLiteral("CCCS · scRGB");
    }
    if (format.encoding == PixelEncoding::Rgba16Float ||
        format.encoding == PixelEncoding::YCbCr10) {
        return QStringLiteral("Extended Linear P3");
    }
    if (format.encoding == PixelEncoding::Bgra8) {
        return QStringLiteral("sRGB");
    }
    return QStringLiteral("—");
}

QString optionalNumber(const std::optional<double> &value, int precision)
{
    return value ? QString::number(*value, 'f', precision) : QStringLiteral("n/a");
}

QString optionalUInt(const std::optional<std::uint32_t> &value)
{
    return value ? QString::number(*value) : QStringLiteral("n/a");
}

QFont uiFont(int pixelSize, bool bold = false, bool mono = false)
{
    QFont font = mono ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont();
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    return font;
}

QLabel *makeReadoutValue(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(QStringLiteral("ReadoutValue"));
    label->setFont(uiFont(kFontSmall, false, true));
    label->setStyleSheet(QStringLiteral("color: #e9e9f0;"));
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

void setPanelValue(QVector<QLabel *> &values, int index, const QString &text)
{
    if (index < 0 || index >= values.size() || values[index] == nullptr) {
        return;
    }
    values[index]->setText(text.isEmpty() ? QStringLiteral("n/a") : text);
}

} // namespace

MagnifierWidget::MagnifierWidget(QWidget *parent) : QWidget(parent)
{
    setFixedSize(80, 80);
}

void MagnifierWidget::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void MagnifierWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(22, 22, 27));

    if (m_image.isNull()) {
        painter.setPen(QColor(130, 130, 142));
        painter.setFont(uiFont(kFontSmall));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待捕获"));
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

    setObjectName(QStringLiteral("MainPanel"));
    setWindowTitle(QStringLiteral("ChromaColorPicker"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(QStringLiteral(
        "QWidget#MainPanel { background-color: #151517; color: #ececf1; }"
        "QFrame#DataPanel { background-color: #111114; border: 1px solid #26262e;"
        "                  border-radius: 5px; }"
        "QLabel#PanelTitle { color: #c7c7d1; }"
        "QLabel#KvKey { color: #8d8d9a; }"
        "QLabel#KvValue { color: #f2f2f7; }"
        "QLabel#ReadoutKey { color: #8c8c99; }"
        "QComboBox { background-color: #101015; border: 1px solid #30303a;"
        "             border-radius: 4px; color: #dcdce4; padding: 2px 5px; }"
        "QComboBox:disabled { color: #55555f; background-color: #111114;"
        "                     border-color: #25252c; }"
        "QComboBox QAbstractItemView { background: #16161c; color: #dcdce4;"
        "                             selection-background-color: #2a2a36; }"
        "QFrame#KvSeparator { border: 0; background-color: #22222a; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(9, 10, 9, 10);
    root->setSpacing(8);

    // 第 1 个 panel：放大镜 + 色块 + 四行读数
    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(8);

    m_magnifier = new MagnifierWidget(this);
    m_magnifier->setAttribute(Qt::WA_TransparentForMouseEvents);
    top->addWidget(m_magnifier, 0, Qt::AlignTop);

    auto *right = new QVBoxLayout();
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(6);

    if (m_swatch && m_swatch->widget()) {
        QWidget *swatch = m_swatch->widget();
        swatch->setFixedSize(30, 30);
        right->addWidget(swatch, 0, Qt::AlignLeft);
    }

    auto *readout = new QGridLayout();
    readout->setContentsMargins(0, 0, 0, 0);
    readout->setHorizontalSpacing(2);
    readout->setVerticalSpacing(2);

    const auto addReadoutRow = [&](int row, const QString &name, QLabel *&value) {
        auto *key = new QLabel(name, this);
        key->setObjectName(QStringLiteral("ReadoutKey"));
        key->setFont(uiFont(kFontSmall));
        key->setFixedWidth(27);
        value = makeReadoutValue(this);
        readout->addWidget(key, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        readout->addWidget(value, row, 1, Qt::AlignLeft | Qt::AlignVCenter);
    };

    addReadoutRow(0, QStringLiteral("色域"), m_gamutLabel);
    addReadoutRow(1, QStringLiteral("R"), m_rLabel);
    addReadoutRow(2, QStringLiteral("G"), m_gLabel);
    addReadoutRow(3, QStringLiteral("B"), m_bLabel);

    right->addLayout(readout);
    top->addLayout(right, 1);
    root->addLayout(top);

    // HDR 配置模式（仅 macOS 可用；Windows 显示为灰色不可交互）
    auto *modeRow = new QHBoxLayout();
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(5);

    auto *modeLabel = new QLabel(QStringLiteral("模式"), this);
    modeLabel->setObjectName(QStringLiteral("ReadoutKey"));
    modeLabel->setFont(uiFont(kFontSmall));
    modeLabel->setFixedWidth(27);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setFont(uiFont(kFontSmall));
    m_modeCombo->addItem(QStringLiteral("Apple HDR preset - EL P3"));
    m_modeCombo->addItem(QStringLiteral("Apple HDR preset - EDR linear"));
    m_modeCombo->addItem(QStringLiteral("raw yuv to linear p3"));
    m_modeCombo->addItem(QStringLiteral("自定义 format"));
    const bool supportsModes = m_capture && m_capture->supportsHdrModes();
    m_modeCombo->setEnabled(supportsModes);
    m_modeCombo->setToolTip(supportsModes
        ? QStringLiteral("切换 HDR 捕获配置")
        : QStringLiteral("仅 macOS 支持 HDR 配置切换"));
    if (supportsModes) {
        switch (m_capture->hdrMode()) {
        case HdrCaptureMode::ApplePresetEdrLinear:
            m_modeCombo->setCurrentIndex(1);
            break;
        case HdrCaptureMode::RawYuvToLinearP3:
            m_modeCombo->setCurrentIndex(2);
            break;
        case HdrCaptureMode::CustomFormat:
            m_modeCombo->setCurrentIndex(3);
            break;
        case HdrCaptureMode::ApplePreset:
        default:
            m_modeCombo->setCurrentIndex(0);
            break;
        }
    }

    modeRow->addWidget(modeLabel);
    modeRow->addWidget(m_modeCombo, 1);
    root->addLayout(modeRow);


    // 第 2 / 3 个 panel：键一行、值下一行
    const auto makeDataPanel = [&](const QString &title,
                                   const QStringList &keys,
                                   QVector<QLabel *> &values) {
        auto *panel = new QFrame(this);
        panel->setObjectName(QStringLiteral("DataPanel"));

        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(0);

        auto *titleLabel = new QLabel(title, panel);
        titleLabel->setObjectName(QStringLiteral("PanelTitle"));
        titleLabel->setFont(uiFont(kFontTitle, true));
        layout->addWidget(titleLabel);
        layout->addSpacing(5);

        for (int i = 0; i < keys.size(); ++i) {
            if (i > 0) {
                auto *separator = new QFrame(panel);
                separator->setObjectName(QStringLiteral("KvSeparator"));
                separator->setFixedHeight(1);
                layout->addWidget(separator);
            }

            auto *item = new QWidget(panel);
            auto *itemLayout = new QVBoxLayout(item);
            itemLayout->setContentsMargins(0, 4, 0, 4);
            itemLayout->setSpacing(1);

            auto *keyLabel = new QLabel(keys[i], item);
            keyLabel->setObjectName(QStringLiteral("KvKey"));
            keyLabel->setFont(uiFont(kFontSmall, true));
            keyLabel->setWordWrap(false);
            keyLabel->setMinimumWidth(0);
            keyLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            keyLabel->setToolTip(keys[i]);

            auto *valueLabel = new QLabel(QStringLiteral("—"), item);
            valueLabel->setObjectName(QStringLiteral("KvValue"));
            valueLabel->setFont(uiFont(kFontSmall, false));
            valueLabel->setWordWrap(true);
            valueLabel->setTextFormat(Qt::PlainText);
            valueLabel->setContentsMargins(10, 0, 0, 0);
            valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

            itemLayout->addWidget(keyLabel);
            itemLayout->addWidget(valueLabel);
            layout->addWidget(item);
            values.push_back(valueLabel);
        }

        return panel;
    };

    root->addWidget(makeDataPanel(
        QStringLiteral("合成空间"),
        {QStringLiteral("合成空间"),
         QStringLiteral("捕获输出空间"),
         QStringLiteral("捕获格式"),
         QStringLiteral("SDR 内容亮度滑杆值"),
         QStringLiteral("SDR 白点"),
         QStringLiteral("1.0 对应实际 nits")},
        m_compositionValues));

    root->addWidget(makeDataPanel(
        QStringLiteral("显示输出"),
        {QStringLiteral("wire format"),
         QStringLiteral("显示器 ICC"),
         QStringLiteral("EDR headroom"),
         QStringLiteral("最大亮度"),
         QStringLiteral("位数"),
         QStringLiteral("HDR / Advanced Color"),
         QStringLiteral("实际 scanout 空间")},
        m_displayValues));

    root->addStretch(1);

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::ApplicationShortcut);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) {
                    return;
                }
                if (!m_captureReady && !m_monitor.id.empty()) {
                    initCapture(m_monitor);
                }
                if (m_capture) {
                    m_capture->refreshDisplayState();
                }
                if (m_hasLastSample) {
                    updateInfoPanels(m_lastSample);
                }
                if (m_swatch) {
                    m_swatch->refreshSurface();
                }
            });

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (!m_capture || !m_capture->supportsHdrModes()) {
                    return;
                }
                HdrCaptureMode mode = HdrCaptureMode::ApplePreset;
                if (index == 1) {
                    mode = HdrCaptureMode::ApplePresetEdrLinear;
                }
                else if (index == 2) {
                    mode = HdrCaptureMode::RawYuvToLinearP3;
                }
                else if (index == 3) {
                    mode = HdrCaptureMode::CustomFormat;
                }
                if (m_capture->hdrMode() == mode) {
                    return;
                }
                m_capture->setHdrMode(mode);
                m_captureReady = false;
                m_hasLastSample = false;
                if (!m_monitor.id.empty()) {
                    initCapture(m_monitor);
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

    placeOnScreenRight();
    tick();
}

void MainWindow::placeOnScreenRight()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return;
    }

    // 不覆盖 macOS 菜单栏；使用 availableGeometry。
    const QRect available = screen->availableGeometry();
    const int panelWidth = std::max(100, static_cast<int>(std::lround(
        static_cast<double>(screen->geometry().width()) * 200.0 / 1920.0)));
    setFixedSize(panelWidth, available.height());
    move(available.right() - width() + 1, available.top());
}

void MainWindow::initCapture(const MonitorInfo &monitor)
{
    m_monitor = monitor;
    m_captureReady = false;
    m_hasLastSample = false;
    m_monitorRect = monitor.bounds;
    m_monitorScale = monitor.scale > 0.0 ? monitor.scale : 1.0;
    if (monitor.id.empty()) {
        return;
    }

    m_captureReady = (m_capture != nullptr) &&
                     m_capture->initialize(monitor, static_cast<NativeWindow>(winId()));
    if (!m_captureReady) {
        return;
    }
    m_captureRetryTicks = 0;
    updateInfoPanels(ColorSample{});
}

void MainWindow::refreshMetadata()
{
    if (!m_captureReady || !m_capture) {
        return;
    }
    m_capture->refreshDisplayState();
    if (m_hasLastSample) {
        updateInfoPanels(m_lastSample);
    }
}

void MainWindow::tick()
{
    if (!m_probe || !m_capture) {
        return;
    }

    const PointI cursor = m_probe->cursorPosition();
    const MonitorInfo monitor = m_probe->monitorAt(cursor);
    if (monitor.id.empty()) {
        return;
    }

    if (!m_captureReady) {
        // 骨架/权限失败时不要每 50ms 重试并刷屏；约 5 秒重试一次。
        if (m_captureRetryTicks <= 0) {
            initCapture(monitor);
            m_captureRetryTicks = 100;
        } else {
            --m_captureRetryTicks;
        }
    }
    else if (!sameMonitor(monitor, m_monitor) &&
             m_capture->backend() != CaptureBackendKind::Wgc) {
        initCapture(monitor);
    }

    if (!m_captureReady || !sameMonitor(monitor, m_monitor)) {
        return;
    }

    m_capture->captureFrame();

    // 面板固定在显示器右侧，采样点仍是鼠标下的屏幕像素。
    const int localX = static_cast<int>(std::lround(
        static_cast<double>(cursor.x - m_monitorRect.left) * m_monitorScale));
    const int localY = static_cast<int>(std::lround(
        static_cast<double>(cursor.y - m_monitorRect.top) * m_monitorScale));
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
    const bool isLinearFloat = sample.format.encoding == PixelEncoding::Fp16 ||
                               sample.format.encoding == PixelEncoding::Rgba16Float ||
                               sample.format.encoding == PixelEncoding::YCbCr10;
    const float r = sample.rgba[0];
    const float g = sample.rgba[1];
    const float b = sample.rgba[2];

    if (isLinearFloat) {
        m_rLabel->setText(QString::number(r, 'f', 4));
        m_gLabel->setText(QString::number(g, 'f', 4));
        m_bLabel->setText(QString::number(b, 'f', 4));
    }
    else {
        m_rLabel->setText(QString::number(std::lround(r * 255.0f)));
        m_gLabel->setText(QString::number(std::lround(g * 255.0f)));
        m_bLabel->setText(QString::number(std::lround(b * 255.0f)));
    }

    if (m_capture && m_capture->backend() == CaptureBackendKind::ScreenCaptureKit) {
        const QString displaySpace = QString::fromStdString(m_capture->display().colorSpace);
        if (m_capture->hdrMode() == HdrCaptureMode::ApplePresetEdrLinear &&
            sample.format.encoding == PixelEncoding::YCbCr10) {
            m_gamutLabel->setText(QStringLiteral("EDR Linear"));
        }
        else if (m_capture->hdrMode() == HdrCaptureMode::RawYuvToLinearP3 &&
                 sample.format.encoding == PixelEncoding::YCbCr10) {
            m_gamutLabel->setText(QStringLiteral("P3 Linear"));
        }
        else if (sample.format.encoding == PixelEncoding::Rgba16Float ||
                 sample.format.encoding == PixelEncoding::YCbCr10) {
            m_gamutLabel->setText(QStringLiteral("P3 Linear"));
        }
        else if (displaySpace.contains(QStringLiteral("P3"), Qt::CaseInsensitive)) {
            m_gamutLabel->setText(QStringLiteral("P3"));
        }
        else {
            m_gamutLabel->setText(QStringLiteral("sRGB"));
        }
    }
    else {
        m_gamutLabel->setText(colorSpaceText(sample.format));
    }
    if (m_swatch) {
        m_swatch->setColor(r, g, b, isLinearFloat);
    }
    updateMagnifier(magSample);
    updateInfoPanels(sample);
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

void MainWindow::updateInfoPanels(const ColorSample &sample)
{
    if (!m_capture) {
        return;
    }

    const DisplayInfo &display = m_capture->display();
    const CaptureFormat format = sample.ok ? sample.format : m_capture->format();
    const bool isFp16 = format.encoding == PixelEncoding::Fp16;
    const bool isScreenCaptureKit =
        m_capture->backend() == CaptureBackendKind::ScreenCaptureKit;
    const double oneNits =
        (display.hdr && isFp16) ? 80.0 : display.sdrWhiteNits.value_or(80.0);
    const QString headroom = QStringLiteral("%1× / %2×")
        .arg(display.edrHeadroomCurrent, 0, 'f', 2)
        .arg(display.edrHeadroomPotential, 0, 'f', 2);
    const QString wireFormat = display.wireFormat.empty()
        ? QString::fromStdString(display.colorSpace)
        : QString::fromStdString(display.wireFormat);

    if (isScreenCaptureKit) {
        // macOS 没有公开的 WindowServer 合成空间/scanout 空间；
        // 这里只显示能够从 SCK / CGDisplay 拿到的信息。
        setPanelValue(m_compositionValues, 0, QStringLiteral("不公开（Apple 无 API）"));
        setPanelValue(m_compositionValues, 1, QString::fromStdString(display.colorSpace));
        QString captureInfo = formatText(format);
        if (!format.pixelFormat.empty()) {
            captureInfo = QString::fromStdString(format.pixelFormat) + QLatin1Char('\n') +
                          QString::fromStdString(format.colorSpaceName) + QLatin1Char('\n') +
                          QString::fromStdString(format.colorMatrix);
        }
        setPanelValue(m_compositionValues, 2, captureInfo);
        setPanelValue(m_compositionValues, 3, QStringLiteral("n/a"));
        setPanelValue(m_compositionValues, 4, QStringLiteral("n/a"));
        setPanelValue(m_compositionValues, 5, QStringLiteral("n/a（按捕获格式解释）"));

        setPanelValue(m_displayValues, 0, QStringLiteral("n/a（无等价公开字段）"));
        QString profileText = display.displayIcc.empty()
            ? QString::fromStdString(display.colorSpace)
            : QString::fromStdString(display.displayIcc);
        if (!display.displayGamut.empty()) {
            profileText += QStringLiteral("\n色域：") +
                           QString::fromStdString(display.displayGamut);
        }
        if (!display.displayTransfer.empty()) {
            profileText += QStringLiteral("\n传递函数：") +
                           QString::fromStdString(display.displayTransfer);
        }
        if (!display.displayWhitePoint.empty()) {
            profileText += QStringLiteral("\n白点：") +
                           QString::fromStdString(display.displayWhitePoint);
        }
        if (!display.displayMatrix.empty()) {
            profileText += QStringLiteral("\n矩阵（RGB→XYZ）：\n") +
                           QString::fromStdString(display.displayMatrix);
        }
        setPanelValue(m_displayValues, 1, profileText);
        setPanelValue(m_displayValues, 2, display.edrHeadroomPotential > 1.0
            ? headroom
            : QStringLiteral("n/a"));
        setPanelValue(m_displayValues, 3, QStringLiteral("n/a（用 EDR headroom）"));
        setPanelValue(m_displayValues, 4, QString::number(display.bitsPerColor));
        setPanelValue(m_displayValues, 5, display.hdr
            ? QStringLiteral("已启用（EDR）")
            : QStringLiteral("未启用（SDR）"));
        setPanelValue(m_displayValues, 6, QStringLiteral("不公开"));
        return;
    }

    setPanelValue(m_compositionValues, 0, colorSpaceText(format));
    setPanelValue(m_compositionValues, 1, QString::fromStdString(display.colorSpace));
    setPanelValue(m_compositionValues, 2, formatText(format));
    setPanelValue(m_compositionValues, 3, optionalUInt(display.sdrWhiteLevelRaw));
    setPanelValue(m_compositionValues, 4, optionalNumber(display.sdrWhiteNits, 1) + QStringLiteral(" nits"));
    setPanelValue(m_compositionValues, 5, QString::number(oneNits, 'f', 1) + QStringLiteral(" nits"));

    setPanelValue(m_displayValues, 0, wireFormat);
    setPanelValue(m_displayValues, 1, QString::fromStdString(display.colorSpace));
    setPanelValue(m_displayValues, 2, QStringLiteral("n/a"));
    setPanelValue(m_displayValues, 3, optionalNumber(display.maxLuminance, 1) + QStringLiteral(" nits"));
    setPanelValue(m_displayValues, 4, display.bitsPerColor == 0
        ? QStringLiteral("n/a")
        : QString::number(display.bitsPerColor));
    setPanelValue(m_displayValues, 5, display.advancedColorEnabled
        ? QStringLiteral("已启用")
        : QStringLiteral("未启用"));
    setPanelValue(m_displayValues, 6, wireFormat);
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::ScreenChangeInternal) {
        placeOnScreenRight();
        if (m_swatch) {
            m_swatch->refreshSurface();
        }
    }
    return QWidget::event(event);
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
