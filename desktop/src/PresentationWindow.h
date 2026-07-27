#pragma once

#include <memory>

#include <QWindow>

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
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
