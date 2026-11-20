#pragma once

#include "core/CaptureSession.hpp"
#include "core/DisplayProbe.hpp"
#include "core/Types.hpp"
#include "ui/HdrPreview.hpp"

#include <QImage>
#include <QPoint>
#include <QVector>
#include <QWidget>

#include <memory>

class QLabel;
class QTimer;

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

/// 简化版屏幕测色计：无边框、置顶、可拖动，Esc 退出。
///
/// 本类只依赖 core 接口，不包含任何平台头文件。
class MainWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    void tick();
    void initCapture(const MonitorInfo &monitor);
    void refreshMetadata();
    void updateReadout(const ColorSample &sample, const ColorSample &magSample);
    void updateMagnifier(const ColorSample &sample);
    void updateInfoLines(const ColorSample &sample);

    std::unique_ptr<IDisplayProbe> m_probe;
    std::unique_ptr<ICaptureSession> m_capture;
    std::unique_ptr<HdrPreview> m_swatch;

    bool m_captureReady = false;
    bool m_hasLastSample = false;
    ColorSample m_lastSample;
    ColorSample m_lastMagnifierSample;
    MonitorInfo m_monitor;
    RectI m_monitorRect = {};
    bool m_paused = false;

    QTimer *m_timer = nullptr;
    QTimer *m_infoTimer = nullptr;
    MagnifierWidget *m_magnifier = nullptr;
    QLabel *m_cccsLabel = nullptr;
    QLabel *m_rLabel = nullptr;
    QLabel *m_gLabel = nullptr;
    QLabel *m_bLabel = nullptr;
    QVector<QLabel *> m_infoLabels;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
} // namespace cp
