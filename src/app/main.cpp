#include "app/MainWindow.h"

#include <QApplication>
#include <QStringList>
#include <QTimer>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChromaCMS"));
    QApplication::setOrganizationName(QStringLiteral("ChromaCMS"));

    // --selftest：用于 ctest 的无交互冒烟启动（显示窗口后自动退出）。
    const bool selfTest = QApplication::arguments().contains(QStringLiteral("--selftest"));

    chroma::app::MainWindow window;
    window.show();

    if (selfTest) {
        QTimer::singleShot(1200, &app, &QCoreApplication::quit);
    }

    return QApplication::exec();
}
