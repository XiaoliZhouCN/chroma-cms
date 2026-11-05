#include "app/MainWindow.hpp"

#include "app/ViewportWidget.hpp"
#include "core/Version.hpp"

#include <QLabel>
#include <QStatusBar>

#include <string>

namespace chroma::app {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ChromaCMS %1 — practice")
                       .arg(QString::fromStdString(std::string(kVersion))));

    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

     auto* status = new QLabel(this);
    status->setText(QStringLiteral("后端：%1").arg(m_backend));
    statusBar()->addPermanentWidget(status);
    statusBar()->showMessage(QStringLiteral("第 2 步：D3D12 清屏"));

    connect(m_viewport, &ViewportWidget::rendererReady, this,
            [this, status](const QString& backend) {
                m_backend = backend;
                status->setText(QStringLiteral("后端：%1").arg(m_backend));
            });

    connect(m_viewport, &ViewportWidget::rendererFailed, this,
            [status](const QString& message) {
                status->setText(QStringLiteral("后端：失败 — %1").arg(message));
            });

    resize(1280, 800);
}

} // namespace chroma::app