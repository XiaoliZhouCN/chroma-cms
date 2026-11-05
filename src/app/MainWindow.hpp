#pragma once

#include <QMainWindow>
#include <QString>

namespace chroma::app {

class ViewportWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    ViewportWidget* m_viewport = nullptr;
    QString m_backend = QStringLiteral("初始化中…");
};

} // namespace chroma::app