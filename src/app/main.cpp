#include "app/MainWindow.h"

#include "core/Version.h"
#include "media/ImageLoader.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QTimer>

#include <memory>
#include <string>

namespace {

QString defaultImagePath()
{
    return QStringLiteral(CHROMA_SOURCE_DIR
                          "/assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChromaCMS"));
    QApplication::setOrganizationName(QStringLiteral("ChromaCMS"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "ChromaCMS —— Windows 图像/视频显示工具（双渲染路径 · 自研色彩管理）"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption imageOption(
        {QStringLiteral("i"), QStringLiteral("image")},
        QStringLiteral("要显示的图片路径（默认使用 assets/test-images 下的 PTMR 测试图）"),
        QStringLiteral("path"));
    const QCommandLineOption selfTestOption(
        QStringLiteral("selftest"), QStringLiteral("无交互自检：显示窗口后自动退出"));
    parser.addOption(imageOption);
    parser.addOption(selfTestOption);
    parser.process(app);

    const QString imagePath = parser.isSet(imageOption) ? parser.value(imageOption)
                                                        : defaultImagePath();

    std::shared_ptr<chroma::media::ImageData> image;
    if (QFileInfo::exists(imagePath)) {
        auto loaded = std::make_shared<chroma::media::ImageData>();
        std::string error;
        if (chroma::media::loadImage(imagePath.toStdWString(), *loaded, error)) {
            image = loaded;
            const auto& c = image->color;
            qInfo().noquote() << "[media] 载入图像:" << imagePath << image->width << "x"
                              << image->height << " 源位深" << image->sourceBitDepth
                              << (image->reinterpretedFrom10Bit ? "(10-bit 还原)" : "");
            qInfo().noquote() << "[media] 色彩元数据(" << QString::fromStdString(c.source)
                              << "): primaries="
                              << chroma::media::colorPrimariesName(c.primaries)
                              << " transfer=" << chroma::media::colorTransferName(c.transfer)
                              << " matrix=" << chroma::media::colorMatrixName(c.matrix)
                              << " range=" << chroma::media::colorRangeName(c.range);
        }
        else {
            qCritical().noquote() << "[media] 载入失败:" << QString::fromStdString(error);
        }
    }
    else {
        qWarning().noquote() << "[media] 文件不存在:" << imagePath;
    }

    chroma::app::MainWindow window(image, imagePath);
    window.show();

    if (parser.isSet(selfTestOption)) {
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }

    const int result = QApplication::exec();
    return result;
}
