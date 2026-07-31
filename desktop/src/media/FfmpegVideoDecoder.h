#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

#include <QString>

#include "media/DecodedVideoFrame.h"
#include "media/FfmpegHardwareDevice.h"

struct VideoTimelineOrigin {
    bool operator==(const VideoTimelineOrigin &) const = default;

    std::int64_t timestamp = 0;
    VideoFrameRational timeBase;

    bool isValid() const;
    std::optional<std::int64_t> microseconds() const;
};

// Converts a normalized playback position into one absolute selected-stream
// timestamp. This is a single 64-bit rescale, not an incremental clock update.
std::optional<std::int64_t> videoStreamTimestampForPosition(
    const VideoTimelineOrigin &origin,
    const VideoFrameRational &streamTimeBase,
    std::int64_t targetPositionMicroseconds);

struct VideoDecodeStart {
    std::optional<std::int64_t> targetPositionMicroseconds;
    std::optional<VideoTimelineOrigin> timelineOrigin;
    bool performDemuxSeek = false;

    bool isValid() const;
};

struct FfmpegVideoDecodeRequest {
    QString path;
    VideoFrameIdentity firstFrameIdentity;
    VideoHardwareDecodeCapability hardwareDecode;
    int extraHardwareFrames = 0;
    VideoDecodeStart start;

    bool isValid() const;
};

struct FfmpegVideoStreamDiagnostics {
    QString containerFormat;
    QString decoderName;
    QString decodePath;
    QString hardwareFallbackReason;
    int videoStreamIndex = -1;
    bool hardwareAccelerated = false;
    bool seekable = false;
    std::optional<std::int64_t> durationMicroseconds;
    bool durationFinal = false;
    std::optional<VideoTimelineOrigin> timelineOrigin;
    std::optional<std::int64_t> nominalFrameDurationMicroseconds;

    bool isValid() const;
};

struct FfmpegVideoDecodeResult {
    FfmpegVideoStreamDiagnostics diagnostics;
    QString error;
    std::uint64_t framesDecoded = 0;
    std::optional<std::int64_t> observedEndMicroseconds;
    bool endOfStream = false;
    bool stopped = false;
    bool cancelled = false;

    bool isSuccess() const;
    bool isCancelled() const;
};

using FfmpegVideoFrameSink = std::function<bool(
    std::shared_ptr<const DecodedVideoFrame>,
    const FfmpegVideoStreamDiagnostics &)>;

// Runs one complete demux/decode operation synchronously on the caller's
// worker thread. Internally, the demuxer and decoder have separate owners and
// communicate through a byte-bounded packet channel. The sink supplies
// decoded-frame backpressure and must not retain frames without a bound.
FfmpegVideoDecodeResult decodeVideoFrames(
    const FfmpegVideoDecodeRequest &request,
    const FfmpegVideoFrameSink &sink,
    std::stop_token stopToken = {});
