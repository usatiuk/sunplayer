#include "media/FfmpegFirstFrameDecoder.h"

#include <utility>

#include "media/DecodedVideoFrame.h"
#include "media/FfmpegHardwareDevice.h"

bool FfmpegFirstFrameResult::isSuccess() const {
    return frame && diagnostics.isValid() && error.isEmpty() && !cancelled;
}

bool FfmpegFirstFrameResult::isCancelled() const { return cancelled && !frame && error.isEmpty(); }

FfmpegFirstFrameResult decodeFirstVideoFrame(QString const& path, VideoFrameIdentity const& identity,
                                             std::stop_token stopToken) {
    return decodeFirstVideoFrame(path, identity, VideoHardwareDecodeCapability{}, stopToken);
}

FfmpegFirstFrameResult decodeFirstVideoFrame(QString const& path, VideoFrameIdentity const& identity,
                                             VideoHardwareDecodeCapability const& hardwareDecode,
                                             std::stop_token stopToken) {
    FfmpegFirstFrameResult firstFrame;
    FfmpegVideoDecodeResult const decoded = decodeVideoFrames(
        {
            .path = path,
            .firstFrameIdentity = identity,
            .hardwareDecode = hardwareDecode,
            .extraHardwareFrames = 2,
        },
        [&firstFrame](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const& diagnostics) {
            firstFrame.frame = std::move(frame);
            firstFrame.diagnostics = diagnostics;
            return false;
        },
        stopToken);

    if (firstFrame.frame) {
        return firstFrame;
    }
    firstFrame.error = decoded.error;
    firstFrame.cancelled = decoded.cancelled;
    return firstFrame;
}
