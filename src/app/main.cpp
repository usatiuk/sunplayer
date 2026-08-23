#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <qpa/qwindowsysteminterface.h>

#include "app/ApplicationError.h"
#include "app/ApplicationSettings.h"
#include "app/PresentationWindow.h"
#include "app/SupportController.h"
#include "diagnostics/ApplicationLog.h"
#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsBackendFactory.h"
#include "playback/MediaSession.h"

#ifdef Q_OS_WIN
#include <qpa/qplatformdrag.h>
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include "platform/linux/LinuxWaylandWindowContext.h"
#endif

#ifdef Q_OS_WIN
namespace {
bool hasExactArgument(int argc, char* argv[], std::string_view expected) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == expected) {
            return true;
        }
    }
    return false;
}

bool verifyInitialWindowBackground(PresentationWindow& window) {
    HWND const windowHandle = reinterpret_cast<HWND>(window.winId());
    HDC const screenDeviceContext = GetDC(nullptr);
    HDC const memoryDeviceContext = screenDeviceContext ? CreateCompatibleDC(screenDeviceContext) : nullptr;
    HBITMAP const bitmap = screenDeviceContext ? CreateCompatibleBitmap(screenDeviceContext, 1, 1) : nullptr;
    HGDIOBJ const previousObject = memoryDeviceContext && bitmap ? SelectObject(memoryDeviceContext, bitmap) : nullptr;
    bool const bitmapSelected = previousObject && previousObject != HGDI_ERROR;

    bool passed = false;
    if (windowHandle && memoryDeviceContext && bitmap && bitmapSelected &&
        PatBlt(memoryDeviceContext, 0, 0, 1, 1, WHITENESS)) {
        LRESULT const eraseResult =
            SendMessageW(windowHandle, WM_ERASEBKGND, reinterpret_cast<WPARAM>(memoryDeviceContext), 0);
        passed = eraseResult == 1 && GetPixel(memoryDeviceContext, 0, 0) == RGB(0, 0, 0);
    }

    if (memoryDeviceContext && bitmapSelected) {
        SelectObject(memoryDeviceContext, previousObject);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    if (memoryDeviceContext) {
        DeleteDC(memoryDeviceContext);
    }
    if (screenDeviceContext) {
        ReleaseDC(nullptr, screenDeviceContext);
    }

    std::fprintf(passed ? stdout : stderr, "SunPlayer initial window background verification %s.\n",
                 passed ? "passed" : "failed");
    std::fflush(passed ? stdout : stderr);
    return passed;
}
} // namespace
#endif

namespace {
constexpr qreal fullscreenSmokeStoredVolume = 0.37;
constexpr qreal fullscreenSmokeChangedVolume = 0.62;

void sendKeyClick(PresentationWindow& window, Qt::Key key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &release);
}

void sendAutoRepeatKeyPress(PresentationWindow& window, Qt::Key key) {
    QKeyEvent repeated(QEvent::KeyPress, key, Qt::NoModifier, QString(), true);
    QCoreApplication::sendEvent(&window, &repeated);
}

void sendMouseDoubleClick(PresentationWindow& window, QPointF const& position) {
    QPointF const globalPosition = window.mapToGlobal(position.toPoint());
    QElapsedTimer eventClock;
    eventClock.start();
    ulong timestamp = static_cast<ulong>(eventClock.msecsSinceReference());
    auto const send = [&window, &position, &globalPosition, &timestamp](QEvent::Type type, Qt::MouseButtons buttons) {
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
            &window, timestamp++, position, globalPosition, buttons, Qt::LeftButton, type, Qt::NoModifier);
    };
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
}

qsizetype otherDisplayBlankingWindowCount(PresentationWindow const& presentationWindow) {
    qsizetype count = 0;
    for (QWindow const* const window : QGuiApplication::topLevelWindows()) {
        if (window->transientParent() == &presentationWindow) {
            ++count;
        }
    }
    return count;
}

bool otherDisplayBlankingReady(PresentationWindow const& presentationWindow) {
    QList<QScreen*> remainingScreens = QGuiApplication::screens();
    remainingScreens.removeOne(presentationWindow.screen());
    for (QWindow const* const window : QGuiApplication::topLevelWindows()) {
        if (window->transientParent() != &presentationWindow) {
            continue;
        }
        if (!window->isVisible() || window->windowState() != Qt::WindowFullScreen ||
            !remainingScreens.removeOne(window->screen()) || window->type() != Qt::Tool ||
            !window->flags().testFlag(Qt::FramelessWindowHint) ||
            !window->flags().testFlag(Qt::WindowDoesNotAcceptFocus) ||
            window->flags().testFlag(Qt::WindowStaysOnTopHint)) {
            return false;
        }
    }
    return remainingScreens.isEmpty();
}

int showStartupError(ApplicationError error, SupportController& supportController) {
    for (;;) {
        supportController.setApplicationError(error);
        QMessageBox dialog(QMessageBox::Critical, QObject::tr("SunPlayer startup error"), error.userMessage(),
                           QMessageBox::NoButton);
        dialog.setInformativeText(QObject::tr("Error code: %1").arg(error.stableCode()));
        dialog.setDetailedText(error.technicalDetail());
        ApplicationErrorActions const actions = error.suggestedActions();
        QAbstractButton* const restart = actions.testFlag(ApplicationErrorAction::Restart)
                                             ? dialog.addButton(QObject::tr("Restart"), QMessageBox::AcceptRole)
                                             : nullptr;
        QAbstractButton* const report = actions.testFlag(ApplicationErrorAction::ReportBug)
                                            ? dialog.addButton(QObject::tr("Report a bug…"), QMessageBox::HelpRole)
                                            : nullptr;
        QAbstractButton* const quit = actions.testFlag(ApplicationErrorAction::Quit)
                                          ? dialog.addButton(QObject::tr("Quit"), QMessageBox::RejectRole)
                                          : nullptr;
        dialog.exec();
        if (restart && dialog.clickedButton() == restart) {
            if (QProcess::startDetached(QCoreApplication::applicationFilePath(), {})) {
                return EXIT_SUCCESS;
            }
            error = ApplicationError(ApplicationError::Code::RestartFailed, QObject::tr("SunPlayer could not restart."),
                                     QStringLiteral("QProcess::startDetached rejected the replacement process"));
        } else if (report && dialog.clickedButton() == report) {
            supportController.reportBug();
        } else if ((quit && dialog.clickedButton() == quit) || !dialog.clickedButton()) {
            return EXIT_FAILURE;
        }
    }
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
    std::uint64_t initialAudioPresentedFrames = 0;
    std::uint64_t audioOutputEpoch = 0;
    bool blankingDisableCycleCompleted = false;
};

void startFullscreenSmokeScenario(QGuiApplication& app, PresentationWindow& window,
                                  ApplicationSettings& applicationSettings) {
    auto state = std::make_shared<FullscreenSmokeState>();
    auto* const deadline = new QTimer(&app);
    auto* const poll = new QTimer(&app);
    deadline->setSingleShot(true);
    deadline->setInterval(20'000);
    poll->setInterval(25);

    QObject::connect(&window, &PresentationWindow::videoFramePresented, &app,
                     [state](qulonglong) { ++state->presentedFrames; });
    QObject::connect(deadline, &QTimer::timeout, &app, [&app, &window, state] {
        qCCritical(sunplayerLogApplication).noquote()
            << "event=application.fullscreen_smoke_timeout"
            << "stage=" + QString::number(static_cast<int>(state->stage))
            << "windowState=" + QString::number(static_cast<int>(window.windowState()))
            << "presentedFrames=" + QString::number(state->presentedFrames)
            << "error=" + window.mediaSession().errorMessage();
        std::fprintf(stderr,
                     "SunPlayer fullscreen smoke timed out: stage=%d, "
                     "windowState=%d, presentedFrames=%llu, "
                     "cursorHidden=%d, cursorShape=%d, blankingWindows=%lld\n",
                     static_cast<int>(state->stage), static_cast<int>(window.windowState()),
                     static_cast<unsigned long long>(state->presentedFrames), window.cursorHidden() ? 1 : 0,
                     static_cast<int>(window.cursor().shape()),
                     static_cast<long long>(otherDisplayBlankingWindowCount(window)));
        std::fflush(stderr);
        app.exit(EXIT_FAILURE);
    });
    QObject::connect(poll, &QTimer::timeout, &app, [&app, &window, &applicationSettings, state, deadline, poll] {
        if (window.mediaSession().state() == MediaSession::State::Error) {
            qCCritical(sunplayerLogApplication).noquote() << "event=application.fullscreen_smoke_failed"
                                                          << "error=" + window.mediaSession().errorMessage();
            QByteArray const error = window.mediaSession().errorMessage().toUtf8();
            std::fprintf(stderr, "SunPlayer fullscreen smoke media failure: %s\n", error.constData());
            std::fflush(stderr);
            deadline->stop();
            poll->stop();
            app.exit(EXIT_FAILURE);
            return;
        }

        auto const hasNewFrame = [state] { return state->presentedFrames > state->frameBaseline; };
        auto const waitForNextFrame = [state] { state->frameBaseline = state->presentedFrames; };

        switch (state->stage) {
        case FullscreenSmokeStage::InitialFrame: {
            auto const audio = window.mediaSession().currentAudioPresentation();
            if (!window.isExposed() || window.windowState() != Qt::WindowNoState || state->presentedFrames == 0 ||
                !audio || !audio->valid || !audio->advancing || audio->presentedFrames == 0 || !window.cursorHidden() ||
                window.cursor().shape() != Qt::BlankCursor) {
                return;
            }
            state->initialAudioPresentedFrames = audio->presentedFrames;
            state->audioOutputEpoch = audio->audioOutputEpoch;
#ifdef Q_OS_WIN
            if (!window.otherDisplayBlankingAvailable()) {
                std::fprintf(stderr, "SunPlayer fullscreen smoke failed: Windows display blanking is unavailable\n");
                std::fflush(stderr);
                deadline->stop();
                poll->stop();
                app.exit(EXIT_FAILURE);
                return;
            }
#endif
            if (window.otherDisplayBlankingAvailable()) {
                window.setBlankOtherDisplaysInFullscreen(true);
            }
            waitForNextFrame();
            sendKeyClick(window, Qt::Key_F11);
            state->stage = FullscreenSmokeStage::FullscreenFromNormal;
            return;
        }
        case FullscreenSmokeStage::FullscreenFromNormal:
            if (window.windowState() != Qt::WindowFullScreen || !window.cursorHidden() ||
                window.cursor().shape() != Qt::BlankCursor || !hasNewFrame() ||
                (window.otherDisplayBlankingAvailable() && !otherDisplayBlankingReady(window))) {
                return;
            }
            if (window.otherDisplayBlankingAvailable() && !state->blankingDisableCycleCompleted) {
                window.setBlankOtherDisplaysInFullscreen(false);
                if (otherDisplayBlankingWindowCount(window) != 0) {
                    return;
                }
                window.setBlankOtherDisplaysInFullscreen(true);
                state->blankingDisableCycleCompleted = true;
                return;
            }
            waitForNextFrame();
            window.setWindowShortcutsBlocked(true);
            sendKeyClick(window, Qt::Key_Escape);
            if (window.windowState() != Qt::WindowFullScreen) {
                std::fprintf(stderr,
                             "SunPlayer fullscreen smoke failed: blocked Escape "
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
            if (window.windowState() != Qt::WindowNoState || !hasNewFrame() ||
                otherDisplayBlankingWindowCount(window) != 0) {
                return;
            }
            waitForNextFrame();
            sendAutoRepeatKeyPress(window, Qt::Key_F11);
            if (window.windowState() != Qt::WindowNoState) {
                qCCritical(sunplayerLogApplication).noquote() << "event=application.fullscreen_smoke_failed"
                                                              << "reason=repeated_f11_changed_state";
                std::fprintf(stderr,
                             "SunPlayer fullscreen smoke failed: "
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
                qCCritical(sunplayerLogApplication).noquote() << "event=application.fullscreen_smoke_failed"
                                                              << "reason=windowed_escape_changed_state";
                std::fprintf(stderr,
                             "SunPlayer fullscreen smoke failed: "
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
                std::fprintf(stderr, "SunPlayer fullscreen smoke failed: "
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
                std::fprintf(stderr, "SunPlayer fullscreen smoke failed: "
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
            if (!window.mediaSession().playRequested() || !hasNewFrame()) {
                return;
            }
            window.showMaximized();
            state->stage = FullscreenSmokeStage::Maximized;
            return;
        case FullscreenSmokeStage::Maximized:
            if (window.windowState() != Qt::WindowMaximized || !hasNewFrame()) {
                return;
            }
            waitForNextFrame();
            sendMouseDoubleClick(window, QPointF(40.0, 40.0));
            state->stage = FullscreenSmokeStage::FullscreenFromMaximized;
            return;
        case FullscreenSmokeStage::FullscreenFromMaximized:
            if (window.windowState() != Qt::WindowFullScreen || !hasNewFrame() ||
                (window.otherDisplayBlankingAvailable() && !otherDisplayBlankingReady(window))) {
                return;
            }
            waitForNextFrame();
            sendKeyClick(window, Qt::Key_F11);
            state->stage = FullscreenSmokeStage::RestoredMaximized;
            return;
        case FullscreenSmokeStage::RestoredMaximized: {
            auto const audio = window.mediaSession().currentAudioPresentation();
            if (window.windowState() != Qt::WindowMaximized || !hasNewFrame() || !audio ||
                audio->audioOutputEpoch != state->audioOutputEpoch ||
                audio->presentedFrames <= state->initialAudioPresentedFrames ||
                otherDisplayBlankingWindowCount(window) != 0) {
                return;
            }
            window.setBlankOtherDisplaysInFullscreen(false);
            ApplicationSettings::Values const persistedSettings = applicationSettings.load();
            bool const expectedPersistedBlanking = !window.otherDisplayBlankingAvailable();
            if (!persistedSettings.volume || !qFuzzyCompare(*persistedSettings.volume, fullscreenSmokeChangedVolume) ||
                !persistedSettings.blankOtherDisplaysInFullscreen ||
                *persistedSettings.blankOtherDisplaysInFullscreen != expectedPersistedBlanking) {
                qCCritical(sunplayerLogApplication).noquote() << "event=application.fullscreen_smoke_failed"
                                                              << "reason=settings_write_through_failed";
                std::fprintf(stderr, "SunPlayer fullscreen smoke settings write-through failed\n");
                std::fflush(stderr);
                deadline->stop();
                poll->stop();
                app.exit(EXIT_FAILURE);
                return;
            }
            qCInfo(sunplayerLogApplication).noquote()
                << "event=application.fullscreen_smoke_complete"
                << "audioBackend=" + window.mediaSession().audioBackend()
                << "audioPresented=" + QString::number(audio->presentedFrames)
                << "screenCount=" + QString::number(QGuiApplication::screens().size());
            QByteArray const backend = window.mediaSession().audioBackend().toUtf8();
            std::fprintf(stdout,
                         "SunPlayer fullscreen smoke passed: "
                         "audioBackend=%s, audioPresented=%llu, screenCount=%lld\n",
                         backend.constData(), static_cast<unsigned long long>(audio->presentedFrames),
                         static_cast<long long>(QGuiApplication::screens().size()));
            std::fflush(stdout);
            deadline->stop();
            poll->stop();
            app.exit(EXIT_SUCCESS);
            return;
        }
        }
    });

    deadline->start();
    poll->start();
}

#ifdef Q_OS_WIN
void startFileDropSmokeScenario(QGuiApplication& app, PresentationWindow& window, QUrl const& expectedUrl) {
    QTimer::singleShot(0, &app, [&app, &window, expectedUrl] {
        QMimeData mimeData;
        mimeData.setUrls({expectedUrl});
        QPoint const dropPosition(window.width() / 2, window.height() / 2);
        Qt::DropActions const availableActions = Qt::CopyAction | Qt::MoveAction;
        QPlatformDragQtResponse const dragResponse = QWindowSystemInterface::handleDrag(
            &window, &mimeData, dropPosition, availableActions, Qt::NoButton, Qt::NoModifier);
        QPlatformDropQtResponse const dropResponse = QWindowSystemInterface::handleDrop(
            &window, &mimeData, dropPosition, availableActions, Qt::NoButton, Qt::NoModifier);

        bool const passed = dragResponse.isAccepted() && dragResponse.acceptedAction() == Qt::CopyAction &&
                            dropResponse.isAccepted() && dropResponse.acceptedAction() == Qt::CopyAction &&
                            window.mediaSession().mediaUrl() == expectedUrl;
        qCInfo(sunplayerLogApplication).noquote()
            << (passed ? "event=application.file_drop_smoke_complete"
                       : "event=application.file_drop_smoke_failed")
            << "dragAccepted=" + QString::number(dragResponse.isAccepted())
            << "dragAction=" + QString::number(static_cast<int>(dragResponse.acceptedAction()))
            << "dropAccepted=" + QString::number(dropResponse.isAccepted())
            << "dropAction=" + QString::number(static_cast<int>(dropResponse.acceptedAction()));
        std::fprintf(passed ? stdout : stderr, "SunPlayer file-drop smoke %s.\n", passed ? "passed" : "failed");
        std::fflush(passed ? stdout : stderr);
        app.exit(passed ? EXIT_SUCCESS : EXIT_FAILURE);
    });
}
#endif
} // namespace

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // QGuiApplication initializes the platform plugin in its constructor.
    // Suppress native failure dialogs before that boundary for the bounded
    // noninteractive application scenario.
    if (hasExactArgument(argc, argv, "--playback-smoke") || hasExactArgument(argc, argv, "--fullscreen-smoke") ||
        hasExactArgument(argc, argv, "--file-drop-smoke") ||
        hasExactArgument(argc, argv, "--verify-initial-background")) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    }
#endif
#ifdef Q_OS_LINUX
    prepareLinuxWaylandPlatform();
#endif
    QApplication app(argc, argv);
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    QCoreApplication::setOrganizationName(QStringLiteral("usatiuk"));
    QCoreApplication::setApplicationName(QStringLiteral("SunPlayer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SUNPLAYER_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/SunPlayer/icons/SunPlayer.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SunPlayer HDR video player"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("media"), QStringLiteral("Local media file to open."),
                                 QStringLiteral("[media]"));
    QCommandLineOption const verifyQmlOption(
        QStringLiteral("verify-qml"),
        QStringLiteral("Load the packaged QML module and exit without opening a window."));
    parser.addOption(verifyQmlOption);
#ifdef Q_OS_WIN
    QCommandLineOption const verifyInitialBackgroundOption(
        QStringLiteral("verify-initial-background"),
        QStringLiteral("Verify the Windows pre-presentation client background and exit."));
    parser.addOption(verifyInitialBackgroundOption);
    QCommandLineOption const fileDropSmokeOption(
        QStringLiteral("file-drop-smoke"),
        QStringLiteral("Run a bounded noninteractive native file-drop scenario for the positional media file."));
    parser.addOption(fileDropSmokeOption);
#endif
    QCommandLineOption const debugLogOption(QStringLiteral("debug-log"),
                                            QStringLiteral("Enable SunPlayer debug logging in the session log."));
    parser.addOption(debugLogOption);
    QCommandLineOption const logFileOption(
        QStringLiteral("log-file"),
        QStringLiteral("Write the session log to local <path> instead of the temporary "
                       "SunPlayer log directory."),
        QStringLiteral("path"));
    parser.addOption(logFileOption);
    QCommandLineOption const noLogFileOption(
        QStringLiteral("no-log-file"),
        QStringLiteral("Disable the session log file; console/debugger logging remains."));
    parser.addOption(noLogFileOption);
    QCommandLineOption const playbackSmokeOption(
        QStringLiteral("playback-smoke"), QStringLiteral("Run a bounded noninteractive real-application playback "
                                                         "scenario for the positional media file."));
    parser.addOption(playbackSmokeOption);
    QCommandLineOption const fullscreenSmokeOption(
        QStringLiteral("fullscreen-smoke"), QStringLiteral("Run a bounded noninteractive real-application fullscreen "
                                                           "scenario for the positional media file."));
    parser.addOption(fullscreenSmokeOption);
    parser.process(app);

    if (parser.isSet(logFileOption) && parser.isSet(noLogFileOption)) {
        qCCritical(sunplayerLogApplication).noquote() << "--log-file and --no-log-file cannot be used together.";
        return EXIT_FAILURE;
    }

    QStringList const positionalArguments = parser.positionalArguments();
    if (parser.isSet(playbackSmokeOption) && parser.isSet(fullscreenSmokeOption)) {
        qCCritical(sunplayerLogApplication).noquote()
            << "--playback-smoke and --fullscreen-smoke are mutually exclusive.";
        return EXIT_FAILURE;
    }
    if (parser.isSet(playbackSmokeOption) && positionalArguments.size() != 1) {
        qCCritical(sunplayerLogApplication).noquote() << "--playback-smoke requires exactly one positional media file.";
        return EXIT_FAILURE;
    }
    if (parser.isSet(fullscreenSmokeOption) && positionalArguments.size() != 1) {
        qCCritical(sunplayerLogApplication).noquote()
            << "--fullscreen-smoke requires exactly one positional media file.";
        return EXIT_FAILURE;
    }
    if (positionalArguments.size() > 1) {
        std::fprintf(stderr, "SunPlayer accepts at most one positional media file.\n");
        std::fflush(stderr);
        return EXIT_FAILURE;
    }
#ifdef Q_OS_WIN
    if (parser.isSet(fileDropSmokeOption) && positionalArguments.size() != 1) {
        qCCritical(sunplayerLogApplication).noquote()
            << "--file-drop-smoke requires exactly one positional media file.";
        return EXIT_FAILURE;
    }
    if (parser.isSet(fileDropSmokeOption) &&
        (parser.isSet(verifyQmlOption) || parser.isSet(playbackSmokeOption) || parser.isSet(fullscreenSmokeOption) ||
         parser.isSet(verifyInitialBackgroundOption))) {
        qCCritical(sunplayerLogApplication).noquote()
            << "--file-drop-smoke cannot be combined with another application scenario.";
        return EXIT_FAILURE;
    }
    if (parser.isSet(verifyInitialBackgroundOption) &&
        (parser.isSet(verifyQmlOption) || parser.isSet(playbackSmokeOption) || parser.isSet(fullscreenSmokeOption) ||
         parser.isSet(fileDropSmokeOption) || !positionalArguments.isEmpty())) {
        qCCritical(sunplayerLogApplication).noquote()
            << "--verify-initial-background cannot be combined with a media file or another application scenario.";
        return EXIT_FAILURE;
    }
#endif

    ApplicationLogOptions logOptions{
        .fileEnabled = !parser.isSet(noLogFileOption),
        .debugEnabled = parser.isSet(debugLogOption),
        .filePath = parser.value(logFileOption),
    };
    QString logError;
    std::unique_ptr<ApplicationLog> applicationLog = ApplicationLog::install(logOptions, &logError);
    if (!applicationLog && logOptions.fileEnabled) {
        qCWarning(sunplayerLogApplication).noquote() << logError << "- continuing without a session log file.";
        logOptions.fileEnabled = false;
        logOptions.filePath.clear();
        applicationLog = ApplicationLog::install(logOptions, &logError);
    }
    if (!applicationLog) {
        qCWarning(sunplayerLogApplication).noquote() << "Could not initialize application logging:" << logError;
    } else {
        qCInfo(sunplayerLogApplication).noquote()
            << "event=application.start"
            << "version=" + QCoreApplication::applicationVersion()
            << "debug=" + QString(applicationLog->debugEnabled() ? QStringLiteral("true") : QStringLiteral("false"))
            << "file=" +
                   (applicationLog->filePath().isEmpty() ? QStringLiteral("disabled") : QStringLiteral("enabled"));
        if (!applicationLog->filePath().isEmpty()) {
            qCDebug(sunplayerLogApplication).noquote() << "event=application.log_file"
                                                       << "path=" + applicationLog->filePath();
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
        QString const deployedQmlPath = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
        if (!QFileInfo(deployedQmlPath).isDir()) {
            qCCritical(sunplayerLogApplication).noquote() << "Missing deployed QML directory:" << deployedQmlPath;
            return EXIT_FAILURE;
        }
#endif

        QQmlEngine engine;
#ifdef Q_OS_WIN
        QStringList importPaths{deployedQmlPath};
        for (QString const& path : engine.importPathList()) {
            if ((path.startsWith(QStringLiteral("qrc:")) || path.startsWith(QStringLiteral(":/"))) &&
                !importPaths.contains(path)) {
                importPaths.append(path);
            }
        }
        engine.setImportPathList(importPaths);
#endif
        QQmlComponent component(&engine);
        component.loadFromModule(QStringLiteral("SunPlayer"), QStringLiteral("Main"));
        if (component.isError()) {
            qCCritical(sunplayerLogApplication).noquote() << component.errorString();
            return EXIT_FAILURE;
        }
        qCInfo(sunplayerLogApplication).noquote() << "event=application.verify_qml_complete";
        return EXIT_SUCCESS;
    }

    bool isolatedSettingsRequested = parser.isSet(playbackSmokeOption) || parser.isSet(fullscreenSmokeOption);
#ifdef Q_OS_WIN
    isolatedSettingsRequested = isolatedSettingsRequested || parser.isSet(verifyInitialBackgroundOption) ||
                                parser.isSet(fileDropSmokeOption);
#endif
    std::unique_ptr<QTemporaryDir> isolatedSettingsDirectory;
    std::unique_ptr<ApplicationSettings> applicationSettings;
    if (isolatedSettingsRequested) {
        isolatedSettingsDirectory = std::make_unique<QTemporaryDir>();
        if (!isolatedSettingsDirectory->isValid()) {
            qCCritical(sunplayerLogApplication).noquote()
                << "Could not create isolated application settings:" << isolatedSettingsDirectory->errorString();
            return EXIT_FAILURE;
        }
        applicationSettings =
            std::make_unique<ApplicationSettings>(isolatedSettingsDirectory->filePath(QStringLiteral("settings.ini")));
    } else {
        applicationSettings = std::make_unique<ApplicationSettings>();
    }
    if (parser.isSet(fullscreenSmokeOption)) {
        applicationSettings->setVolume(fullscreenSmokeStoredVolume);
        applicationSettings->setBlankOtherDisplaysInFullscreen(true);
        applicationSettings->sync();
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [&applicationSettings] { applicationSettings->sync(); });

    GraphicsBackendFactory::configureQtQuick();

    SupportController supportController(applicationLog && applicationLog->debugEnabled());

#ifdef Q_OS_LINUX
    std::unique_ptr<LinuxWaylandWindowContext> windowContext;
    std::unique_ptr<PresentationWindow> presentationWindow;
    try {
        windowContext = std::make_unique<LinuxWaylandWindowContext>(app);
        presentationWindow =
            std::make_unique<PresentationWindow>(*applicationSettings, supportController, *windowContext);
    } catch (std::system_error const& error) {
        qCCritical(sunplayerLogPlatform).noquote()
            << "event=application.wayland_connection_failed"
            << "detail=" + QString::fromUtf8(error.what());
        return EXIT_FAILURE;
    } catch (LinuxWaylandStartupError const& error) {
        return showStartupError(
            ApplicationError(ApplicationError::Code::PlatformStartupFailed,
                             QObject::tr("SunPlayer could not initialize the Wayland/Vulkan presentation platform."),
                             QString::fromUtf8(error.what())),
            supportController);
    }
    PresentationWindow& window = *presentationWindow;
#else
    PresentationWindow window(*applicationSettings, supportController);
#endif
    supportController.setParentWindow(&window);
    bool const presentationStarted = window.startPresentation();
    bool const automatedScenario = parser.isSet(playbackSmokeOption) || parser.isSet(fullscreenSmokeOption)
#ifdef Q_OS_WIN
                                   || parser.isSet(fileDropSmokeOption) || parser.isSet(verifyInitialBackgroundOption)
#endif
        ;
    if (!presentationStarted && automatedScenario) {
        return EXIT_FAILURE;
    }
    if (parser.isSet(fullscreenSmokeOption)) {
        bool const expectedRestoredBlanking = window.otherDisplayBlankingAvailable();
        if (!qFuzzyCompare(window.mediaSession().volume(), fullscreenSmokeStoredVolume) ||
            window.blankOtherDisplaysInFullscreen() != expectedRestoredBlanking) {
            qCCritical(sunplayerLogApplication).noquote() << "event=application.fullscreen_smoke_failed"
                                                          << "reason=settings_restore_failed";
            return EXIT_FAILURE;
        }

        // Drive the setter behind the same writable property used by QML.
        window.mediaSession().setVolume(fullscreenSmokeChangedVolume);
        ApplicationSettings::Values const persistedSettings = applicationSettings->load();
        if (!persistedSettings.volume || !qFuzzyCompare(*persistedSettings.volume, fullscreenSmokeChangedVolume) ||
            !persistedSettings.blankOtherDisplaysInFullscreen || !*persistedSettings.blankOtherDisplaysInFullscreen) {
            qCCritical(sunplayerLogApplication).noquote() << "event=application.fullscreen_smoke_failed"
                                                          << "reason=volume_write_through_failed";
            return EXIT_FAILURE;
        }
    }
#ifdef Q_OS_WIN
    if (parser.isSet(verifyInitialBackgroundOption)) {
        return verifyInitialWindowBackground(window) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif
    if (!positionalArguments.isEmpty()
#ifdef Q_OS_WIN
        && !parser.isSet(fileDropSmokeOption)
#endif
    ) {
        QString const absolutePath = QFileInfo(positionalArguments.constFirst()).absoluteFilePath();
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
        QObject::connect(&playbackSmokeDeadline, &QTimer::timeout, &app,
                         [&app, &window, &firstVideoContentRevision, &distinctVideoContentPresented] {
                             MediaSession const& session = window.mediaSession();
                             auto const audio = session.currentAudioPresentation();
                             qCCritical(sunplayerLogApplication).noquote()
                                 << "event=application.playback_smoke_timeout"
                                 << "state=" + QString::number(static_cast<int>(session.state()))
                                 << "hasFrame=" +
                                        QString(session.hasFrame() ? QStringLiteral("true") : QStringLiteral("false"))
                                 << "positionMs=" + QString::number(session.positionMilliseconds())
                                 << "error=" + session.errorMessage();
                             std::fprintf(stderr,
                                          "SunPlayer playback smoke timed out: state=%d, "
                                          "hasFrame=%d, positionMs=%lld, "
                                          "audioPresented=%llu, audioValid=%d, "
                                          "firstVideoRevision=%llu, "
                                          "distinctVideoRevision=%d\n",
                                          static_cast<int>(session.state()), session.hasFrame() ? 1 : 0,
                                          static_cast<long long>(session.positionMilliseconds()),
                                          static_cast<unsigned long long>(audio ? audio->presentedFrames : 0),
                                          audio && audio->valid ? 1 : 0,
                                          static_cast<unsigned long long>(firstVideoContentRevision.value_or(0)),
                                          distinctVideoContentPresented ? 1 : 0);
                             std::fflush(stderr);
                             app.exit(EXIT_FAILURE);
                         });
        QObject::connect(&window, &PresentationWindow::videoFramePresented, &app,
                         [&window, &firstVideoContentRevision, &distinctVideoContentPresented,
                          &firstPresentedPositionMilliseconds](qulonglong contentRevision) {
                             if (!firstVideoContentRevision) {
                                 firstVideoContentRevision = contentRevision;
                             } else if (contentRevision != *firstVideoContentRevision) {
                                 distinctVideoContentPresented = true;
                             }
                             if (firstPresentedPositionMilliseconds < 0) {
                                 firstPresentedPositionMilliseconds = window.mediaSession().positionMilliseconds();
                             }
                         });
        playbackSmokePoll.setInterval(25);
        QObject::connect(&playbackSmokePoll, &QTimer::timeout, &app,
                         [&app, &window, &playbackSmokeDeadline, &playbackSmokePoll, &firstVideoContentRevision,
                          &distinctVideoContentPresented, &firstPresentedPositionMilliseconds] {
                             MediaSession const& session = window.mediaSession();
                             if (session.state() == MediaSession::State::Error) {
                                 qCCritical(sunplayerLogApplication).noquote()
                                     << "event=application.playback_smoke_failed"
                                     << "error=" + session.errorMessage();
                                 QByteArray const error = session.errorMessage().toUtf8();
                                 std::fprintf(stderr, "SunPlayer playback smoke failed: %s\n", error.constData());
                                 std::fflush(stderr);
                                 playbackSmokeDeadline.stop();
                                 playbackSmokePoll.stop();
                                 app.exit(EXIT_FAILURE);
                                 return;
                             }

                             qlonglong const position = session.positionMilliseconds();
                             auto const audio = session.currentAudioPresentation();
                             if (!firstVideoContentRevision || !distinctVideoContentPresented || !session.hasFrame() ||
                                 !audio || !audio->valid || !audio->advancing || audio->presentedFrames == 0 ||
                                 audio->mediaPositionMicroseconds < 1'500'000 || position < 1'500 ||
                                 position < firstPresentedPositionMilliseconds + 250) {
                                 return;
                             }

                             qCInfo(sunplayerLogApplication).noquote()
                                 << "event=application.playback_smoke_complete"
                                 << "positionMs=" + QString::number(position)
                                 << "audioBackend=" + session.audioBackend()
                                 << "audioPresented=" + QString::number(audio->presentedFrames);
                             QByteArray const backend = session.audioBackend().toUtf8();
                             std::fprintf(stdout,
                                          "SunPlayer playback smoke passed: positionMs=%lld, "
                                          "audioBackend=%s, audioPresented=%llu\n",
                                          static_cast<long long>(position), backend.constData(),
                                          static_cast<unsigned long long>(audio->presentedFrames));
                             std::fflush(stdout);
                             playbackSmokeDeadline.stop();
                             playbackSmokePoll.stop();
                             app.exit(EXIT_SUCCESS);
                         });
        playbackSmokeDeadline.start();
        playbackSmokePoll.start();
    }
    if (parser.isSet(fullscreenSmokeOption)) {
        startFullscreenSmokeScenario(app, window, *applicationSettings);
    }
    window.show();
#ifdef Q_OS_WIN
    if (parser.isSet(fileDropSmokeOption)) {
        QString const absolutePath = QFileInfo(positionalArguments.constFirst()).absoluteFilePath();
        startFileDropSmokeScenario(app, window, QUrl::fromLocalFile(absolutePath));
    }
#endif

    int const exitCode = app.exec();
    qCInfo(sunplayerLogApplication).noquote() << "event=application.stop"
                                              << "exitCode=" + QString::number(exitCode);
    return exitCode;
}
