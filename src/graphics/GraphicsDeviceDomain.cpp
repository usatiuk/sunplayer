#include "graphics/GraphicsDeviceDomain.h"

#include <atomic>
#include <utility>

GraphicsDeviceExecutionScope::GraphicsDeviceExecutionScope(
        std::shared_ptr<void> state,
        UnlockOperation unlock)
    : m_state(std::move(state)),
      m_unlock(unlock) {
    Q_ASSERT(m_state);
    Q_ASSERT(m_unlock);
}

GraphicsDeviceExecutionScope::~GraphicsDeviceExecutionScope() {
    if (m_unlock)
        m_unlock(m_state.get());
}

GraphicsDeviceExecutionScope::GraphicsDeviceExecutionScope(
        GraphicsDeviceExecutionScope &&other) noexcept
    : m_state(std::move(other.m_state)),
      m_unlock(std::exchange(other.m_unlock, nullptr)) {}

bool GraphicsDeviceDiagnostics::isValid() const {
    return backend != GraphicsBackend::Unknown
        && !backendName.isEmpty()
        && !nativeApi.isEmpty()
        && !adapterName.isEmpty();
}

bool LibplaceboGraphicsContext::isValid() const {
    return log && gpu;
}

GraphicsDeviceDomain::GraphicsDeviceDomain() {
    static std::atomic_uint64_t nextGeneration{1};
    do {
        m_generation = nextGeneration.fetch_add(
            1, std::memory_order_relaxed);
    } while (m_generation == 0);
}

GraphicsDeviceDomain::~GraphicsDeviceDomain() = default;

bool GraphicsDeviceDomain::supportsPresentation(QWindow &) const {
    return true;
}

bool GraphicsDeviceDomain::supportsHdr10Presentation(QWindow &) const {
    return true;
}

GraphicsBackend GraphicsDeviceDomain::backend() const {
    return diagnostics().backend;
}

std::uint64_t GraphicsDeviceDomain::generation() const {
    return m_generation;
}
