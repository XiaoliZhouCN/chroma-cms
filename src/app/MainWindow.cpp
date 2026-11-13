#include "app/MainWindow.hpp"

#include "app/ViewportWidget.hpp"
#include "core/Version.hpp"

#include "media/ImageLoader.hpp"
#include "render/RendererFactory.hpp"

#include <utility>

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QTabBar>
#include <QVBoxLayout>

#include <string>

namespace chroma::app {

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(nullptr, QString(), parent)
{
}

MainWindow::MainWindow(std::shared_ptr<const media::Image> image, QString imagePath,
                       QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ChromaCMS %1 — practice")
                       .arg(QString::fromStdString(std::string(kVersion))));

    buildLayout(std::move(image), imagePath);
    resize(1600, 900);
}

void MainWindow::buildLayout(std::shared_ptr<const media::Image> image, const QString& imagePath)
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    m_tabs = new QTabBar(central);
    m_tabs->addTab(QStringLiteral("左"));
    m_tabs->addTab(QStringLiteral("右"));
    m_tabs->addTab(QStringLiteral("并排"));
    m_tabs->addTab(QStringLiteral("叠图"));
    m_tabs->setTabEnabled(3, false);
    m_tabs->setTabToolTip(3, QStringLiteral("P1 实现：需要离屏合成/差值渲染"));
    rootLayout->addWidget(m_tabs);

    auto* panels = new QWidget(central);
    auto* panelLayout = new QHBoxLayout(panels);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(4);

    auto* leftGroup = new QGroupBox(QStringLiteral("左侧：手写原生 API"), panels);
    auto* leftLayout = new QVBoxLayout(leftGroup);
    m_left = new ViewportWidget(render::BackendKind::Native, image, leftGroup);
    leftLayout->addWidget(m_left);

    auto* rightGroup = new QGroupBox(QStringLiteral("右侧：Skia 后端"), panels);
    auto* rightLayout = new QVBoxLayout(rightGroup);
    m_right = new ViewportWidget(render::BackendKind::Skia, image, rightGroup);
    rightLayout->addWidget(m_right);

    m_leftPanel = leftGroup;
    m_rightPanel = rightGroup;
    panelLayout->addWidget(leftGroup, 1);
    panelLayout->addWidget(rightGroup, 1);
    rootLayout->addWidget(panels, 1);

    setCentralWidget(central);

    m_status = new QLabel(this);
    statusBar()->addPermanentWidget(m_status);
    statusBar()->showMessage(QStringLiteral("图像：%1").arg(imagePath));

    connect(m_left, &ViewportWidget::rendererReady, this, [this](const QString& backend) {
        m_leftBackend = backend;
        updateStatus();
    });
    connect(m_right, &ViewportWidget::rendererReady, this, [this](const QString& backend) {
        m_rightBackend = backend;
        updateStatus();
    });
    connect(m_left, &ViewportWidget::rendererFailed, this, [this](const QString& message) {
        m_leftBackend = QStringLiteral("失败：%1").arg(message);
        updateStatus();
    });
    connect(m_right, &ViewportWidget::rendererFailed, this, [this](const QString& message) {
        m_rightBackend = QStringLiteral("失败：%1").arg(message);
        updateStatus();
    });

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) { applyTab(index); });

    // 默认并排：左右两张图同时可见。
    m_tabs->setCurrentIndex(2);
    applyTab(2);
    updateStatus();

}

void MainWindow::applyTab(int index)
{
    if (m_leftPanel == nullptr || m_rightPanel == nullptr) {
        return;
    }
    switch (index) {
    case 0: // 左
        m_leftPanel->setVisible(true);
        m_rightPanel->setVisible(false);
        break;
    case 1: // 右
        m_leftPanel->setVisible(false);
        m_rightPanel->setVisible(true);
        break;
    default: // 并排（叠图当前禁用）
        m_leftPanel->setVisible(true);
        m_rightPanel->setVisible(true);
        break;
    }
}

void MainWindow::updateStatus()
{
    if (m_status == nullptr) {
        return;
    }
    m_status->setText(QStringLiteral("左：%1　|　右：%2")
                          .arg(m_leftBackend, m_rightBackend));
}

} // namespace chroma::app
