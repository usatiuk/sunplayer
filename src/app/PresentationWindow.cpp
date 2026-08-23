#include "app/PresentationWindow.h"

#include <utility>

#include <QAbstractButton>
#include <QCoreApplication>
#include <QCursor>
#include <QDropEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QProcess>
#include <QPushButton>
#include <QQuickWindow>
#include <QRasterWindow>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "app/ApplicationSettings.h"
#include "app/LocalMediaDrop.h"
#include "app/PresentationSettings.h"
#include "app/SupportController.h"
#include "app/VideoViewportState.h"
#include "app/WindowShortcut.h"
#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsBackendFactory.h"
#include "platform/DisplayStateProvider.h"
#include "platform/PlaybackPowerInhibitor.h"
#include "playback/MediaSession.h"
#include "presentation/PresentationOutputState.h"
#include "presentation/RhiPresentationEngine.h"
#include "video/ActiveVideoSource.h"
#include "video/DiagnosticVideoSource.h"

#ifdef Q_OS_LINUX
#include "platform/linux/LinuxWaylandWindowContext.h"
#endif

namespace {

class OtherDisplayBlankingWindow final : public QRasterWindow {
  public:
    OtherDisplayBlankingWindow(QScreen& targetScreen, QWindow& presentationWindow) {
        setTransientParent(&presentationWindow);
        setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        setScreen(&targetScreen);
        setGeometry(targetScreen.geometry());
        setCursor(QCursor(Qt::BlankCursor));
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(QRect(QPoint(), size()), Qt::black);
    }
};

void parentNativeDialog(QWidget& dialog, QWindow& parent) {
    dialog.winId();
    if (QWindow* const window = dialog.windowHandle()) {
        window->setTransientParent(&parent);
    }
}

} // namespace

#ifdef Q_OS_LINUX
PresentationWindow::PresentationWindow(ApplicationSettings& applicationSettings, SupportController& supportController,
                                       LinuxWaylandWindowContext& windowContext)
    : m_applicationSettings(applicationSettings), m_supportController(supportController),
      m_windowChrome(*this, windowContext.requiresClientSideDecorations()), m_windowContext(windowContext) {
    initialize(windowContext.surfaceSelection().presentationContract(), windowContext.takeDisplayStateProvider(nullptr),
               &windowContext);
#else
PresentationWindow::PresentationWindow(ApplicationSettings& applicationSettings, SupportController& supportController)
    : m_applicationSettings(applicationSettings), m_supportController(supportController), m_windowChrome(*this, false) {
    initialize(PresentationSurfaceContract{}, {}, nullptr);
#endif
}

void PresentationWindow::initialize(PresentationSurfaceContract surfaceContract,
                                    std::unique_ptr<DisplayStateProvider> displayStateProvider,
                                    PresentationSurfaceController* surfaceController) {
    setSurfaceType(GraphicsBackendFactory::windowSurfaceType());
#ifdef Q_OS_LINUX
    m_windowContext.configureWindow(*this);
#endif
    setTitle(tr("SunPlayer"));

    m_outputState = displayStateProvider
                        ? std::make_unique<PresentationOutputState>(std::move(displayStateProvider), nullptr)
                        : std::make_unique<PresentationOutputState>(nullptr);
    m_settings = std::make_unique<PresentationSettings>(nullptr);
    m_diagnosticVideoSource = std::make_unique<DiagnosticVideoSource>(VideoTargetReadback::Disabled);
    m_mediaSession = std::make_unique<MediaSession>(VideoTargetReadback::Disabled);
    m_playbackPowerInhibitor = createPlaybackPowerInhibitor();
    connect(m_mediaSession.get(), &MediaSession::sessionChanged, this,
            [this] { m_playbackPowerInhibitor->reconcile(*m_mediaSession); });
    ApplicationSettings::Values const storedSettings = m_applicationSettings.load();
    if (storedSettings.volume) {
        m_mediaSession->setVolume(*storedSettings.volume);
    }
    if (storedSettings.blankOtherDisplaysInFullscreen && otherDisplayBlankingAvailable()) {
        setBlankOtherDisplaysInFullscreen(*storedSettings.blankOtherDisplaysInFullscreen);
    }
    connect(m_mediaSession.get(), &MediaSession::volumeChanged, this,
            [this] { m_applicationSettings.setVolume(m_mediaSession->volume()); });
    connect(this, &PresentationWindow::blankOtherDisplaysInFullscreenChanged, this,
            [this] { m_applicationSettings.setBlankOtherDisplaysInFullscreen(m_blankOtherDisplaysInFullscreen); });
    m_activeVideoSource = std::make_unique<ActiveVideoSource>(m_mediaSession->videoSource(), *m_diagnosticVideoSource);
    m_videoViewport = std::make_unique<VideoViewportState>(nullptr);
    m_supportController.attach(*m_mediaSession, *m_outputState);
    m_surfaceContract = surfaceContract;
    m_surfaceController = surfaceController;
    connect(this, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
        if (state != Qt::WindowFullScreen) {
            m_restoreMaximizedAfterFullscreen = state == Qt::WindowMaximized;
        }
        QTimer::singleShot(0, this, [this] {
            applyCursorVisibility();
            updateOtherDisplayBlanking();
        });
    });
    auto const scheduleDisplayBlankingUpdate = [this](QScreen*) {
        QTimer::singleShot(0, this, [this] { updateOtherDisplayBlanking(); });
    };
    connect(this, &QWindow::screenChanged, this, scheduleDisplayBlankingUpdate);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, scheduleDisplayBlankingUpdate);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) {
        // Remove any window associated with the departing screen before Qt
        // relocates it to the primary screen, then rebuild after the screen list settles.
        m_otherDisplayBlankingWindows.clear();
        QTimer::singleShot(0, this, [this] { updateOtherDisplayBlanking(); });
    });

    setMinimumSize({760, 560});
    resize(1100, 760);
    // Attaching may create the native window and synchronously deliver events;
    // the lifecycle remains Initializing until startPresentation() owns an engine.
    m_outputState->attach(*this);
}

PresentationWindow::~PresentationWindow() {
    // QRhi and every resource derived from the native surface must be gone
    // before Qt destroys that surface and its QVulkanInstance association.
    Q_ASSERT(m_presentationLifecycle != PresentationLifecycle::Releasing);
    m_presentationLifecycle = PresentationLifecycle::Releasing;
    m_engine.reset();
#ifdef Q_OS_LINUX
    m_outputState.reset();
    m_windowContext.releaseWindow(*this);
#endif
}

bool PresentationWindow::startPresentation() {
    Q_ASSERT(m_presentationLifecycle == PresentationLifecycle::Initializing);
    return createPresentationEngine();
}

void PresentationWindow::openMedia(QUrl const& url) {
    m_mediaSession->openMedia(url);
    emit mediaOpenRequested();
}

MediaSession& PresentationWindow::mediaSession() { return *m_mediaSession; }

MediaSession const& PresentationWindow::mediaSession() const { return *m_mediaSession; }

void PresentationWindow::toggleFullscreen() {
    if (windowState() == Qt::WindowFullScreen) {
        exitFullscreen();
        return;
    }

    m_restoreMaximizedAfterFullscreen = windowState() == Qt::WindowMaximized;
    showFullScreen();
}

void PresentationWindow::exitFullscreen() {
    if (windowState() != Qt::WindowFullScreen) {
        return;
    }

    if (m_restoreMaximizedAfterFullscreen) {
        showMaximized();
    } else {
        showNormal();
    }
}

void PresentationWindow::restartApplication() {
    if (!launchRestart()) {
        QMessageBox dialog(QMessageBox::Critical, tr("Could not restart SunPlayer"),
                           tr("SunPlayer could not start a replacement process. You can report the problem or quit."),
                           QMessageBox::Ok);
        dialog.setWindowModality(Qt::WindowModal);
        parentNativeDialog(dialog, *this);
        dialog.exec();
    }
}

void PresentationWindow::quitApplication() { QCoreApplication::quit(); }

bool PresentationWindow::cursorHidden() const { return m_cursorHidden; }

void PresentationWindow::setCursorHidden(bool hidden) {
    if (hidden == m_cursorHidden) {
        return;
    }
    m_cursorHidden = hidden;
    applyCursorVisibility();
}

bool PresentationWindow::windowShortcutsBlocked() const { return m_windowShortcutsBlocked; }

void PresentationWindow::setWindowShortcutsBlocked(bool blocked) { m_windowShortcutsBlocked = blocked; }

bool PresentationWindow::otherDisplayBlankingAvailable() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool PresentationWindow::blankOtherDisplaysInFullscreen() const { return m_blankOtherDisplaysInFullscreen; }

void PresentationWindow::setBlankOtherDisplaysInFullscreen(bool enabled) {
    if (enabled && !otherDisplayBlankingAvailable()) {
        Q_ASSERT_X(false, "PresentationWindow::setBlankOtherDisplaysInFullscreen",
                   "Other-display blanking is not enabled on this platform");
        return;
    }
    if (enabled == m_blankOtherDisplaysInFullscreen) {
        return;
    }

    m_blankOtherDisplaysInFullscreen = enabled;
    updateOtherDisplayBlanking();
    emit blankOtherDisplaysInFullscreenChanged();
}

WindowChromeController* PresentationWindow::windowChrome() { return &m_windowChrome; }

void PresentationWindow::applyCursorVisibility() {
    if (m_cursorHidden) {
        setCursor(QCursor(Qt::BlankCursor));
    } else {
        unsetCursor();
    }
}

void PresentationWindow::updateOtherDisplayBlanking() {
    m_otherDisplayBlankingWindows.clear();
    if (!otherDisplayBlankingAvailable() || !m_blankOtherDisplaysInFullscreen ||
        windowState() != Qt::WindowFullScreen) {
        return;
    }

    QScreen* const presentationScreen = screen();
    if (!presentationScreen) {
        return;
    }
    for (QScreen* const targetScreen : QGuiApplication::screens()) {
        if (targetScreen == presentationScreen) {
            continue;
        }

        auto blankingWindow = std::make_unique<OtherDisplayBlankingWindow>(*targetScreen, *this);
        blankingWindow->showFullScreen();
        m_otherDisplayBlankingWindows.push_back(std::move(blankingWindow));
    }
}

void PresentationWindow::exposeEvent(QExposeEvent*) {
    if (m_presentationLifecycle != PresentationLifecycle::Active) {
        return;
    }
    Q_ASSERT(m_engine);
    m_engine->handleExposure();
}

void PresentationWindow::resizeEvent(QResizeEvent*) {
    if (m_presentationLifecycle != PresentationLifecycle::Active) {
        return;
    }
    Q_ASSERT(m_engine);
    m_engine->markUiDirty();
}

void PresentationWindow::mousePressEvent(QMouseEvent* event) { forwardMouseEvent(*event); }

void PresentationWindow::mouseReleaseEvent(QMouseEvent* event) { forwardMouseEvent(*event); }

void PresentationWindow::mouseDoubleClickEvent(QMouseEvent* event) { forwardMouseEvent(*event); }

void PresentationWindow::mouseMoveEvent(QMouseEvent* event) { forwardMouseEvent(*event); }

void PresentationWindow::forwardMouseEvent(QMouseEvent& event) {
    if (m_engine) {
        if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
            QMouseEvent mapped(event.type(), event.position(), event.scenePosition(), event.globalPosition(),
                               event.button(), event.buttons(), event.modifiers(), event.source(),
                               event.pointingDevice());
            mapped.setTimestamp(event.timestamp());
            QCoreApplication::sendEvent(quickWindow, &mapped);
        }
    }
}

bool PresentationWindow::playerShortcutContextActive() const {
    return !m_windowShortcutsBlocked && m_activeVideoSource->route() == ActiveVideoSource::Route::Player;
}

bool PresentationWindow::playbackShortcutEnabled() const {
    return playerShortcutContextActive() && m_mediaSession->state() == MediaSession::State::Ready &&
           !m_mediaSession->seeking();
}

void PresentationWindow::togglePlayback() {
    if (m_mediaSession->playRequested()) {
        m_mediaSession->pause();
    } else {
        m_mediaSession->play();
    }
}

void PresentationWindow::wheelEvent(QWheelEvent* event) {
    if (m_engine) {
        if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
            QCoreApplication::sendEvent(quickWindow, event);
        }
    }
}

void PresentationWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F11) {
        if (!event->isAutoRepeat()) {
            toggleFullscreen();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && !m_windowShortcutsBlocked) {
        if (!event->isAutoRepeat()) {
            exitFullscreen();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && playbackShortcutEnabled()) {
        if (!event->isAutoRepeat()) {
            togglePlayback();
        }
        event->accept();
        return;
    }
    std::optional<qlonglong> const relativeSeek = relativeSeekMilliseconds(*event);
    if (relativeSeek && playerShortcutContextActive()) {
        if (!event->isAutoRepeat()) {
            emit relativeSeekRequested(*relativeSeek);
        }
        event->accept();
        return;
    }
    if (m_engine) {
        if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
            QCoreApplication::sendEvent(quickWindow, event);
        }
    }
}

void PresentationWindow::keyReleaseEvent(QKeyEvent* event) {
    std::optional<qlonglong> const relativeSeek = relativeSeekMilliseconds(*event);
    if (event->key() == Qt::Key_F11 || (event->key() == Qt::Key_Escape && !m_windowShortcutsBlocked) ||
        (event->key() == Qt::Key_Space && playbackShortcutEnabled()) ||
        (relativeSeek && playerShortcutContextActive())) {
        event->accept();
        return;
    }
    if (m_engine) {
        if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
            QCoreApplication::sendEvent(quickWindow, event);
        }
    }
}

#ifdef Q_OS_WIN
bool PresentationWindow::nativeEvent(QByteArray const& eventType, void* message, qintptr* result) {
    Q_ASSERT(message);
    Q_ASSERT(result);
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        MSG const& nativeMessage = *static_cast<MSG const*>(message);
        bool const presentationOwnsClientArea = m_engine && m_engine->hasPresentedFrame();
        if (nativeMessage.message == WM_ERASEBKGND && !presentationOwnsClientArea) {
            RECT clientRect{};
            HDC const deviceContext = reinterpret_cast<HDC>(nativeMessage.wParam);
            HBRUSH const blackBrush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            if (deviceContext && blackBrush && GetClientRect(nativeMessage.hwnd, &clientRect) &&
                FillRect(deviceContext, &clientRect, blackBrush)) {
                *result = 1;
                return true;
            }
            qCWarning(sunplayerLogApplication, "Could not paint the pre-presentation Windows client background");
        }
    }
    return QWindow::nativeEvent(eventType, message, result);
}
#endif

bool PresentationWindow::event(QEvent* event) {
    // Native-surface teardown must always reach a live engine. A terminal
    // presentation error suspends rendering before destroying the failed
    // engine on the next event-loop turn, and the surface may disappear in
    // that interval.
    if (event->type() == QEvent::PlatformSurface && m_engine) {
        auto* const surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
            m_engine->handleSurfaceCreated();
        } else if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            m_engine->releaseSwapChain();
        }
    }
    if (m_presentationLifecycle != PresentationLifecycle::Active) {
        return QWindow::event(event);
    }
    Q_ASSERT(m_engine);
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::Drop: {
        auto* const dropEvent = static_cast<QDropEvent*>(event);
        std::optional<QUrl> const url = singleLocalMediaDropUrl(dropEvent->mimeData(), dropEvent->possibleActions());
        if (!url) {
            dropEvent->ignore();
            return true;
        }

        dropEvent->setDropAction(Qt::CopyAction);
        dropEvent->accept();
        if (event->type() == QEvent::Drop) {
            openMedia(*url);
        }
        return true;
    }
    case QEvent::UpdateRequest:
        m_engine->render();
        return true;
    case QEvent::DevicePixelRatioChange:
        m_engine->markUiDirty();
        break;
    default:
        break;
    }
    return QWindow::event(event);
}

bool PresentationWindow::createPresentationEngine() {
    Q_ASSERT(!m_engine);
    Q_ASSERT(m_presentationLifecycle == PresentationLifecycle::Initializing ||
             m_presentationLifecycle == PresentationLifecycle::Suspended);
    m_presentationLifecycle = PresentationLifecycle::Initializing;
    m_engine = std::make_unique<RhiPresentationEngine>(*this, *m_outputState, *m_settings, *m_activeVideoSource,
                                                       *m_diagnosticVideoSource, *m_mediaSession, *m_videoViewport,
                                                       m_supportController, m_surfaceContract, m_surfaceController);
    connect(m_engine.get(), &RhiPresentationEngine::videoFramePresented, this,
            &PresentationWindow::videoFramePresented);
    connect(m_engine.get(), &RhiPresentationEngine::terminalError, this, &PresentationWindow::handlePresentationError);
    if (!m_engine->start()) {
        return false;
    }
    m_presentationLifecycle = PresentationLifecycle::Active;
    m_supportController.setApplicationError(std::nullopt);
    if (isExposed()) {
        m_engine->handleExposure();
    }
    return true;
}

void PresentationWindow::handlePresentationError(ApplicationError error) {
    if (m_presentationLifecycle == PresentationLifecycle::Releasing ||
        m_presentationLifecycle == PresentationLifecycle::Suspended) {
        return;
    }
    m_presentationLifecycle = PresentationLifecycle::Suspended;
    m_supportController.setApplicationError(error);
    QTimer::singleShot(0, this, [this, error = std::move(error)]() mutable {
        if (m_presentationLifecycle != PresentationLifecycle::Suspended) {
            return;
        }
        m_engine.reset();
        showPresentationErrorDialog(std::move(error));
    });
}

void PresentationWindow::showPresentationErrorDialog(ApplicationError error) {
    while (m_presentationLifecycle == PresentationLifecycle::Suspended) {
        QMessageBox dialog(QMessageBox::Critical, tr("SunPlayer presentation error"), error.userMessage(),
                           QMessageBox::NoButton);
        dialog.setInformativeText(tr("Error code: %1").arg(error.stableCode()));
        dialog.setDetailedText(error.technicalDetail());
        dialog.setWindowModality(Qt::WindowModal);
        parentNativeDialog(dialog, *this);

        ApplicationErrorActions const actions = error.suggestedActions();
        QAbstractButton* retry = actions.testFlag(ApplicationErrorAction::Retry)
                                     ? dialog.addButton(tr("Retry"), QMessageBox::AcceptRole)
                                     : nullptr;
        QAbstractButton* restart = actions.testFlag(ApplicationErrorAction::Restart)
                                       ? dialog.addButton(tr("Restart"), QMessageBox::ActionRole)
                                       : nullptr;
        QAbstractButton* report = actions.testFlag(ApplicationErrorAction::ReportBug)
                                      ? dialog.addButton(tr("Report a bug…"), QMessageBox::HelpRole)
                                      : nullptr;
        QAbstractButton* quit = actions.testFlag(ApplicationErrorAction::Quit)
                                    ? dialog.addButton(tr("Quit"), QMessageBox::RejectRole)
                                    : nullptr;
        dialog.exec();
        QAbstractButton* const selected = dialog.clickedButton();
        if (retry && selected == retry) {
            retryPresentation();
            return;
        }
        if (restart && selected == restart) {
            if (launchRestart()) {
                return;
            }
            error = ApplicationError(ApplicationError::Code::RestartFailed, tr("SunPlayer could not restart."),
                                     QStringLiteral("QProcess::startDetached rejected the replacement process"));
            continue;
        }
        if (report && selected == report) {
            m_supportController.reportBug();
            continue;
        }
        if ((quit && selected == quit) || !selected) {
            QCoreApplication::quit();
            return;
        }
    }
}

void PresentationWindow::retryPresentation() {
    if (m_presentationLifecycle != PresentationLifecycle::Suspended) {
        return;
    }
    createPresentationEngine();
}

bool PresentationWindow::launchRestart() {
    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), {})) {
        QCoreApplication::quit();
        return true;
    }
    m_supportController.setApplicationError(
        ApplicationError(ApplicationError::Code::RestartFailed, tr("SunPlayer could not restart."),
                         QStringLiteral("QProcess::startDetached rejected the replacement process")));
    return false;
}
