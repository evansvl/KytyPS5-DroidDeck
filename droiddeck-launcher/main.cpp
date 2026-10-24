#include "BigPictureWindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KytyPS5 DroidDeck"));
    QApplication::setOrganizationName(QStringLiteral("evansvl"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    BigPictureWindow window;
    window.showFullScreen();
    QTimer::singleShot(200, &window, [&window] { window.showFullScreen(); });
    return app.exec();
}
