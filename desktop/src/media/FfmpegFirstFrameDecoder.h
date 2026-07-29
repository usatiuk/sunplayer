#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>

#include <QString>

class DecodedVideoFrame;
struct VideoFrameIdentity;

struct FfmpegFirstFrameDiagnostics {
    QString containerFormat;
    QString decoderName;
    int videoStreamIndex = -1;

    bool isValid() const;
};

struct FfmpegFirstFrameResult {
    std::shared_ptr<const DecodedVideoFrame> frame;
    FfmpegFirstFrameDiagnostics diagnostics;
    QString error;
    bool cancelled = false;

    bool isSuccess() const;
    bool isCancelled() const;
};

// Synchronous first-frame boundary used to prove real demux, decode, frame
// ownership, and rendering. Continuous playback will put the same decode
// operations behind cancellation and bounded packet/frame queues.
FfmpegFirstFrameResult decodeFirstVideoFrame(
    const QString &path,
    const VideoFrameIdentity &identity,
    std::stop_token stopToken = {});
