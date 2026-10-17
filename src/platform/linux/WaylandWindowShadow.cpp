#include "platform/linux/WaylandWindowShadow.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <vector>

#include <QEvent>
#include <QImage>
#include <QPlatformSurfaceEvent>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QWindow>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandshm_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include "diagnostics/LogCategories.h"

namespace {
constexpr int extent = 24;

std::vector<std::array<float, 2>> coverage(int pixels, int body, int scale) {
    std::vector<std::array<float, 2>> result(pixels);
    for (int i = 0; i < pixels; ++i) {
        float const p = (i + 0.5f) / scale - extent;
        auto const layer = [p, body](float sigma, float spread) {
            float const k = std::sqrt(2.0f) * sigma;
            return 0.5f * (std::erf((p + spread) / k) - std::erf((p - body - spread) / k));
        };
        // libadwaita's centered 14px/5px-spread and 5px/2px-spread
        // shadows. CSS blur radius is twice Gaussian sigma.
        result[i] = {layer(7.0f, 5.0f), layer(2.5f, 2.0f)};
    }
    return result;
}

void paintShadow(QImage& image, QSize body, int scale) {
    image.fill(Qt::transparent);
    int const margin = extent * scale;
    int const right = margin + body.width() * scale;
    int const bottom = margin + body.height() * scale;
    auto const horizontal = coverage(image.width(), body.width(), scale);
    auto const vertical = coverage(image.height(), body.height(), scale);
    for (int y = 0; y < image.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (y >= margin && y < bottom && x >= margin && x < right) {
                x = right - 1;
                continue;
            }
            float const ambient = 0.15f * horizontal[x][0] * vertical[y][0];
            float const contact = 0.10f * horizontal[x][1] * vertical[y][1];
            int const alpha = qRound(255.0f * (ambient + contact * (1.0f - ambient)));
            row[x] = qRgba(0, 0, 0, alpha);
        }
    }
}
} // namespace

struct WaylandWindowShadow::Buffer final : QtWayland::wl_buffer {
    Buffer(WaylandWindowShadow& owner, QSize body, int scale) : owner(owner), body(body), scale(scale) {
        QSize const size = (body + QSize(2 * extent, 2 * extent)) * scale;
        qint64 const bytes = qint64(size.width()) * size.height() * 4;
        if (bytes <= 0 || bytes > std::numeric_limits<int>::max()) {
            qCFatal(sunplayerLogPlatform, "Invalid Wayland shadow buffer size");
        }
        QString const runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (runtime.isEmpty()) {
            qCWarning(sunplayerLogPlatform,
                      "event=wayland.shadow_unavailable reason=storage error=no-runtime-directory");
            return;
        }
        QTemporaryFile file(runtime + QStringLiteral("/sunplayer-shadow-XXXXXX"));
        if (!file.open() || !file.resize(bytes)) {
            qCWarning(sunplayerLogPlatform, "event=wayland.shadow_unavailable reason=storage error=%s",
                      qPrintable(file.errorString()));
            return;
        }
        // ftruncate alone leaves sparse tmpfs pages: writing a full runtime
        // filesystem could otherwise SIGBUS after an apparently valid mmap.
        int const allocationError = posix_fallocate(file.handle(), 0, bytes);
        if (allocationError != 0) {
            qCWarning(sunplayerLogPlatform, "event=wayland.shadow_unavailable reason=storage error=%s",
                      std::strerror(allocationError));
            return;
        }
        uchar* const memory = file.map(0, bytes);
        if (!memory) {
            qCWarning(sunplayerLogPlatform, "event=wayland.shadow_unavailable reason=mapping error=%s",
                      qPrintable(file.errorString()));
            return;
        }
        QImage image(memory, size.width(), size.height(), QImage::Format_ARGB32_Premultiplied);
        paintShadow(image, body, scale);
        auto* const shm = owner.m_nativeWindow->display()->shm();
        Q_ASSERT(shm);
        wl_shm_pool* const pool = wl_shm_create_pool(shm->object(), file.handle(), int(bytes));
        Q_CHECK_PTR(pool);
        init(wl_shm_pool_create_buffer(pool, 0, size.width(), size.height(), size.width() * 4, WL_SHM_FORMAT_ARGB8888));
        Q_CHECK_PTR(object());
        wl_shm_pool_destroy(pool);
        // The server retains the passed fd/mapping. This immutable buffer's
        // storage is never rewritten, including while a commit is pending.
        if (!file.unmap(memory)) {
            qCFatal(sunplayerLogPlatform, "Could not unmap Wayland shadow storage");
        }
    }

    ~Buffer() override {
        if (isInitialized()) {
            destroy();
        }
    }

    void buffer_release() override {
        busy = false;
        owner.m_window.requestUpdate();
    }

    WaylandWindowShadow& owner;
    QSize const body;
    int const scale;
    bool busy = false;
};

WaylandWindowShadow::WaylandWindowShadow(QWindow& window, std::function<void(bool)> tilingChanged)
    : m_window(window), m_tilingChanged(std::move(tilingChanged)) {
    m_window.installEventFilter(this);
    attach();
}

WaylandWindowShadow::~WaylandWindowShadow() {
    m_window.removeEventFilter(this);
    detach();
}

void WaylandWindowShadow::attach() {
    Q_ASSERT(!m_nativeWindow);
    m_nativeWindow = static_cast<QtWaylandClient::QWaylandWindow*>(m_window.handle());
    Q_ASSERT(m_nativeWindow);
    connect(m_nativeWindow, &QNativeInterface::Private::QWaylandWindow::surfaceCreated, this, [this] { reconcile(); });
    connect(m_nativeWindow, &QNativeInterface::Private::QWaylandWindow::surfaceDestroyed, this,
            [this] { clearSurface(); });
    auto* const display = m_nativeWindow->display();
    for (auto const& global : display->globals()) {
        if (global.interface == QStringLiteral("wl_subcompositor")) {
            m_subcompositor = static_cast<wl_subcompositor*>(
                wl_registry_bind(global.registry, global.id, &wl_subcompositor_interface, 1));
            Q_CHECK_PTR(m_subcompositor);
            break;
        }
    }
    if (!m_subcompositor) {
        qCInfo(sunplayerLogPlatform, "event=wayland.shadow_unavailable reason=no-subcompositor");
        return;
    }
    reconcile();
}

void WaylandWindowShadow::detach() {
    if (m_nativeWindow) {
        disconnect(m_nativeWindow, nullptr, this, nullptr);
    }
    clearSurface();
    if (m_subcompositor) {
        wl_subcompositor_destroy(m_subcompositor);
        m_subcompositor = nullptr;
    }
    m_nativeWindow = nullptr;
}

void WaylandWindowShadow::clearSurface() {
    if (m_subsurface) {
        wl_subsurface_destroy(m_subsurface);
        m_subsurface = nullptr;
        m_surface->destroy();
        m_surface.reset();
    }
    m_buffers = {};
    m_attached = nullptr;
    m_unavailableBody = {};
    m_unavailableScale = 0;
}

bool WaylandWindowShadow::eventFilter(QObject* watched, QEvent* event) {
    Q_ASSERT(watched == &m_window);
    if (event->type() == QEvent::PlatformSurface) {
        auto const* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            detach();
        } else if (!m_nativeWindow) {
            attach();
        }
    } else if (event->type() == QEvent::Expose || event->type() == QEvent::Paint ||
               event->type() == QEvent::UpdateRequest || event->type() == QEvent::Resize ||
               event->type() == QEvent::DevicePixelRatioChange || event->type() == QEvent::WindowStateChange) {
        // Run before the application's handler can present the parent. The
        // synchronized child must be staged for that same Qt-owned commit.
        reconcile();
    }
    return false;
}

void WaylandWindowShadow::hide() {
    if (!m_attached) {
        return;
    }
    m_surface->attach(nullptr, 0, 0);
    m_surface->commit();
    m_attached = nullptr;
    m_window.requestUpdate();
    qCDebug(sunplayerLogPlatform, "event=wayland.shadow visible=false");
}

void WaylandWindowShadow::reconcile() {
    if (!m_nativeWindow) {
        return;
    }
    bool const tiled = m_nativeWindow->toplevelWindowTilingStates() != QtWaylandClient::QWaylandWindow::WindowNoState;
    if (m_tilingChanged) {
        m_tilingChanged(tiled);
    }
    if (!m_subcompositor || !m_nativeWindow->wlSurface()) {
        return;
    }
    auto const states = m_nativeWindow->windowStates();
    if (states.testAnyFlags(Qt::WindowFullScreen | Qt::WindowMaximized) || tiled || m_window.size().isEmpty()) {
        hide();
        return;
    }
    // Integer buffer scale is sufficient for a soft shadow. Ceiling the parent
    // DPR also handles fractional outputs without another viewport protocol.
    int const scale = qCeil(m_window.devicePixelRatio());
    QSize const body = m_window.size();
    if (body == m_unavailableBody && scale == m_unavailableScale) {
        return;
    }
    m_unavailableBody = {};
    m_unavailableScale = 0;
    if (m_attached && m_attached->body == body && m_attached->scale == scale) {
        return;
    }
    auto slot =
        std::find_if(m_buffers.begin(), m_buffers.end(), [](auto const& buffer) { return !buffer || !buffer->busy; });
    if (slot == m_buffers.end()) {
        // Coalesce resize pressure to the latest window size. A release event
        // schedules the retry; never grow an unbounded queue of SHM images.
        hide();
        return;
    }
    if (!*slot || (*slot)->body != body || (*slot)->scale != scale) {
        auto buffer = std::make_unique<Buffer>(*this, body, scale);
        if (!buffer->isInitialized()) {
            // Storage/mapping failures are external. Leave playback intact and
            // retry only for changed geometry or a recreated native surface.
            m_unavailableBody = body;
            m_unavailableScale = scale;
            hide();
            return;
        }
        *slot = std::move(buffer);
    }
    if (!m_subsurface) {
        auto* const compositor = m_nativeWindow->display()->compositor();
        m_surface = std::make_unique<QtWayland::wl_surface>(compositor->create_surface());
        m_subsurface =
            wl_subcompositor_get_subsurface(m_subcompositor, m_surface->object(), m_nativeWindow->wlSurface());
        Q_CHECK_PTR(m_subsurface);
        wl_subsurface_place_below(m_subsurface, m_nativeWindow->wlSurface());
        wl_subsurface_set_position(m_subsurface, -extent, -extent);
        wl_region* const empty = compositor->create_region();
        m_surface->set_input_region(empty);
        m_surface->set_opaque_region(empty);
        wl_region_destroy(empty);
    }
    m_attached = slot->get();
    m_attached->busy = true;
    m_surface->set_buffer_scale(scale);
    m_surface->attach(m_attached->object(), 0, 0);
    m_surface->damage(0, 0, body.width() + 2 * extent, body.height() + 2 * extent);
    m_surface->commit();
    m_window.requestUpdate();
    qCDebug(sunplayerLogPlatform).noquote() << "event=wayland.shadow visible=true body=" << body << "scale=" << scale;
}
