#include "app/MainWindow.h"

#include "app/ViewportWidget.h"
#include "core/Version.h"

#include <QLabel>
#include <QStatusBar>

#include <string>

namespace chroma::app {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    const QString title = QStringLiteral("%1 %2 — M0")
                              .arg(QString::fromStdString(std::string(kAppName)))
                              .arg(QString::fromStdString(std::string(kVersion)));
    setWindowTitle(title);

    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    buildStatusBar();
    resize(1280, 720);
}

void MainWindow::buildStatusBar()
{
    m_status = new QLabel(this);
    m_status->setText(QStringLiteral("后端：D3D12（未接入）　|　色彩管理：未接入"));
    statusBar()->addPermanentWidget(m_status);
    statusBar()->showMessage(QStringLiteral("M0：原生表面宿主就绪"));
}

} // namespace chroma::app
