#pragma once

#include <QString>
#include <QWidget>

#include "render/IRendererReadback.hpp"
#include "render/RendererFactory.hpp"

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

/// 单个 CSM 查看器：原生表面 + renderer + 图像 + 视图变换。：提供一个稳定的 native window 与正确的物理像素尺寸。
///
/// 三个属性是必需的：
///   WA_NativeWindow       保证有独立 native window
///   WA_OpaquePaintEvent   禁止 Qt 擦背景
///   WA_NoSystemBackground 同上，避免和交换链抢后台缓冲
class CSMViewerWidget final : public QWidget {
    Q_OBJECT

public:
    explicit CSMViewerWidget(QWidget* parent = nullptr);
    explicit CSMViewerWidget(std::shared_ptr<const media::Image> image,
                                 QWidget* parent = nullptr);
    CSMViewerWidget(render::BackendKind kind, std::shared_ptr<const media::Image> image,
                    QWidget* parent = nullptr);
    ~CSMViewerWidget() override;

    void setImage(std::shared_ptr<const media::Image> image);
    std::shared_ptr<const media::Image> image() const { return m_image; }
    void setViewTransform(int quarterTurns, float zoom);
    int quarterTurns() const { return m_quarterTurns; }
    float zoom() const { return m_zoom; }

    /// 将源图坐标映射到当前 renderer surface 的物理像素坐标。
    QPointF imagePointToSurface(const QPointF& imagePoint) const;
    bool readPixels(const std::vector<render::PixelPoint>& points,
                    std::vector<render::RgbaF32>& out);
    void setBackend(render::BackendKind kind);
    render::BackendKind backendKind() const { return m_backendKind; }

signals:
    void rendererReady(const QString& backend);
    void rendererFailed(const QString& message);
    void framePresented();

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

    render::BackendKind m_backendKind = render::BackendKind::Native;
    std::unique_ptr<render::IRenderer> m_renderer;
    bool m_initAttempted = false;

    int m_quarterTurns = 0;
    float m_zoom = 1.0f;

    std::shared_ptr<const media::Image> m_image;
};

} // namespace chroma::app
