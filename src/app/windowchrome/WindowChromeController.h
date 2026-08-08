#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QWindow;

// Public Qt window operations used by optional application-drawn chrome.
// The compositor remains responsible for movement, resizing, and state changes.
class WindowChromeController final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool enabled READ enabled CONSTANT)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY stateChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY stateChanged)

  public:
    WindowChromeController(QWindow& window, bool enabled, QObject* parent = nullptr);

    bool enabled() const;
    bool fullscreen() const;
    bool maximized() const;

    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximized();
    Q_INVOKABLE void close();
    Q_INVOKABLE bool beginSystemMove();
    Q_INVOKABLE bool beginSystemResize(int edgeMask);

  signals:
    void stateChanged();

  private:
    QWindow& m_window;
    bool m_enabled = false;
};
