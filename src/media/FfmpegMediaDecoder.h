#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

#include <QString>

#include "audio/AudioTypes.h"
#include "media/FfmpegVideoDecoder.h"
#include "subtitles/SubtitleTypes.h"

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
    int selectedSubtitleStreamIndex = -1;

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
    QString subtitleError;
    bool subtitleEndOfStream = false;
    QString error;
    bool cancelled = false;

    bool isSuccess() const;
    bool isCancelled() const;
    bool isStopped() const;
};

using FfmpegPcmAudioSink = std::function<bool(PcmAudioBlock, FfmpegAudioStreamDiagnostics const&, std::stop_token)>;

// Complete decoded-audio lifecycle. endOfStream is published by the audio
// worker immediately after its decoder and resampler have drained; it does
// not wait for video decoding or physical presentation to finish.
struct FfmpegAudioOutputSink {
    FfmpegPcmAudioSink submit;
    std::function<void(std::uint64_t)> endOfStream;

    bool isValid() const;
};

struct FfmpegMediaStreamSelection {
    bool audioStreamPresent = false;
    // False when selected-stream timing proves that the requested interval
    // begins at or after the audio endpoint. The stream remains selected for
    // diagnostics, but playback need not wait for an output epoch.
    bool audioOutputExpected = false;
    FfmpegVideoStreamDiagnostics videoDiagnostics;
    std::vector<SubtitleTrackDescriptor> subtitleTracks;
    std::optional<SubtitleStreamConfiguration> subtitleConfiguration;
};

using FfmpegMediaStreamSink = std::function<void(FfmpegMediaStreamSelection const&)>;

struct FfmpegSubtitleOutputSink {
    std::function<bool(SubtitleEvent, std::stop_token)> submit;
    std::function<void(QString)> failed;

    bool isValid() const;
};

// Opens and probes the source once, then routes referenced packets for the
// selected video and audio streams under one shared byte/count budget. This
// selected video path feeds the same hardware-capable packet decoder as the
// video-only operation. The caller supplies the active graphics capability;
// whole-operation hardware fallback remains a production-session policy.
// Video and audio sinks run concurrently on their decoder workers. They must
// provide their own synchronization for shared state, apply bounded
// backpressure, and return promptly when their supplied stop token is set.
FfmpegMediaDecodeResult decodeMediaFrames(FfmpegMediaDecodeRequest const& request,
                                          FfmpegVideoFrameSink const& videoSink, FfmpegPcmAudioSink const& audioSink,
                                          std::stop_token stopToken = {});

// Production boundary with early stream discovery and an explicit decoded
// audio end-of-stream event. Stream discovery is emitted exactly once after
// probing and seek setup succeed, before either decoder worker starts.
FfmpegMediaDecodeResult decodeMediaFrames(FfmpegMediaDecodeRequest const& request,
                                          FfmpegVideoFrameSink const& videoSink, FfmpegAudioOutputSink const& audioSink,
                                          FfmpegMediaStreamSink const& streamSink, std::stop_token stopToken = {});

FfmpegMediaDecodeResult decodeMediaFrames(FfmpegMediaDecodeRequest const& request,
                                          FfmpegVideoFrameSink const& videoSink, FfmpegAudioOutputSink const& audioSink,
                                          FfmpegMediaStreamSink const& streamSink,
                                          FfmpegSubtitleOutputSink const& subtitleSink, std::stop_token stopToken = {});
