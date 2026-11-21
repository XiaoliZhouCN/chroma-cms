#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

#include "probe/ProbeConfig.hpp"

class QEvent;
class QResizeEvent;
class QWidget;

namespace chroma::media { struct Image; }

namespace chroma::app {

class CSMViewerWidget;
class ViewportWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    MainWindow(std::shared_ptr<const media::Image> image, QString imagePath,
               QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class DisplayMode
    {
        SkiaOnly = 0,
        NativeOnly = 1,
        Both = 2,
    };

    void buildLayout(std::shared_ptr<const media::Image> image, QString imagePath);
    void updateWindowGeometry();
    void layoutViewports();
    void applyDisplayMode();
    void toggleDisplayMode();
    void beginTabOverride();
    void endTabOverride();
    void applyViewTransforms();
    bool handleTransformKey(int key);
    void updateInfo();
    void updateHover(QObject* watched);
    void initProbe();
    void onFramePresented(CSMViewerWidget* viewer);
    void runProbe(CSMViewerWidget* viewer);

    ViewportWidget* viewportUnderMouse() const;
    ViewportWidget* activeViewport() const;

    QWidget* m_central = nullptr;
    ViewportWidget* m_native = nullptr;
    ViewportWidget* m_skia = nullptr;
    ViewportWidget* m_hoverViewport = nullptr;

    std::shared_ptr<const media::Image> m_image;
    QString m_imagePath;
    DisplayMode m_mode = DisplayMode::SkiaOnly;

    // 两个 view / 两个 API 共用同一套旋转缩放状态。
    int m_quarterTurns = 0;
    float m_zoom = 1.0f;

    std::unique_ptr<chroma::probe::ProbeConfig> m_probeConfig;
    QString m_probeOutputPath;
    bool m_probePending = false;

    bool m_overrideActive = false;
    ViewportWidget* m_overrideViewport = nullptr;
    int m_overrideOriginalBackend = 0;
};

} // namespace chroma::app
