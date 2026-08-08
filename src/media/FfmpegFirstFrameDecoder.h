#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>

#include <QString>

#include "media/FfmpegVideoDecoder.h"

class DecodedVideoFrame;
struct VideoHardwareDecodeCapability;
struct VideoFrameIdentity;

using FfmpegFirstFrameDiagnostics = FfmpegVideoStreamDiagnostics;

struct FfmpegFirstFrameResult {
    std::shared_ptr<DecodedVideoFrame const> frame;
    FfmpegFirstFrameDiagnostics diagnostics;
    QString error;
    bool cancelled = false;

    bool isSuccess() const;
    bool isCancelled() const;
};

// Compatibility adapter used by focused frame/import tests. Production
// playback and this helper share the same continuous decoder implementation.
FfmpegFirstFrameResult decodeFirstVideoFrame(QString const& path, VideoFrameIdentity const& identity,
                                             std::stop_token stopToken = {});

FfmpegFirstFrameResult decodeFirstVideoFrame(QString const& path, VideoFrameIdentity const& identity,
                                             VideoHardwareDecodeCapability const& hardwareDecode,
                                             std::stop_token stopToken = {});
