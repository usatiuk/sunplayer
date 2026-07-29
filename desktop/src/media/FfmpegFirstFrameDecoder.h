#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>

#include <QString>

#include "media/FfmpegVideoDecoder.h"

class DecodedVideoFrame;
struct VideoHardwareDecodeCapability;
struct VideoFrameIdentity;

using FfmpegFirstFrameDiagnostics =
    FfmpegVideoStreamDiagnostics;

struct FfmpegFirstFrameResult {
    std::shared_ptr<const DecodedVideoFrame> frame;
    FfmpegFirstFrameDiagnostics diagnostics;
    QString error;
    bool cancelled = false;

    bool isSuccess() const;
    bool isCancelled() const;
};

// Compatibility adapter used by focused frame/import tests. Production
// playback and this helper share the same continuous decoder implementation.
FfmpegFirstFrameResult decodeFirstVideoFrame(
    const QString &path,
    const VideoFrameIdentity &identity,
    std::stop_token stopToken = {});

FfmpegFirstFrameResult decodeFirstVideoFrame(
    const QString &path,
    const VideoFrameIdentity &identity,
    const VideoHardwareDecodeCapability &hardwareDecode,
    std::stop_token stopToken = {});
