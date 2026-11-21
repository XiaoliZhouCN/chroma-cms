#pragma once

#include "render/RendererFactory.hpp"

#include <QWidget>

#include <memory>

class QResizeEvent;

namespace chroma::media { struct Image; }

namespace chroma::app {

class AssistInfoWidget;
class CSMViewerWidget;

/// 一个对照 view：
///   CSMViewerWidget(Native) + CSMViewerWidget(Skia) + AssistInfoWidget
///
/// 两个 CSMViewerWidget 在程序生命周期内同时存在，
/// 切换只做 raise/lower，不销毁 renderer。
class ViewportWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ViewportWidget(render::BackendKind defaultBackend,
                   std::shared_ptr<const media::Image> image,
                   QWidget* parent = nullptr);

    void setImage(std::shared_ptr<const media::Image> image);
    void showBackend(render::BackendKind kind);
    void setApiTransform(render::BackendKind kind, int quarterTurns, float zoom);
    render::BackendKind backendKind() const { return m_backend; }

    void setInfoText(const QString& text);
    CSMViewerWidget* viewer() const;
    CSMViewerWidget* nativeViewer() const { return m_nativeViewer; }
    CSMViewerWidget* skiaViewer() const { return m_skiaViewer; }
    AssistInfoWidget* assistInfo() const { return m_info; }

signals:
    void rendererReady(const QString& backend);
    void rendererFailed(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void raiseCurrent();

    CSMViewerWidget* m_nativeViewer = nullptr;
    CSMViewerWidget* m_skiaViewer = nullptr;
    AssistInfoWidget* m_info = nullptr;
    render::BackendKind m_backend = render::BackendKind::Skia;
};

} // namespace chroma::app
