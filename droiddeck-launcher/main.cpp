#include "MainWindow.h"

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KytyPS5 DroidDeck"));
    QApplication::setOrganizationName(QStringLiteral("evansvl"));

    LauncherWindow window;
    window.show();
    return app.exec();
}
