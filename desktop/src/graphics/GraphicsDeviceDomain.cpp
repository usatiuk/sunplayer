#include "graphics/GraphicsDeviceDomain.h"

#include <atomic>

bool GraphicsDeviceDiagnostics::isValid() const {
    return backend != GraphicsBackend::Unknown
        && !backendName.isEmpty()
        && !nativeApi.isEmpty()
        && !adapterName.isEmpty();
}

GraphicsDeviceDomain::GraphicsDeviceDomain() {
    static std::atomic_uint64_t nextGeneration{1};
    do {
        m_generation = nextGeneration.fetch_add(
            1, std::memory_order_relaxed);
    } while (m_generation == 0);
}

GraphicsDeviceDomain::~GraphicsDeviceDomain() = default;

GraphicsBackend GraphicsDeviceDomain::backend() const {
    return diagnostics().backend;
}

std::uint64_t GraphicsDeviceDomain::generation() const {
    return m_generation;
}
