#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>

#include <QString>

#include "audio/AudioTypes.h"
#include "media/FfmpegVideoDecoder.h"

struct FfmpegAudioStreamDiagnostics {
    QString decoderName;
    int audioStreamIndex = -1;
    int sourceSampleRate = 0;
    int sourceChannelCount = 0;
    AudioStreamFormat outputFormat;
    std::optional<VideoTimelineOrigin> timelineOrigin;

    bool isValid() const;
};

struct FfmpegMediaDecodeRequest {
    FfmpegVideoDecodeRequest video;
    bool decodeSelectedAudio = true;
    AudioStreamFormat audioOutput{48'000, 2};

    bool isValid() const;
};

struct FfmpegMediaDecodeResult {
    FfmpegVideoDecodeResult video;
    std::optional<FfmpegAudioStreamDiagnostics> audio;
    std::uint64_t decodedAudioFrames = 0;
    std::uint64_t outputAudioFrames = 0;
    std::optional<std::int64_t> observedAudioEndMicroseconds;
    std::size_t packetCountLimit = 0;
    std::size_t packetByteLimit = 0;
    std::size_t maximumQueuedPacketCount = 0;
    std::size_t maximumQueuedPacketBytes = 0;
    std::size_t largestQueuedPacketBytes = 0;
    bool audioStreamPresent = false;
    bool audioEndOfStream = false;
    bool audioStopped = false;
    QString error;
    bool cancelled = false;

    bool isSuccess() const;
    bool isCancelled() const;
    bool isStopped() const;
};

using FfmpegPcmAudioSink = std::function<bool(
    PcmAudioBlock,
    const FfmpegAudioStreamDiagnostics &,
    std::stop_token)>;

// Opens and probes the source once, then routes referenced packets for the
// selected video and audio streams under one shared byte/count budget. This
// selected video path feeds the same hardware-capable packet decoder as the
// video-only operation. The caller supplies the active graphics capability;
// whole-operation hardware fallback remains a production-session policy.
// Video and audio sinks run concurrently on their decoder workers. They must
// provide their own synchronization for shared state, apply bounded
// backpressure, and return promptly when their supplied stop token is set.
FfmpegMediaDecodeResult decodeMediaFrames(
    const FfmpegMediaDecodeRequest &request,
    const FfmpegVideoFrameSink &videoSink,
    const FfmpegPcmAudioSink &audioSink,
    std::stop_token stopToken = {});
