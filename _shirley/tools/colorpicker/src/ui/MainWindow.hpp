#pragma once

#include "core/CaptureSession.hpp"
#include "core/DisplayProbe.hpp"
#include "core/Types.hpp"
#include "ui/HdrPreview.hpp"

#include <QImage>
#include <QVector>
#include <QWidget>

#include <memory>

class QComboBox;
class QLabel;

namespace cp
{
class HdrPreview;

class MagnifierWidget : public QWidget
{
public:
    explicit MagnifierWidget(QWidget *parent = nullptr);
    void setImage(const QImage &image);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
};

/// 无边框、置顶、右侧全高取色器面板。
///
/// 布局：
///   放大镜 80×80 + 色块 30×30 + 色域/RGB 四行
///   合成空间键值对 panel
///   显示输出键值对 panel
///
/// 本类只依赖 core 接口，不包含任何平台头文件。
class MainWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    void placeOnScreenRight();
    void tick();
    void initCapture(const MonitorInfo &monitor);
    void refreshMetadata();
    void updateReadout(const ColorSample &sample, const ColorSample &magSample);
    void updateMagnifier(const ColorSample &sample);
    void updateInfoPanels(const ColorSample &sample);

    std::unique_ptr<IDisplayProbe> m_probe;
    std::unique_ptr<ICaptureSession> m_capture;
    std::unique_ptr<HdrPreview> m_swatch;

    bool m_captureReady = false;
    int m_captureRetryTicks = 0;
    bool m_hasLastSample = false;
    ColorSample m_lastSample;
    ColorSample m_lastMagnifierSample;
    MonitorInfo m_monitor;
    RectI m_monitorRect = {};
    double m_monitorScale = 1.0;

    class QTimer *m_timer = nullptr;
    class QTimer *m_infoTimer = nullptr;
    MagnifierWidget *m_magnifier = nullptr;
    QComboBox *m_modeCombo = nullptr;

    QLabel *m_gamutLabel = nullptr;
    QLabel *m_rLabel = nullptr;
    QLabel *m_gLabel = nullptr;
    QLabel *m_bLabel = nullptr;

    QVector<QLabel *> m_compositionValues;
    QVector<QLabel *> m_displayValues;
};
} // namespace cp
