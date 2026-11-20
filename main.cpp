#include "mainwindow.h"
#include "version.h"
#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QSettings>
#include <QStyleFactory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Tralow"));
    app.setApplicationName(QStringLiteral("Tabor8200-Control"));
    app.setApplicationDisplayName(QStringLiteral("Tabor 8200 Control"));
    app.setApplicationVersion(QString::fromLatin1(applicationVersion));
    app.setWindowIcon(QIcon(QStringLiteral(":/device-icon.ico")));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont font = app.font();
    if (QFontDatabase::hasFamily(QStringLiteral("Inter"))) {
        font.setFamily(QStringLiteral("Inter"));
    }
    font.setPointSize(10);
    app.setFont(font);
    MainWindow window;
    QSettings settings;
    const auto geometryKey = QStringLiteral("window/geometry");
    window.restoreGeometry(settings.value(geometryKey).toByteArray());
    window.show();

    const int exitCode = app.exec();
    settings.setValue(geometryKey, window.saveGeometry());
    return exitCode;
}
