#include "media/FfmpegHardwareDevice.h"

#include <utility>

extern "C" {
#include <libavutil/buffer.h>
}

std::shared_ptr<const FfmpegHardwareDevice>
FfmpegHardwareDevice::adopt(
        AVBufferRef *deviceContext,
        std::uint64_t graphicsDeviceGeneration,
        QString apiName) {
    if (!deviceContext
            || graphicsDeviceGeneration == 0
            || apiName.isEmpty()) {
        av_buffer_unref(&deviceContext);
        return {};
    }
    return std::shared_ptr<const FfmpegHardwareDevice>(
        new FfmpegHardwareDevice(
            deviceContext,
            graphicsDeviceGeneration,
            std::move(apiName)));
}

FfmpegHardwareDevice::FfmpegHardwareDevice(
        AVBufferRef *deviceContext,
        std::uint64_t graphicsDeviceGeneration,
        QString apiName)
    : m_deviceContext(deviceContext),
      m_graphicsDeviceGeneration(graphicsDeviceGeneration),
      m_apiName(std::move(apiName)) {}

FfmpegHardwareDevice::~FfmpegHardwareDevice() {
    av_buffer_unref(&m_deviceContext);
}

AVBufferRef *
FfmpegHardwareDevice::referenceDeviceContext() const {
    return av_buffer_ref(m_deviceContext);
}

std::uint64_t
FfmpegHardwareDevice::graphicsDeviceGeneration() const {
    return m_graphicsDeviceGeneration;
}

const QString &FfmpegHardwareDevice::apiName() const {
    return m_apiName;
}

bool VideoHardwareDecodeCapability::isAvailable() const {
    return static_cast<bool>(device);
}
