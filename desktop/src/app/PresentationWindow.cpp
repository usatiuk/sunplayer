#include "app/PresentationWindow.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QWheelEvent>

#include "app/PresentationSettings.h"
#include "app/VideoViewportState.h"
#include "graphics/GraphicsBackendFactory.h"
#include "playback/MediaSession.h"
#include "presentation/PresentationOutputState.h"
#include "presentation/RhiPresentationEngine.h"
#include "video/ActiveVideoSource.h"
#include "video/DiagnosticVideoSource.h"

PresentationWindow::PresentationWindow() {
    setSurfaceType(GraphicsBackendFactory::windowSurfaceType());
    setTitle(tr("Sunroom"));

    m_outputState = std::make_unique<PresentationOutputState>(nullptr);
    m_settings = std::make_unique<PresentationSettings>(nullptr);
    m_diagnosticVideoSource =
        std::make_unique<DiagnosticVideoSource>(
            VideoTargetReadback::Disabled);
    m_mediaSession = std::make_unique<MediaSession>(
        VideoTargetReadback::Disabled);
    m_activeVideoSource = std::make_unique<ActiveVideoSource>(
        m_mediaSession->videoSource(),
        *m_diagnosticVideoSource);
    m_videoViewport = std::make_unique<VideoViewportState>(nullptr);
    m_engine = std::make_unique<RhiPresentationEngine>(
        *this,
        *m_outputState,
        *m_settings,
        *m_activeVideoSource,
        *m_diagnosticVideoSource,
        *m_mediaSession,
        *m_videoViewport);

    setMinimumSize({760, 560});
    resize(1100, 760);
    // Attaching may create the native window and synchronously deliver events.
    // The engine must already exist before display monitoring starts.
    m_outputState->attach(*this);
}

PresentationWindow::~PresentationWindow() = default;

void PresentationWindow::openMedia(const QUrl &url) {
    m_mediaSession->openMedia(url);
}

void PresentationWindow::exposeEvent(QExposeEvent *) {
    m_engine->handleExposure();
}

void PresentationWindow::resizeEvent(QResizeEvent *) {
    m_engine->markUiDirty();
}

void PresentationWindow::mousePressEvent(QMouseEvent *event) {
    if (QQuickWindow *quickWindow = m_engine->quickWindow()) {
        QMouseEvent mapped(
            event->type(), event->position(), event->globalPosition(),
            event->button(), event->buttons(), event->modifiers());
        QCoreApplication::sendEvent(quickWindow, &mapped);
    }
}

void PresentationWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (QQuickWindow *quickWindow = m_engine->quickWindow()) {
        QMouseEvent mapped(
            event->type(), event->position(), event->globalPosition(),
            event->button(), event->buttons(), event->modifiers());
        QCoreApplication::sendEvent(quickWindow, &mapped);
    }
}

void PresentationWindow::mouseMoveEvent(QMouseEvent *event) {
    if (QQuickWindow *quickWindow = m_engine->quickWindow()) {
        QMouseEvent mapped(
            event->type(), event->position(), event->globalPosition(),
            event->button(), event->buttons(), event->modifiers());
        QCoreApplication::sendEvent(quickWindow, &mapped);
    }
}

void PresentationWindow::wheelEvent(QWheelEvent *event) {
    if (QQuickWindow *quickWindow = m_engine->quickWindow())
        QCoreApplication::sendEvent(quickWindow, event);
}

void PresentationWindow::keyPressEvent(QKeyEvent *event) {
    if (QQuickWindow *quickWindow = m_engine->quickWindow())
        QCoreApplication::sendEvent(quickWindow, event);
}

void PresentationWindow::keyReleaseEvent(QKeyEvent *event) {
    if (QQuickWindow *quickWindow = m_engine->quickWindow())
        QCoreApplication::sendEvent(quickWindow, event);
}

bool PresentationWindow::event(QEvent *event) {
    switch (event->type()) {
    case QEvent::UpdateRequest:
        m_engine->render();
        return true;
    case QEvent::DevicePixelRatioChange:
        m_engine->markUiDirty();
        break;
    case QEvent::PlatformSurface:
        if (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            m_engine->releaseSwapChain();
        }
        break;
    default:
        break;
    }
    return QWindow::event(event);
}
