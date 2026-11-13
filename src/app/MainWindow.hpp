#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

namespace chroma::media { struct Image; }

class QLabel;
class QTabBar;
class QWidget;

namespace chroma::app {

class ViewportWidget;

/// 主窗口：双视图对照（左 = 手写原生后端，右 = Skia 后端）。
/// Tab：左 / 右 / 并排；"叠图"需要离屏合成，P1 再实现（当前占位禁用）。
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    MainWindow(std::shared_ptr<const media::Image> image, QString imagePath,
               QWidget* parent = nullptr);

private:
    void buildLayout(std::shared_ptr<const media::Image> image, const QString& imagePath);
    void applyTab(int index);
    void updateStatus();

    ViewportWidget* m_left = nullptr;
    ViewportWidget* m_right = nullptr;
    QWidget* m_leftPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QTabBar* m_tabs = nullptr;
    QLabel* m_status = nullptr;

    QString m_leftBackend = QStringLiteral("初始化中…");
    QString m_rightBackend = QStringLiteral("初始化中…");
};

} // namespace chroma::app
