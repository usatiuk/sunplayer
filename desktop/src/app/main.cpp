#include <cstdlib>

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStyleHints>
#include <QUrl>

#include "app/PresentationWindow.h"
#include "diagnostics/ApplicationLog.h"
#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsBackendFactory.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    QCoreApplication::setApplicationName(
        QStringLiteral("Sunroom"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(SUNROOM_VERSION));

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
    const QCommandLineOption debugLogOption(
        QStringLiteral("debug-log"),
        QStringLiteral(
            "Enable Sunroom debug logging in the session log."));
    parser.addOption(debugLogOption);
    const QCommandLineOption logFileOption(
        QStringLiteral("log-file"),
        QStringLiteral(
            "Write the session log to local <path> instead of the temporary "
            "Sunroom log directory."),
        QStringLiteral("path"));
    parser.addOption(logFileOption);
    const QCommandLineOption noLogFileOption(
        QStringLiteral("no-log-file"),
        QStringLiteral(
            "Disable the session log file; console/debugger logging remains."));
    parser.addOption(noLogFileOption);
    parser.process(app);

    if (parser.isSet(logFileOption)
            && parser.isSet(noLogFileOption)) {
        qCCritical(sunroomLogApplication).noquote()
            << "--log-file and --no-log-file cannot be used together.";
        return EXIT_FAILURE;
    }

    ApplicationLogOptions logOptions{
        .fileEnabled = !parser.isSet(noLogFileOption),
        .debugEnabled = parser.isSet(debugLogOption),
        .filePath = parser.value(logFileOption),
    };
    QString logError;
    std::unique_ptr<ApplicationLog> applicationLog =
        ApplicationLog::install(logOptions, &logError);
    if (!applicationLog && logOptions.fileEnabled) {
        qCWarning(sunroomLogApplication).noquote()
            << logError
            << "- continuing without a session log file.";
        logOptions.fileEnabled = false;
        logOptions.filePath.clear();
        applicationLog =
            ApplicationLog::install(logOptions, &logError);
    }
    if (!applicationLog) {
        qCWarning(sunroomLogApplication).noquote()
            << "Could not initialize application logging:"
            << logError;
    } else {
        qCInfo(sunroomLogApplication).noquote()
            << "event=application.start"
            << "version=" + QCoreApplication::applicationVersion()
            << "debug=" + QString(
                applicationLog->debugEnabled()
                ? QStringLiteral("true")
                : QStringLiteral("false"))
            << "file=" + (
                applicationLog->filePath().isEmpty()
                ? QStringLiteral("disabled")
                : QStringLiteral("enabled"));
        if (!applicationLog->filePath().isEmpty()) {
            qCDebug(
                sunroomLogApplication).noquote()
                << "event=application.log_file"
                << "path="
                    + applicationLog->filePath();
        }
    }

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

    if (parser.isSet(verifyQmlOption)) {
        const QString applicationDirectory =
            QCoreApplication::applicationDirPath();
        QCoreApplication::setLibraryPaths({applicationDirectory});
        const QString deployedQmlPath =
            QDir(applicationDirectory).filePath(
                QStringLiteral("qml"));
        if (!QFileInfo(deployedQmlPath).isDir()) {
            qCCritical(sunroomLogApplication).noquote()
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
            qCCritical(sunroomLogApplication).noquote()
                << component.errorString();
            return EXIT_FAILURE;
        }
        qCInfo(sunroomLogApplication).noquote()
            << "event=application.verify_qml_complete";
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

    const int exitCode = app.exec();
    qCInfo(sunroomLogApplication).noquote()
        << "event=application.stop"
        << "exitCode=" + QString::number(exitCode);
    return exitCode;
}
