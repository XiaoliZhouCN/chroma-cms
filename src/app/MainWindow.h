#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

namespace chroma::media {
struct ImageData;
}

class QLabel;

namespace chroma::app {

class ViewportWidget;

/// 应用外壳：一个窗口 + 一个原生表面宿主 + 状态栏（显示图像与后端信息）。
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(std::shared_ptr<const media::ImageData> image, QString imagePath,
               QWidget* parent = nullptr);

private:
    void buildStatusBar();
    void refreshStatus(const QString& backend);

    ViewportWidget* m_viewport = nullptr;
    QLabel* m_status = nullptr;
    std::shared_ptr<const media::ImageData> m_image;
    QString m_imagePath;
    QString m_backend = QStringLiteral("初始化中…");
};

} // namespace chroma::app
