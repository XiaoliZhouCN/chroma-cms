#pragma once

#include "Capture.hpp"

#include <QImage>
#include <QPoint>
#include <QVector>
#include <QWidget>

#include <memory>

class QLabel;
class QTimer;

namespace cp
{
class HdrSwatch;

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

private:
    void tick();
    void initCapture(HMONITOR monitor);
    void refreshMetadata();
    bool event(QEvent *event) override;
    void updateReadout(const ScreenCapture::Sample &sample,
                       const ScreenCapture::Sample &magSample, POINT cursor);
    void updateMagnifier(const ScreenCapture::Sample &sample);
    void updateInfoLines(const ScreenCapture::Sample &sample);

    ScreenCapture m_capture;
    bool m_captureReady = false;
    bool m_hasLastSample = false;
    ScreenCapture::Sample m_lastSample;
    ScreenCapture::Sample m_lastMagnifierSample;
    HMONITOR m_monitor = nullptr;
    RECT m_monitorRect = {};
    int m_tickCounter = 0;

    QTimer *m_timer = nullptr;
    QTimer *m_infoTimer = nullptr;
    MagnifierWidget *m_magnifier = nullptr;
    HdrSwatch *m_swatch = nullptr;
    QLabel *m_cccsLabel = nullptr;
    QLabel *m_rLabel = nullptr;
    QLabel *m_gLabel = nullptr;
    QLabel *m_bLabel = nullptr;
    QVector<QLabel *> m_infoLabels;

    bool m_dragging = false;
    bool m_paused = false;
    QPoint m_dragOffset;

    QString m_cccsText;
};
} // namespace cp
