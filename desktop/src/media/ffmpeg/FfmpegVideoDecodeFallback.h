#pragma once

#include <functional>

#include "media/FfmpegVideoDecoder.h"

struct VideoHardwareDecodeCapability;

using FfmpegVideoDecodeAttempt =
    std::function<FfmpegVideoDecodeResult(
        const VideoHardwareDecodeCapability &,
        bool &hardwareSelected)>;

FfmpegVideoDecodeResult decodeVideoFramesWithFallback(
    const VideoHardwareDecodeCapability &hardwareDecode,
    const FfmpegVideoDecodeAttempt &attempt);
