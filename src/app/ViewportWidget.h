#pragma once

#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>

namespace chroma::media {
struct ImageData;
}

namespace chroma::render {
class D3D12Renderer;
}

namespace chroma::app {

/// 原生表面宿主。
///
/// 职责：提供一个稳定的 HWND 与正确的**物理像素**尺寸，交给渲染后端创建交换链。
/// M0：QPainter 占位。M1 起由 D3D12 接管，本类不再绘制像素。
///
/// 关键属性（原因见 Docs/adr/0002-qt-widgets-native-surface.md）：
///   WA_NativeWindow      保证有独立 HWND
///   WA_OpaquePaintEvent  禁止 Qt 擦背景
///   WA_NoSystemBackground 同上，避免与交换链争抢后台缓冲
class ViewportWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// 只持有共享指针；若渲染器尚未就绪，会在初始化后补上。
    void setImage(std::shared_ptr<const media::ImageData> image);

signals:
    void rendererReady(const QString& backend);
    void rendererFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void ensureRenderer();
    std::uint32_t physicalWidth() const;
    std::uint32_t physicalHeight() const;

    std::unique_ptr<render::D3D12Renderer> m_renderer;
    std::shared_ptr<const media::ImageData> m_image;
    bool m_initAttempted = false;
};

} // namespace chroma::app
