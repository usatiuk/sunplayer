#pragma once

#include <cstdint>
#include <memory>

#include <QString>
#include <libplacebo/gpu.h>

class QRhi;
class VideoTargetInterop;
struct VideoTargetRequest;

enum class GraphicsBackend {
    Unknown,
    D3D11,
    Vulkan,
    Metal,
};

struct GraphicsDeviceDiagnostics {
    GraphicsBackend backend = GraphicsBackend::Unknown;
    QString backendName;
    QString nativeApi;
    QString adapterName;

    bool isValid() const;
};

struct LibplaceboGraphicsContext {
    pl_log log = nullptr;
    pl_gpu gpu = nullptr;

    bool isValid() const;
};

// Owns one native graphics-device domain and its shared QRhi/libplacebo
// contexts. Backend-native types remain in the factory-selected
// implementation.
class GraphicsDeviceDomain {
public:
    virtual ~GraphicsDeviceDomain();

    GraphicsDeviceDomain(const GraphicsDeviceDomain &) = delete;
    GraphicsDeviceDomain &operator=(const GraphicsDeviceDomain &) = delete;

    virtual QRhi &rhi() const = 0;
    virtual const GraphicsDeviceDiagnostics &diagnostics() const = 0;
    virtual const LibplaceboGraphicsContext &
        libplaceboContext() const = 0;
    virtual std::unique_ptr<VideoTargetInterop> createVideoTarget(
        const VideoTargetRequest &request) = 0;

    GraphicsBackend backend() const;
    std::uint64_t generation() const;

protected:
    GraphicsDeviceDomain();

private:
    std::uint64_t m_generation = 0;
};
