#pragma once

#include <cstdint>
#include <memory>

#include <QString>
#include <libplacebo/gpu.h>

class QRhi;
class QWindow;
class LibplaceboHardwareFrameImporter;
struct VideoHardwareDecodeCapability;
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

// Keeps one backend-defined GPU command sequence mutually exclusive with
// decoder command submission without exposing native synchronization types.
// The shared state outlives the graphics domain when teardown occurs inside
// a protected operation.
class GraphicsDeviceExecutionScope final {
public:
    using UnlockOperation = void (*)(void *);

    GraphicsDeviceExecutionScope(
        std::shared_ptr<void> state,
        UnlockOperation unlock);
    ~GraphicsDeviceExecutionScope();

    GraphicsDeviceExecutionScope(
        const GraphicsDeviceExecutionScope &) = delete;
    GraphicsDeviceExecutionScope &operator=(
        const GraphicsDeviceExecutionScope &) = delete;
    GraphicsDeviceExecutionScope(
        GraphicsDeviceExecutionScope &&other) noexcept;
    GraphicsDeviceExecutionScope &operator=(
        GraphicsDeviceExecutionScope &&) = delete;

private:
    std::shared_ptr<void> m_state;
    UnlockOperation m_unlock = nullptr;
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
    virtual const VideoHardwareDecodeCapability &
        videoDecodeCapability() const = 0;
    virtual GraphicsDeviceExecutionScope
        acquireExecutionScope() = 0;
    virtual std::unique_ptr<VideoTargetInterop> createVideoTarget(
        const VideoTargetRequest &request) = 0;
    virtual std::unique_ptr<LibplaceboHardwareFrameImporter>
        createHardwareFrameImporter() = 0;
    virtual bool supportsPresentation(QWindow &window) const;
    virtual bool supportsHdr10Presentation(QWindow &window) const;

    GraphicsBackend backend() const;
    std::uint64_t generation() const;

protected:
    GraphicsDeviceDomain();

private:
    std::uint64_t m_generation = 0;
};
