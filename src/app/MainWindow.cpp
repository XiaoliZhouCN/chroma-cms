#include "app/MainWindow.h"

#include "app/ViewportWidget.h"
#include "core/Version.h"
#include "media/ImageData.h"

#include <QFileInfo>
#include <QLabel>
#include <QStatusBar>

#include <string>

namespace chroma::app {

MainWindow::MainWindow(std::shared_ptr<const media::ImageData> image, QString imagePath,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_image(std::move(image))
    , m_imagePath(std::move(imagePath))
{
    setWindowTitle(QStringLiteral("%1 %2 — M1 · D3D12")
                       .arg(QString::fromStdString(std::string(kAppName)))
                       .arg(QString::fromStdString(std::string(kVersion))));

    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    buildStatusBar();

    connect(m_viewport, &ViewportWidget::rendererReady, this,
            [this](const QString& backend) {
                m_backend = backend;
                refreshStatus(m_backend);
            });
    connect(m_viewport, &ViewportWidget::rendererFailed, this, [this](const QString& message) {
        m_backend = QStringLiteral("失败");
        refreshStatus(m_backend);
        statusBar()->showMessage(QStringLiteral("渲染初始化失败：%1").arg(message), 0);
    });

    if (m_image) {
        m_viewport->setImage(m_image);
    }

    resize(1280, 800);
}

void MainWindow::buildStatusBar()
{
    m_status = new QLabel(this);
    statusBar()->addPermanentWidget(m_status);
    refreshStatus(m_backend);
}

void MainWindow::refreshStatus(const QString& backend)
{
    QString imageText = QStringLiteral("图像：未加载");
    if (m_image) {
        const QFileInfo info(m_imagePath);
        imageText = QStringLiteral("图像：%1  %2×%3  %4-bit%5")
                        .arg(info.fileName())
                        .arg(m_image->width)
                        .arg(m_image->height)
                        .arg(m_image->sourceBitDepth)
                        .arg(m_image->reinterpretedFrom10Bit
                                 ? QStringLiteral("（10-bit 码已还原到满量程）")
                                 : QString());
    }

    QString colorText = QStringLiteral("色彩：—");
    if (m_image) {
        const media::ColorMetadata& color = m_image->color;
        colorText = QStringLiteral("色彩：%1 / %2 / %3 / %4（%5）")
                        .arg(QString::fromUtf8(media::colorPrimariesName(color.primaries)),
                             QString::fromUtf8(media::colorTransferName(color.transfer)),
                             QString::fromUtf8(media::colorMatrixName(color.matrix)),
                             QString::fromUtf8(media::colorRangeName(color.range)),
                             QString::fromStdString(color.source.empty() ? "none" : color.source));
    }

    m_status->setText(
        QStringLiteral("%1　|　%2　|　后端：%3").arg(imageText, colorText, backend));
    statusBar()->showMessage(QStringLiteral("M1：D3D12 原生路径显示静态图（未做色彩管理）"));
}

} // namespace chroma::app
