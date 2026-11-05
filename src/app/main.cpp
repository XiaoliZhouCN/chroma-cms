#include "app/MainWindow.hpp"

#include <QApplication>

int main(int argc, char *argv[])
{

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChromaCMS"));
    QApplication::setOrganizationName(QStringLiteral("ChromaCMS"));

    chroma::app::MainWindow window;
    window.show();

    return QApplication::exec();
}
