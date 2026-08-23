#pragma once

#include <memory>
#include <vector>

#include <QUrl>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include "app/windowchrome/WindowChromeController.h"
#include "presentation/PresentationSurfaceContract.h"

class DisplayStateProvider;
class ApplicationSettings;
class ApplicationError;
class SupportController;

#ifdef Q_OS_LINUX
class LinuxWaylandWindowContext;
#endif

// Native window and event boundary for the current application shell.
class PresentationWindow : public QWindow {
    Q_OBJECT
    Q_PROPERTY(bool cursorHidden READ cursorHidden WRITE setCursorHidden)
    Q_PROPERTY(bool windowShortcutsBlocked READ windowShortcutsBlocked WRITE setWindowShortcutsBlocked)
    Q_PROPERTY(bool otherDisplayBlankingAvailable READ otherDisplayBlankingAvailable CONSTANT)
    Q_PROPERTY(bool blankOtherDisplaysInFullscreen READ blankOtherDisplaysInFullscreen WRITE
                   setBlankOtherDisplaysInFullscreen NOTIFY blankOtherDisplaysInFullscreenChanged)
    Q_PROPERTY(WindowChromeController* windowChrome READ windowChrome CONSTANT)

  public:
#ifdef Q_OS_LINUX
    PresentationWindow(ApplicationSettings& applicationSettings, SupportController& supportController,
                       LinuxWaylandWindowContext& windowContext);
#else
    PresentationWindow(ApplicationSettings& applicationSettings, SupportController& supportController);
#endif
    ~PresentationWindow() override;

    bool startPresentation();
    Q_INVOKABLE void openMedia(QUrl const& url);
    class MediaSession& mediaSession();
    const class MediaSession& mediaSession() const;

    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void exitFullscreen();
    Q_INVOKABLE void restartApplication();
    Q_INVOKABLE void quitApplication();

    bool cursorHidden() const;
    void setCursorHidden(bool hidden);
    bool windowShortcutsBlocked() const;
    void setWindowShortcutsBlocked(bool blocked);
    bool otherDisplayBlankingAvailable() const;
    bool blankOtherDisplaysInFullscreen() const;
    void setBlankOtherDisplaysInFullscreen(bool enabled);
    WindowChromeController* windowChrome();

  signals:
    void videoFramePresented(qulonglong contentRevision);
    void blankOtherDisplaysInFullscreenChanged();
    void relativeSeekRequested(qlonglong milliseconds);
    void mediaOpenRequested();

  protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(QByteArray const& eventType, void* message, qintptr* result) override;
#endif
    bool event(QEvent* event) override;

  private:
    enum class PresentationLifecycle {
        Initializing,
        Active,
        Suspended,
        Releasing,
    };

    void initialize(PresentationSurfaceContract surfaceContract,
                    std::unique_ptr<DisplayStateProvider> displayStateProvider,
                    PresentationSurfaceController* surfaceController);
    void applyCursorVisibility();
    void updateOtherDisplayBlanking();
    void forwardMouseEvent(QMouseEvent& event);
    bool playerShortcutContextActive() const;
    bool playbackShortcutEnabled() const;
    void togglePlayback();
    bool createPresentationEngine();
    void handlePresentationError(ApplicationError error);
    void showPresentationErrorDialog(ApplicationError error);
    void retryPresentation();
    bool launchRestart();

    ApplicationSettings& m_applicationSettings;
    SupportController& m_supportController;
    std::unique_ptr<class PresentationOutputState> m_outputState;
    std::unique_ptr<class PresentationSettings> m_settings;
    std::unique_ptr<class DiagnosticVideoSource> m_diagnosticVideoSource;
    std::unique_ptr<class MediaSession> m_mediaSession;
    std::unique_ptr<class PlaybackPowerInhibitor> m_playbackPowerInhibitor;
    std::unique_ptr<class ActiveVideoSource> m_activeVideoSource;
    std::unique_ptr<class VideoViewportState> m_videoViewport;
    std::unique_ptr<class RhiPresentationEngine> m_engine;
    PresentationSurfaceContract m_surfaceContract;
    PresentationSurfaceController* m_surfaceController = nullptr;
    std::vector<std::unique_ptr<QWindow>> m_otherDisplayBlankingWindows;
    WindowChromeController m_windowChrome;
#ifdef Q_OS_LINUX
    LinuxWaylandWindowContext& m_windowContext;
#endif
    bool m_restoreMaximizedAfterFullscreen = false;
    bool m_cursorHidden = false;
    bool m_windowShortcutsBlocked = false;
    bool m_blankOtherDisplaysInFullscreen = false;
    PresentationLifecycle m_presentationLifecycle = PresentationLifecycle::Initializing;
};

struct PresentationWindowQmlForeign {
    Q_GADGET
    QML_FOREIGN(PresentationWindow)
    QML_NAMED_ELEMENT(WindowCommands)
    QML_UNCREATABLE("The application supplies the native window commands")
};
