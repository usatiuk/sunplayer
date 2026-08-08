#pragma once

#include <cstdint>
#include <memory>

#include <QString>

struct AVBufferRef;

// Retains an initialized FFmpeg hardware-device context that belongs to one
// graphics-device generation. The native device stays opaque at the shared
// media boundary.
class FfmpegHardwareDevice final {
  public:
    static std::shared_ptr<FfmpegHardwareDevice const> adopt(AVBufferRef* deviceContext,
                                                             std::uint64_t graphicsDeviceGeneration, QString apiName);

    ~FfmpegHardwareDevice();

    FfmpegHardwareDevice(FfmpegHardwareDevice const&) = delete;
    FfmpegHardwareDevice& operator=(FfmpegHardwareDevice const&) = delete;

    AVBufferRef* referenceDeviceContext() const;
    std::uint64_t graphicsDeviceGeneration() const;
    QString const& apiName() const;

  private:
    FfmpegHardwareDevice(AVBufferRef* deviceContext, std::uint64_t graphicsDeviceGeneration, QString apiName);

    AVBufferRef* m_deviceContext = nullptr;
    std::uint64_t m_graphicsDeviceGeneration = 0;
    QString m_apiName;
};

struct VideoHardwareDecodeCapability {
    std::shared_ptr<FfmpegHardwareDevice const> device;
    QString unavailableReason;

    bool isAvailable() const;
};
