#pragma once

#include <array>
#include <functional>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QSize>

class QWindow;
struct wl_subcompositor;
struct wl_subsurface;
namespace QtWayland {
class wl_surface;
}
namespace QtWaylandClient {
class QWaylandWindow;
}

// A passive SHM child of the Qt-owned CSD surface. Never changes parent geometry
// or commits the parent; Qt's next presentation latches the synchronized child.
class WaylandWindowShadow final : public QObject {
  public:
    explicit WaylandWindowShadow(QWindow& window, std::function<void(bool)> tilingChanged = {});
    ~WaylandWindowShadow() override;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct Buffer;
    void attach();
    void detach();
    void clearSurface();
    void reconcile();
    void hide();

    QWindow& m_window;
    std::function<void(bool)> m_tilingChanged;
    QPointer<QtWaylandClient::QWaylandWindow> m_nativeWindow;
    wl_subcompositor* m_subcompositor = nullptr;
    std::unique_ptr<QtWayland::wl_surface> m_surface;
    wl_subsurface* m_subsurface = nullptr;
    std::array<std::unique_ptr<Buffer>, 2> m_buffers;
    Buffer* m_attached = nullptr;
    QSize m_unavailableBody;
    int m_unavailableScale = 0;
};
