#pragma once

#include <QMainWindow>

class QLabel;

namespace chroma::app {

class ViewportWidget;

/// M0 的应用外壳：一个窗口 + 一个原生表面宿主 + 状态栏。
/// 渲染在 M1 接管视口，本类不直接接触 D3D12。
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildStatusBar();

    ViewportWidget* m_viewport = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace chroma::app
