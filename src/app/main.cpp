#include "app/MainWindow.hpp"
#include "media/ImageLoader.hpp"

#include <QApplication>

#include <memory>

int main(int argc, char *argv[])
{

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChromaCMS"));
    QApplication::setOrganizationName(QStringLiteral("ChromaCMS"));

    const QString imagePath =
        QStringLiteral(CHROMA_SOURCE_DIR
                       "/assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png");

    std::shared_ptr<chroma::media::Image> image;
    {
        auto loaded = std::make_shared<chroma::media::Image>();
        std::string error;
        if (chroma::media::loadImage(imagePath.toStdString(), *loaded, error))
        {
            image = loaded;
            qInfo().noquote() << "[media] 载入图像:" << imagePath << loaded->width << "x"
                              << loaded->height;
        }
        else
        {
            qCritical().noquote() << "[media] 载入失败:" << QString::fromStdString(error);
        }
    }

    chroma::app::MainWindow window(image, imagePath);

    // chroma::app::MainWindow window;
    window.show();

    return QApplication::exec();
}
