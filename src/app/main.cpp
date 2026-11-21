#include "app/MainWindow.hpp"
#include "media/ImageLoader.hpp"

#include <QApplication>
#include <QDebug>

#include <memory>

namespace
{
std::shared_ptr<chroma::media::Image> loadImage(const QString& path)
{
    auto image = std::make_shared<chroma::media::Image>();
    std::string error;
    if (chroma::media::loadImage(path.toStdString(), *image, error)) {
        qInfo().noquote() << "[media] 载入图像:" << path << image->width << "x"
                          << image->height;
        return image;
    }
    qCritical().noquote() << "[media] 载入失败:" << path
                          << QString::fromStdString(error);
    return nullptr;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChromaCMS"));
    QApplication::setOrganizationName(QStringLiteral("ChromaCMS"));

    const QString imagePath =
        QStringLiteral(CHROMA_SOURCE_DIR
                       "/assets/test-images/PTMR_8ramps_17_2048x2048_10bit.png");
    std::shared_ptr<chroma::media::Image> image = loadImage(imagePath);
    if (!image) {
        return 1;
    }

    chroma::app::MainWindow window(std::move(image), imagePath);
    window.show();

    return QApplication::exec();
}
