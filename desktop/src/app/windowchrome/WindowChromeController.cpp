#include "app/windowchrome/WindowChromeController.h"

#include <QWindow>

WindowChromeController::WindowChromeController(
        QWindow &window,
        bool enabled,
        QObject *parent)
    : QObject(parent),
      m_window(window),
      m_enabled(enabled) {
    if (m_enabled)
        m_window.setFlags(m_window.flags() | Qt::FramelessWindowHint);
    connect(
        &m_window,
        &QWindow::windowStateChanged,
        this,
        &WindowChromeController::stateChanged);
}

bool WindowChromeController::enabled() const {
    return m_enabled;
}

bool WindowChromeController::fullscreen() const {
    return m_window.windowState() == Qt::WindowFullScreen;
}

bool WindowChromeController::maximized() const {
    return m_window.windowState() == Qt::WindowMaximized;
}

void WindowChromeController::minimize() {
    m_window.showMinimized();
}

void WindowChromeController::toggleMaximized() {
    if (maximized())
        m_window.showNormal();
    else
        m_window.showMaximized();
}

void WindowChromeController::close() {
    m_window.close();
}

bool WindowChromeController::beginSystemMove() {
    Q_ASSERT(m_enabled);
    return m_window.startSystemMove();
}

bool WindowChromeController::beginSystemResize(int edgeMask) {
    Q_ASSERT(m_enabled);
    const Qt::Edges edges = Qt::Edges::fromInt(edgeMask);
    const bool valid = edges == Qt::LeftEdge
        || edges == Qt::RightEdge
        || edges == Qt::TopEdge
        || edges == Qt::BottomEdge
        || edges == (Qt::TopEdge | Qt::LeftEdge)
        || edges == (Qt::TopEdge | Qt::RightEdge)
        || edges == (Qt::BottomEdge | Qt::LeftEdge)
        || edges == (Qt::BottomEdge | Qt::RightEdge);
    Q_ASSERT(valid);
    return m_window.startSystemResize(edges);
}
