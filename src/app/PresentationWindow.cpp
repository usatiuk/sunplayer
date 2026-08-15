#include "app/PresentationWindow.h"

#include <utility>

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QQuickWindow>
#include <QRasterWindow>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "app/PresentationSettings.h"
#include "app/VideoViewportState.h"
#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsBackendFactory.h"
#include "platform/DisplayStateProvider.h"
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

} // namespace

#ifdef Q_OS_LINUX
PresentationWindow::PresentationWindow(LinuxWaylandWindowContext& windowContext)
    : m_windowChrome(*this, windowContext.requiresClientSideDecorations()), m_windowContext(windowContext) {
    initialize(windowContext.surfaceSelection().presentationContract(), windowContext.takeDisplayStateProvider(nullptr),
               &windowContext);
#else
PresentationWindow::PresentationWindow() : m_windowChrome(*this, false) {
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
    setTitle(tr("Sunroom"));

    m_outputState = displayStateProvider
                        ? std::make_unique<PresentationOutputState>(std::move(displayStateProvider), nullptr)
                        : std::make_unique<PresentationOutputState>(nullptr);
    m_settings = std::make_unique<PresentationSettings>(nullptr);
    m_diagnosticVideoSource = std::make_unique<DiagnosticVideoSource>(VideoTargetReadback::Disabled);
    m_mediaSession = std::make_unique<MediaSession>(VideoTargetReadback::Disabled);
    m_activeVideoSource = std::make_unique<ActiveVideoSource>(m_mediaSession->videoSource(), *m_diagnosticVideoSource);
    m_videoViewport = std::make_unique<VideoViewportState>(nullptr);
    m_engine = std::make_unique<RhiPresentationEngine>(*this, *m_outputState, *m_settings, *m_activeVideoSource,
                                                       *m_diagnosticVideoSource, *m_mediaSession, *m_videoViewport,
                                                       surfaceContract, surfaceController);
    connect(m_engine.get(), &RhiPresentationEngine::videoFramePresented, this,
            &PresentationWindow::videoFramePresented);
    m_presentationLifecycle = PresentationLifecycle::Active;
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
    // Attaching may create the native window and synchronously deliver events.
    // The engine must already exist before display monitoring starts.
    m_outputState->attach(*this);
}

PresentationWindow::~PresentationWindow() {
    // QRhi and every resource derived from the native surface must be gone
    // before Qt destroys that surface and its QVulkanInstance association.
    Q_ASSERT(m_presentationLifecycle == PresentationLifecycle::Active);
    m_presentationLifecycle = PresentationLifecycle::Releasing;
    m_engine.reset();
#ifdef Q_OS_LINUX
    m_outputState.reset();
    m_windowContext.releaseWindow(*this);
#endif
}

void PresentationWindow::openMedia(QUrl const& url) { m_mediaSession->openMedia(url); }

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
    if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
        QMouseEvent mapped(event.type(), event.position(), event.scenePosition(), event.globalPosition(),
                           event.button(), event.buttons(), event.modifiers(), event.source(), event.pointingDevice());
        mapped.setTimestamp(event.timestamp());
        QCoreApplication::sendEvent(quickWindow, &mapped);
    }
}

bool PresentationWindow::playbackShortcutEnabled() const {
    return !m_windowShortcutsBlocked && m_activeVideoSource->route() == ActiveVideoSource::Route::Player &&
           m_mediaSession->state() == MediaSession::State::Ready && !m_mediaSession->seeking();
}

void PresentationWindow::togglePlayback() {
    if (m_mediaSession->playRequested()) {
        m_mediaSession->pause();
    } else {
        m_mediaSession->play();
    }
}

void PresentationWindow::wheelEvent(QWheelEvent* event) {
    if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
        QCoreApplication::sendEvent(quickWindow, event);
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
    if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
        QCoreApplication::sendEvent(quickWindow, event);
    }
}

void PresentationWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F11 || (event->key() == Qt::Key_Escape && !m_windowShortcutsBlocked) ||
        (event->key() == Qt::Key_Space && playbackShortcutEnabled())) {
        event->accept();
        return;
    }
    if (QQuickWindow* quickWindow = m_engine->quickWindow()) {
        QCoreApplication::sendEvent(quickWindow, event);
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
            qCWarning(sunroomLogApplication, "Could not paint the pre-presentation Windows client background");
        }
    }
    return QWindow::nativeEvent(eventType, message, result);
}
#endif

bool PresentationWindow::event(QEvent* event) {
    if (m_presentationLifecycle != PresentationLifecycle::Active) {
        return QWindow::event(event);
    }
    Q_ASSERT(m_engine);
    switch (event->type()) {
    case QEvent::UpdateRequest:
        m_engine->render();
        return true;
    case QEvent::DevicePixelRatioChange:
        m_engine->markUiDirty();
        break;
    case QEvent::PlatformSurface:
        if (static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
            m_engine->handleSurfaceCreated();
        } else if (static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
                   QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            m_engine->releaseSwapChain();
        }
        break;
    default:
        break;
    }
    return QWindow::event(event);
}
