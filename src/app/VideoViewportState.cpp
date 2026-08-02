#include "app/VideoViewportState.h"

#include <cmath>

VideoViewportState::VideoViewportState(QObject *parent)
    : QObject(parent) {}

QRectF VideoViewportState::rect() const {
    return m_rect;
}

bool VideoViewportState::visible() const {
    return m_visible;
}

bool VideoViewportState::isRenderable() const {
    return m_visible && !m_rect.isEmpty();
}

void VideoViewportState::setRect(const QRectF &value) {
    Q_ASSERT(std::isfinite(value.x()));
    Q_ASSERT(std::isfinite(value.y()));
    Q_ASSERT(std::isfinite(value.width()) && value.width() >= 0.0);
    Q_ASSERT(std::isfinite(value.height()) && value.height() >= 0.0);
    if (value == m_rect)
        return;
    m_rect = value;
    emit viewportChanged();
}

void VideoViewportState::setVisible(bool value) {
    if (value == m_visible)
        return;
    m_visible = value;
    emit viewportChanged();
}
