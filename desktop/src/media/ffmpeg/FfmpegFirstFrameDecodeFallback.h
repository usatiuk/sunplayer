#pragma once

#include <functional>

#include "media/FfmpegFirstFrameDecoder.h"

struct VideoHardwareDecodeCapability;

using FfmpegFirstFrameDecodeAttempt =
    std::function<FfmpegFirstFrameResult(
        const VideoHardwareDecodeCapability &,
        bool &hardwareSelected)>;

FfmpegFirstFrameResult decodeFirstVideoFrameWithFallback(
    const VideoHardwareDecodeCapability &hardwareDecode,
    const FfmpegFirstFrameDecodeAttempt &attempt);
