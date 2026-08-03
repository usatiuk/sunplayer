#pragma once

#include <memory>

#include <QUrl>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include "app/windowchrome/WindowChromeController.h"
#include "presentation/PresentationSurfaceContract.h"

class DisplayStateProvider;

#ifdef Q_OS_LINUX
class LinuxWaylandWindowContext;
#endif

// Native window and event boundary for the current application shell.
class PresentationWindow : public QWindow {
    Q_OBJECT
    Q_PROPERTY(bool cursorHidden READ cursorHidden WRITE setCursorHidden)
    Q_PROPERTY(bool windowShortcutsBlocked
                   READ windowShortcutsBlocked
                   WRITE setWindowShortcutsBlocked)
    Q_PROPERTY(WindowChromeController *windowChrome
                   READ windowChrome
                   CONSTANT)

public:
#ifdef Q_OS_LINUX
    explicit PresentationWindow(LinuxWaylandWindowContext &windowContext);
#else
    PresentationWindow();
#endif
    ~PresentationWindow() override;

    void openMedia(const QUrl &url);
    const class MediaSession &mediaSession() const;

    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void exitFullscreen();

    bool cursorHidden() const;
    void setCursorHidden(bool hidden);
    bool windowShortcutsBlocked() const;
    void setWindowShortcutsBlocked(bool blocked);
    WindowChromeController *windowChrome();

signals:
    void videoFramePresented(qulonglong contentRevision);

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    enum class PresentationLifecycle {
        Initializing,
        Active,
        Releasing,
    };

    void initialize(
        PresentationSurfaceContract surfaceContract,
        std::unique_ptr<DisplayStateProvider> displayStateProvider,
        PresentationSurfaceController *surfaceController);
    void applyCursorVisibility();
    void forwardMouseEvent(QMouseEvent &event);
    bool playbackShortcutEnabled() const;
    void togglePlayback();

    std::unique_ptr<class PresentationOutputState> m_outputState;
    std::unique_ptr<class PresentationSettings> m_settings;
    std::unique_ptr<class DiagnosticVideoSource> m_diagnosticVideoSource;
    std::unique_ptr<class MediaSession> m_mediaSession;
    std::unique_ptr<class ActiveVideoSource> m_activeVideoSource;
    std::unique_ptr<class VideoViewportState> m_videoViewport;
    std::unique_ptr<class RhiPresentationEngine> m_engine;
    WindowChromeController m_windowChrome;
#ifdef Q_OS_LINUX
    LinuxWaylandWindowContext &m_windowContext;
#endif
    bool m_restoreMaximizedAfterFullscreen = false;
    bool m_cursorHidden = false;
    bool m_windowShortcutsBlocked = false;
    PresentationLifecycle m_presentationLifecycle =
        PresentationLifecycle::Initializing;
};

struct PresentationWindowQmlForeign {
    Q_GADGET
    QML_FOREIGN(PresentationWindow)
    QML_NAMED_ELEMENT(WindowCommands)
    QML_UNCREATABLE("The application supplies the native window commands")
};
