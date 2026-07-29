#include "media/ffmpeg/FfmpegFirstFrameDecodeFallback.h"

#include "media/FfmpegHardwareDevice.h"

FfmpegFirstFrameResult decodeFirstVideoFrameWithFallback(
        const VideoHardwareDecodeCapability &hardwareDecode,
        const FfmpegFirstFrameDecodeAttempt &attempt) {
    Q_ASSERT(attempt);

    bool hardwareSelected = false;
    FfmpegFirstFrameResult result =
        attempt(hardwareDecode, hardwareSelected);
    if (result.isSuccess()
            || result.isCancelled()
            || !hardwareSelected) {
        return result;
    }

    const QString hardwareError = result.error.isEmpty()
        ? QStringLiteral("unknown decoding error")
        : result.error;
    const QString apiName = hardwareDecode.device
        ? hardwareDecode.device->apiName()
        : QStringLiteral("Hardware");
    bool softwareHardwareSelected = false;
    FfmpegFirstFrameResult softwareResult =
        attempt(
            {
                .device = {},
                .unavailableReason =
                    QStringLiteral("%1 decode failed: %2")
                        .arg(apiName, hardwareError),
            },
            softwareHardwareSelected);
    Q_ASSERT(!softwareHardwareSelected);
    if (softwareResult.isSuccess()
            || softwareResult.isCancelled()) {
        return softwareResult;
    }

    softwareResult.error = QStringLiteral(
        "Hardware decode failed: %1 Software fallback also failed: %2")
        .arg(hardwareError, softwareResult.error);
    return softwareResult;
}
