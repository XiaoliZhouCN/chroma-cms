#pragma once

#include <QWidget>

namespace chroma::app {

/// 原生表面宿主。
///
/// 职责：提供一个稳定的 HWND 与正确的物理像素尺寸，交给渲染后端创建交换链。
/// M0：仅占位绘制（QPainter）。M1 起由 IRenderer 接管，本类不再绘制像素。
///
/// 关键属性（原因见 Docs/adr/0002-qt-widgets-native-surface.md）：
///   WA_NativeWindow      保证有独立 HWND
///   WA_OpaquePaintEvent  禁止 Qt 擦背景
///   WA_NoSystemBackground 同上，避免与交换链争抢后台缓冲
class ViewportWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);

    /// DXGI 创建交换链所需的窗口句柄（QWidget 的 WId 即 HWND）。
    WId nativeHandle();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    bool m_handleLogged = false;
};

} // namespace chroma::app
