#pragma once

#include "Capture.hpp"

#include <QImage>
#include <QWidget>

#include <memory>

class QLabel;
class QTimer;

namespace cp
{
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

class SwatchWidget : public QWidget
{
public:
    explicit SwatchWidget(QWidget *parent = nullptr);
    void setColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color = Qt::black;
};

/// 简化版"屏幕测色计"：无热键，跟随鼠标连续读取光标下 1×1 像素。
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void tick();
    void initCapture(HMONITOR monitor);
    void updateReadout(const ScreenCapture::Sample &sample,
                       const ScreenCapture::Sample &magSample, POINT cursor);
    void updateDisplayInfo();
    void updateMagnifier(const ScreenCapture::Sample &sample);

    ScreenCapture m_capture;
    bool m_captureReady = false;
    HMONITOR m_monitor = nullptr;
    RECT m_monitorRect = {};
    int m_tickCounter = 0;

    QTimer *m_timer = nullptr;
    MagnifierWidget *m_magnifier = nullptr;
    SwatchWidget *m_swatch = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_warningLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_rLabel = nullptr;
    QLabel *m_gLabel = nullptr;
    QLabel *m_bLabel = nullptr;
};
} // namespace cp
