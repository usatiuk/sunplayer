#pragma once

#include <memory>

#include <QWindow>

// Native window and event boundary for the current application shell.
class PresentationWindow final : public QWindow {
    Q_OBJECT

public:
    PresentationWindow();
    ~PresentationWindow() override;

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    std::unique_ptr<class PresentationOutputState> m_outputState;
    std::unique_ptr<class PresentationSettings> m_settings;
    std::unique_ptr<class DiagnosticVideoSource> m_videoSource;
    std::unique_ptr<class RhiPresentationEngine> m_engine;
};
