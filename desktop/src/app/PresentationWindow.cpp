#include "app/PresentationWindow.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QWheelEvent>

#include "app/PresentationSettings.h"
#include "presentation/PresentationOutputState.h"
#include "presentation/RhiPresentationEngine.h"

PresentationWindow::PresentationWindow() {
    setSurfaceType(QSurface::Direct3DSurface);
    setTitle(tr("Sunroom — RHI / HDR"));

    m_outputState = std::make_unique<PresentationOutputState>();
    m_settings = std::make_unique<PresentationSettings>();
    m_engine = std::make_unique<RhiPresentationEngine>(
        *this, *m_outputState, *m_settings);

    setMinimumSize({760, 560});
    resize(1100, 760);
    // Attaching may create the native window and synchronously deliver events.
    // The engine must already exist before display monitoring starts.
    m_outputState->attach(*this);
}

PresentationWindow::~PresentationWindow() = default;

void PresentationWindow::exposeEvent(QExposeEvent *) {
    m_engine->handleExposure();
}

void PresentationWindow::resizeEvent(QResizeEvent *) {
    m_engine->markUiDirty();
    m_engine->markCanvasDirty();
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
        m_engine->markCanvasDirty();
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
