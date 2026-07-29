#include <cstdlib>

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>

#include "app/PresentationWindow.h"
#include "graphics/GraphicsBackendFactory.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    GraphicsBackendFactory::configureQtQuick();

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#111318")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#f2f4f8")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#171a21")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#20242d")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#f2f4f8")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#252a35")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#f2f4f8")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#4f8cff")));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#8e97a8")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#252a35")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#f2f4f8")));
    app.setPalette(palette);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Sunroom HDR video player"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("media"),
        QStringLiteral("Local media file to open."),
        QStringLiteral("[media]"));
    const QCommandLineOption verifyQmlOption(
        QStringLiteral("verify-qml"),
        QStringLiteral(
            "Load the packaged QML module and exit without opening a window."));
    parser.addOption(verifyQmlOption);
    parser.process(app);

    if (parser.isSet(verifyQmlOption)) {
        const QString applicationDirectory =
            QCoreApplication::applicationDirPath();
        QCoreApplication::setLibraryPaths({applicationDirectory});
        const QString deployedQmlPath =
            QDir(applicationDirectory).filePath(
                QStringLiteral("qml"));
        if (!QFileInfo(deployedQmlPath).isDir()) {
            qCritical().noquote()
                << "Missing deployed QML directory:"
                << deployedQmlPath;
            return EXIT_FAILURE;
        }

        QQmlEngine engine;
        QStringList importPaths{deployedQmlPath};
        for (const QString &path : engine.importPathList()) {
            if ((path.startsWith(QStringLiteral("qrc:"))
                 || path.startsWith(QStringLiteral(":/")))
                && !importPaths.contains(path)) {
                importPaths.append(path);
            }
        }
        engine.setImportPathList(importPaths);
        QQmlComponent component(&engine);
        component.loadFromModule(
            QStringLiteral("Sunroom"), QStringLiteral("Main"));
        if (component.isError()) {
            qCritical().noquote() << component.errorString();
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    PresentationWindow window;
    const QStringList positionalArguments =
        parser.positionalArguments();
    if (!positionalArguments.isEmpty()) {
        const QString absolutePath =
            QFileInfo(positionalArguments.constFirst())
                .absoluteFilePath();
        window.openMedia(QUrl::fromLocalFile(absolutePath));
    }
    window.show();

    return app.exec();
}
