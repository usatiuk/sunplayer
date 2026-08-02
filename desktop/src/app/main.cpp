#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>

#include <QByteArray>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <qpa/qwindowsysteminterface.h>

#include "app/PresentationWindow.h"
#include "diagnostics/ApplicationLog.h"
#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsBackendFactory.h"
#include "playback/MediaSession.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include "platform/linux/LinuxWaylandWindowContext.h"
#endif

#ifdef Q_OS_WIN
namespace {
bool hasExactArgument(
        int argc,
        char *argv[],
        std::string_view expected) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == expected)
            return true;
    }
    return false;
}
}
#endif

namespace {
void sendKeyClick(PresentationWindow &window, Qt::Key key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &release);
}

void sendAutoRepeatKeyPress(PresentationWindow &window, Qt::Key key) {
    QKeyEvent repeated(
        QEvent::KeyPress,
        key,
        Qt::NoModifier,
        QString(),
        true);
    QCoreApplication::sendEvent(&window, &repeated);
}

void sendMouseDoubleClick(
        PresentationWindow &window,
        const QPointF &position) {
    const QPointF globalPosition =
        window.mapToGlobal(position.toPoint());
    QElapsedTimer eventClock;
    eventClock.start();
    ulong timestamp = static_cast<ulong>(
        eventClock.msecsSinceReference());
    const auto send = [&window, &position, &globalPosition, &timestamp](
                              QEvent::Type type,
                              Qt::MouseButtons buttons) {
        QWindowSystemInterface::handleMouseEvent<
            QWindowSystemInterface::SynchronousDelivery>(
            &window,
            timestamp++,
            position,
            globalPosition,
            buttons,
            Qt::LeftButton,
            type,
            Qt::NoModifier);
    };
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
}

enum class FullscreenSmokeStage {
    InitialFrame,
    FullscreenFromNormal,
    RestoredNormal,
    PausedBySpace,
    ResumedBySpace,
    Maximized,
    FullscreenFromMaximized,
    RestoredMaximized,
};

struct FullscreenSmokeState {
    FullscreenSmokeStage stage = FullscreenSmokeStage::InitialFrame;
    qulonglong presentedFrames = 0;
    qulonglong frameBaseline = 0;
};

void startFullscreenSmokeScenario(
        QGuiApplication &app,
        PresentationWindow &window) {
    auto state = std::make_shared<FullscreenSmokeState>();
    auto *const deadline = new QTimer(&app);
    auto *const poll = new QTimer(&app);
    deadline->setSingleShot(true);
    deadline->setInterval(20'000);
    poll->setInterval(25);

    QObject::connect(
        &window,
        &PresentationWindow::videoFramePresented,
        &app,
        [state](qulonglong) { ++state->presentedFrames; });
    QObject::connect(
        deadline,
        &QTimer::timeout,
        &app,
        [&app, &window, state] {
            qCCritical(sunroomLogApplication).noquote()
                << "event=application.fullscreen_smoke_timeout"
                << "stage=" + QString::number(
                    static_cast<int>(state->stage))
                << "windowState=" + QString::number(
                    static_cast<int>(window.windowState()))
                << "presentedFrames=" + QString::number(
                    state->presentedFrames)
                << "error=" + window.mediaSession().errorMessage();
            std::fprintf(
                stderr,
                "Sunroom fullscreen smoke timed out: stage=%d, "
                "windowState=%d, presentedFrames=%llu, "
                "cursorHidden=%d, cursorShape=%d\n",
                static_cast<int>(state->stage),
                static_cast<int>(window.windowState()),
                static_cast<unsigned long long>(
                    state->presentedFrames),
                window.cursorHidden() ? 1 : 0,
                static_cast<int>(window.cursor().shape()));
            std::fflush(stderr);
            app.exit(EXIT_FAILURE);
        });
    QObject::connect(
        poll,
        &QTimer::timeout,
        &app,
        [&app, &window, state, deadline, poll] {
            if (window.mediaSession().state()
                    == MediaSession::State::Error) {
                qCCritical(sunroomLogApplication).noquote()
                    << "event=application.fullscreen_smoke_failed"
                    << "error="
                        + window.mediaSession().errorMessage();
                const QByteArray error =
                    window.mediaSession().errorMessage().toUtf8();
                std::fprintf(
                    stderr,
                    "Sunroom fullscreen smoke media failure: %s\n",
                    error.constData());
                std::fflush(stderr);
                deadline->stop();
                poll->stop();
                app.exit(EXIT_FAILURE);
                return;
            }

            const auto hasNewFrame = [state] {
                return state->presentedFrames > state->frameBaseline;
            };
            const auto waitForNextFrame = [state] {
                state->frameBaseline = state->presentedFrames;
            };

            switch (state->stage) {
            case FullscreenSmokeStage::InitialFrame:
                if (!window.isExposed()
                        || window.windowState() != Qt::WindowNoState
                        || state->presentedFrames == 0
                        || !window.cursorHidden()
                        || window.cursor().shape() != Qt::BlankCursor) {
                    return;
                }
                waitForNextFrame();
                sendKeyClick(window, Qt::Key_F11);
                state->stage =
                    FullscreenSmokeStage::FullscreenFromNormal;
                return;
            case FullscreenSmokeStage::FullscreenFromNormal:
                if (window.windowState() != Qt::WindowFullScreen
                        || !window.cursorHidden()
                        || window.cursor().shape() != Qt::BlankCursor
                        || !hasNewFrame()) {
                    return;
                }
                waitForNextFrame();
                window.setWindowShortcutsBlocked(true);
                sendKeyClick(window, Qt::Key_Escape);
                if (window.windowState() != Qt::WindowFullScreen) {
                    std::fprintf(
                        stderr,
                        "Sunroom fullscreen smoke failed: blocked Escape "
                        "changed state to %d\n",
                        static_cast<int>(window.windowState()));
                    std::fflush(stderr);
                    deadline->stop();
                    poll->stop();
                    app.exit(EXIT_FAILURE);
                    return;
                }
                window.setWindowShortcutsBlocked(false);
                sendKeyClick(window, Qt::Key_Escape);
                state->stage = FullscreenSmokeStage::RestoredNormal;
                return;
            case FullscreenSmokeStage::RestoredNormal:
                if (window.windowState() != Qt::WindowNoState
                        || !hasNewFrame()) {
                    return;
                }
                waitForNextFrame();
                sendAutoRepeatKeyPress(window, Qt::Key_F11);
                if (window.windowState() != Qt::WindowNoState) {
                    qCCritical(sunroomLogApplication).noquote()
                        << "event=application.fullscreen_smoke_failed"
                        << "reason=repeated_f11_changed_state";
                    std::fprintf(
                        stderr,
                        "Sunroom fullscreen smoke failed: "
                        "repeated F11 changed state to %d\n",
                        static_cast<int>(window.windowState()));
                    std::fflush(stderr);
                    deadline->stop();
                    poll->stop();
                    app.exit(EXIT_FAILURE);
                    return;
                }
                sendKeyClick(window, Qt::Key_Escape);
                if (window.windowState() != Qt::WindowNoState) {
                    qCCritical(sunroomLogApplication).noquote()
                        << "event=application.fullscreen_smoke_failed"
                        << "reason=windowed_escape_changed_state";
                    std::fprintf(
                        stderr,
                        "Sunroom fullscreen smoke failed: "
                        "windowed Escape changed state to %d\n",
                        static_cast<int>(window.windowState()));
                    std::fflush(stderr);
                    deadline->stop();
                    poll->stop();
                    app.exit(EXIT_FAILURE);
                    return;
                }
                sendKeyClick(window, Qt::Key_Space);
                if (window.mediaSession().playRequested()) {
                    std::fprintf(
                        stderr,
                        "Sunroom fullscreen smoke failed: "
                        "Space did not pause playback\n");
                    std::fflush(stderr);
                    deadline->stop();
                    poll->stop();
                    app.exit(EXIT_FAILURE);
                    return;
                }
                state->stage = FullscreenSmokeStage::PausedBySpace;
                return;
            case FullscreenSmokeStage::PausedBySpace:
                sendKeyClick(window, Qt::Key_Space);
                if (!window.mediaSession().playRequested()) {
                    std::fprintf(
                        stderr,
                        "Sunroom fullscreen smoke failed: "
                        "Space did not resume playback\n");
                    std::fflush(stderr);
                    deadline->stop();
                    poll->stop();
                    app.exit(EXIT_FAILURE);
                    return;
                }
                state->stage = FullscreenSmokeStage::ResumedBySpace;
                return;
            case FullscreenSmokeStage::ResumedBySpace:
                if (!window.mediaSession().playRequested()
                        || !hasNewFrame()) {
                    return;
                }
                window.showMaximized();
                state->stage = FullscreenSmokeStage::Maximized;
                return;
            case FullscreenSmokeStage::Maximized:
                if (window.windowState() != Qt::WindowMaximized
                        || !hasNewFrame()) {
                    return;
                }
                waitForNextFrame();
                sendMouseDoubleClick(window, QPointF(40.0, 40.0));
                state->stage =
                    FullscreenSmokeStage::FullscreenFromMaximized;
                return;
            case FullscreenSmokeStage::FullscreenFromMaximized:
                if (window.windowState() != Qt::WindowFullScreen
                        || !hasNewFrame()) {
                    return;
                }
                waitForNextFrame();
                sendKeyClick(window, Qt::Key_F11);
                state->stage = FullscreenSmokeStage::RestoredMaximized;
                return;
            case FullscreenSmokeStage::RestoredMaximized:
                if (window.windowState() != Qt::WindowMaximized
                        || !hasNewFrame()) {
                    return;
                }
                qCInfo(sunroomLogApplication).noquote()
                    << "event=application.fullscreen_smoke_complete";
                std::fprintf(
                    stdout,
                    "Sunroom fullscreen smoke passed\n");
                std::fflush(stdout);
                deadline->stop();
                poll->stop();
                app.exit(EXIT_SUCCESS);
                return;
            }
        });

    deadline->start();
    poll->start();
}
}

int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN
    // QGuiApplication initializes the platform plugin in its constructor.
    // Suppress native failure dialogs before that boundary for the bounded
    // noninteractive application scenario.
    if (hasExactArgument(argc, argv, "--playback-smoke")
            || hasExactArgument(argc, argv, "--fullscreen-smoke")) {
        SetErrorMode(
            SEM_FAILCRITICALERRORS
            | SEM_NOGPFAULTERRORBOX
            | SEM_NOOPENFILEERRORBOX);
    }
#endif
#ifdef Q_OS_LINUX
    prepareLinuxWaylandPlatform();
#endif
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
    const QCommandLineOption playbackSmokeOption(
        QStringLiteral("playback-smoke"),
        QStringLiteral(
            "Run a bounded noninteractive real-application playback "
            "scenario for the positional media file."));
    parser.addOption(playbackSmokeOption);
    const QCommandLineOption fullscreenSmokeOption(
        QStringLiteral("fullscreen-smoke"),
        QStringLiteral(
            "Run a bounded noninteractive real-application fullscreen "
            "scenario for the positional media file."));
    parser.addOption(fullscreenSmokeOption);
    parser.process(app);

    if (parser.isSet(logFileOption)
            && parser.isSet(noLogFileOption)) {
        qCCritical(sunroomLogApplication).noquote()
            << "--log-file and --no-log-file cannot be used together.";
        return EXIT_FAILURE;
    }

    const QStringList positionalArguments =
        parser.positionalArguments();
    if (parser.isSet(playbackSmokeOption)
            && parser.isSet(fullscreenSmokeOption)) {
        qCCritical(sunroomLogApplication).noquote()
            << "--playback-smoke and --fullscreen-smoke are mutually exclusive.";
        return EXIT_FAILURE;
    }
    if (parser.isSet(playbackSmokeOption)
            && positionalArguments.size() != 1) {
        qCCritical(sunroomLogApplication).noquote()
            << "--playback-smoke requires exactly one positional media file.";
        return EXIT_FAILURE;
    }
    if (parser.isSet(fullscreenSmokeOption)
            && positionalArguments.size() != 1) {
        qCCritical(sunroomLogApplication).noquote()
            << "--fullscreen-smoke requires exactly one positional media file.";
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
#ifdef Q_OS_WIN
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
#endif

        QQmlEngine engine;
#ifdef Q_OS_WIN
        QStringList importPaths{deployedQmlPath};
        for (const QString &path : engine.importPathList()) {
            if ((path.startsWith(QStringLiteral("qrc:"))
                 || path.startsWith(QStringLiteral(":/")))
                && !importPaths.contains(path)) {
                importPaths.append(path);
            }
        }
        engine.setImportPathList(importPaths);
#endif
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

    GraphicsBackendFactory::configureQtQuick();

#ifdef Q_OS_LINUX
    LinuxWaylandWindowContext windowContext(app);
    PresentationWindow window(windowContext);
#else
    PresentationWindow window;
#endif
    if (!positionalArguments.isEmpty()) {
        const QString absolutePath =
            QFileInfo(positionalArguments.constFirst())
                .absoluteFilePath();
        window.openMedia(QUrl::fromLocalFile(absolutePath));
    }

    QTimer playbackSmokeDeadline;
    QTimer playbackSmokePoll;
    std::optional<qulonglong> firstVideoContentRevision;
    bool distinctVideoContentPresented = false;
    qlonglong firstPresentedPositionMilliseconds = -1;
    if (parser.isSet(playbackSmokeOption)) {
        playbackSmokeDeadline.setSingleShot(true);
        playbackSmokeDeadline.setInterval(15'000);
        QObject::connect(
            &playbackSmokeDeadline,
            &QTimer::timeout,
            &app,
            [&app,
             &window,
             &firstVideoContentRevision,
             &distinctVideoContentPresented] {
                const MediaSession &session =
                    window.mediaSession();
                const auto audio =
                    session.currentAudioPresentation();
                qCCritical(sunroomLogApplication).noquote()
                    << "event=application.playback_smoke_timeout"
                    << "state=" + QString::number(
                        static_cast<int>(session.state()))
                    << "hasFrame=" + QString(
                        session.hasFrame()
                        ? QStringLiteral("true")
                        : QStringLiteral("false"))
                    << "positionMs=" + QString::number(
                        session.positionMilliseconds())
                    << "error=" + session.errorMessage();
                std::fprintf(
                    stderr,
                    "Sunroom playback smoke timed out: state=%d, "
                    "hasFrame=%d, positionMs=%lld, "
                    "audioPresented=%llu, audioValid=%d, "
                    "firstVideoRevision=%llu, "
                    "distinctVideoRevision=%d\n",
                    static_cast<int>(session.state()),
                    session.hasFrame() ? 1 : 0,
                    static_cast<long long>(
                        session.positionMilliseconds()),
                    static_cast<unsigned long long>(
                        audio ? audio->presentedFrames : 0),
                    audio && audio->valid ? 1 : 0,
                    static_cast<unsigned long long>(
                        firstVideoContentRevision.value_or(0)),
                    distinctVideoContentPresented ? 1 : 0);
                std::fflush(stderr);
                app.exit(EXIT_FAILURE);
            });
        QObject::connect(
            &window,
            &PresentationWindow::videoFramePresented,
            &app,
            [&window,
             &firstVideoContentRevision,
             &distinctVideoContentPresented,
             &firstPresentedPositionMilliseconds](
                    qulonglong contentRevision) {
                if (!firstVideoContentRevision) {
                    firstVideoContentRevision = contentRevision;
                } else if (contentRevision
                        != *firstVideoContentRevision) {
                    distinctVideoContentPresented = true;
                }
                if (firstPresentedPositionMilliseconds < 0) {
                    firstPresentedPositionMilliseconds =
                        window.mediaSession()
                            .positionMilliseconds();
                }
            });
        playbackSmokePoll.setInterval(25);
        QObject::connect(
            &playbackSmokePoll,
            &QTimer::timeout,
            &app,
            [&app,
             &window,
             &playbackSmokeDeadline,
             &playbackSmokePoll,
             &firstVideoContentRevision,
             &distinctVideoContentPresented,
             &firstPresentedPositionMilliseconds] {
                const MediaSession &session =
                    window.mediaSession();
                if (session.state() == MediaSession::State::Error) {
                    qCCritical(sunroomLogApplication).noquote()
                        << "event=application.playback_smoke_failed"
                        << "error=" + session.errorMessage();
                    const QByteArray error =
                        session.errorMessage().toUtf8();
                    std::fprintf(
                        stderr,
                        "Sunroom playback smoke failed: %s\n",
                        error.constData());
                    std::fflush(stderr);
                    playbackSmokeDeadline.stop();
                    playbackSmokePoll.stop();
                    app.exit(EXIT_FAILURE);
                    return;
                }

                const qlonglong position =
                    session.positionMilliseconds();
                const auto audio =
                    session.currentAudioPresentation();
                if (!firstVideoContentRevision
                        || !distinctVideoContentPresented
                        || !session.hasFrame()
                        || !audio
                        || !audio->valid
                        || !audio->advancing
                        || audio->presentedFrames == 0
                        || audio->mediaPositionMicroseconds
                            < 1'500'000
                        || position < 1'500
                        || position
                            < firstPresentedPositionMilliseconds
                                + 250) {
                    return;
                }

                qCInfo(sunroomLogApplication).noquote()
                    << "event=application.playback_smoke_complete"
                    << "positionMs=" + QString::number(position);
                std::fprintf(
                    stdout,
                    "Sunroom playback smoke passed at %lld ms\n",
                    static_cast<long long>(position));
                std::fflush(stdout);
                playbackSmokeDeadline.stop();
                playbackSmokePoll.stop();
                app.exit(EXIT_SUCCESS);
            });
        playbackSmokeDeadline.start();
        playbackSmokePoll.start();
    }
    if (parser.isSet(fullscreenSmokeOption))
        startFullscreenSmokeScenario(app, window);
    window.show();

    const int exitCode = app.exec();
    qCInfo(sunroomLogApplication).noquote()
        << "event=application.stop"
        << "exitCode=" + QString::number(exitCode);
    return exitCode;
}
