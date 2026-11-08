#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>

class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

namespace chroma::media { struct Image; }

namespace chroma::render {
class IRenderer;
}

namespace chroma::app {

/// 原生表面宿主：提供一个稳定的 HWND 与正确的物理像素尺寸。
///
/// 三个属性是必需的：
///   WA_NativeWindow       保证有独立 HWND
///   WA_OpaquePaintEvent   禁止 Qt 擦背景
///   WA_NoSystemBackground 同上，避免和交换链抢后台缓冲
class ViewportWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;
    explicit ViewportWidget(std::shared_ptr<const media::Image> image,
                            QWidget* parent = nullptr);

signals:
    void rendererReady(const QString& backend);
    void rendererFailed(const QString& message);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void ensureRenderer();
    void applyViewTransform();
    std::uint32_t physicalWidth() const;
    std::uint32_t physicalHeight() const;

    std::unique_ptr<render::IRenderer> m_renderer;
    bool m_initAttempted = false;

    int m_quarterTurns = 0;
    float m_zoom = 1.0f;

    std::shared_ptr<const media::Image> m_image;
};

} // namespace chroma::app