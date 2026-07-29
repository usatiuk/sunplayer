#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

#include <QString>

class DecodedVideoFrame;
struct VideoFrameIdentity;
struct VideoHardwareDecodeCapability;

struct FfmpegVideoStreamDiagnostics {
    QString containerFormat;
    QString decoderName;
    QString decodePath;
    QString hardwareFallbackReason;
    int videoStreamIndex = -1;
    bool hardwareAccelerated = false;
    std::optional<std::int64_t> durationMicroseconds;
    std::optional<std::int64_t> timelineOriginMicroseconds;
    std::optional<std::int64_t> nominalFrameDurationMicroseconds;

    bool isValid() const;
};

struct FfmpegVideoDecodeResult {
    FfmpegVideoStreamDiagnostics diagnostics;
    QString error;
    std::uint64_t framesDecoded = 0;
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
    const QString &path,
    const VideoFrameIdentity &firstFrameIdentity,
    const VideoHardwareDecodeCapability &hardwareDecode,
    int extraHardwareFrames,
    const FfmpegVideoFrameSink &sink,
    std::stop_token stopToken = {});
