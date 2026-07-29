#include "media/FfmpegFirstFrameDecoder.h"

#include <utility>

#include "media/DecodedVideoFrame.h"
#include "media/FfmpegHardwareDevice.h"

bool FfmpegFirstFrameResult::isSuccess() const {
    return frame
        && diagnostics.isValid()
        && error.isEmpty()
        && !cancelled;
}

bool FfmpegFirstFrameResult::isCancelled() const {
    return cancelled
        && !frame
        && error.isEmpty();
}

FfmpegFirstFrameResult decodeFirstVideoFrame(
        const QString &path,
        const VideoFrameIdentity &identity,
        std::stop_token stopToken) {
    return decodeFirstVideoFrame(
        path,
        identity,
        VideoHardwareDecodeCapability{},
        stopToken);
}

FfmpegFirstFrameResult decodeFirstVideoFrame(
        const QString &path,
        const VideoFrameIdentity &identity,
        const VideoHardwareDecodeCapability &hardwareDecode,
        std::stop_token stopToken) {
    FfmpegFirstFrameResult firstFrame;
    const FfmpegVideoDecodeResult decoded =
        decodeVideoFrames(
            path,
            identity,
            hardwareDecode,
            2,
            [&firstFrame](
                    std::shared_ptr<const DecodedVideoFrame> frame,
                    const FfmpegVideoStreamDiagnostics &diagnostics) {
                firstFrame.frame = std::move(frame);
                firstFrame.diagnostics = diagnostics;
                return false;
            },
            stopToken);

    if (firstFrame.frame)
        return firstFrame;
    firstFrame.error = decoded.error;
    firstFrame.cancelled = decoded.cancelled;
    return firstFrame;
}
