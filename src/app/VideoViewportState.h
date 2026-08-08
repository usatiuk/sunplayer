#pragma once

#include <QObject>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

// Application-facing description of the active page's video region in root
// logical coordinates. Page selection remains a QML concern.
class VideoViewportState final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("VideoViewportState is owned by the application")

    Q_PROPERTY(QRectF rect READ rect WRITE setRect NOTIFY viewportChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY viewportChanged)

  public:
    explicit VideoViewportState(QObject* parent);

    QRectF rect() const;
    bool visible() const;
    bool isRenderable() const;

    void setRect(QRectF const& value);
    void setVisible(bool value);

  signals:
    void viewportChanged();

  private:
    QRectF m_rect{0.0, 0.0, 0.0, 0.0};
    bool m_visible = false;
};
