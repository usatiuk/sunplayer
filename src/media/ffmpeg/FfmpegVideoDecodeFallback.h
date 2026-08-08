#pragma once

#include <functional>

#include "media/FfmpegVideoDecoder.h"

struct VideoHardwareDecodeCapability;

using FfmpegVideoDecodeAttempt =
    std::function<FfmpegVideoDecodeResult(VideoHardwareDecodeCapability const&, bool& hardwareSelected)>;

FfmpegVideoDecodeResult decodeVideoFramesWithFallback(VideoHardwareDecodeCapability const& hardwareDecode,
                                                      FfmpegVideoDecodeAttempt const& attempt);
