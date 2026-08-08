#pragma once

#include <functional>
#include <memory>
#include <stop_token>

#include <QString>

#include "media/FfmpegVideoDecoder.h"

struct AVCodec;
struct AVCodecParameters;
struct AVPacket;

struct FfmpegAvPacketDeleter {
    void operator()(AVPacket* packet) const;
};

using FfmpegAvPacketPtr = std::unique_ptr<AVPacket, FfmpegAvPacketDeleter>;

enum class FfmpegVideoPacketTerminal {
    Packet,
    EndOfStream,
    Failed,
    Cancelled,
};

struct FfmpegVideoPacketRead {
    FfmpegAvPacketPtr packet;
    FfmpegVideoPacketTerminal terminal = FfmpegVideoPacketTerminal::Packet;
    QString error;
};

using FfmpegVideoPacketSource = std::function<FfmpegVideoPacketRead(std::stop_token)>;

// Owns the selected codec context and all decoded-frame state. The source
// owns demuxing and packet backpressure; every packet remains owned by the
// returned read object until the decoder has consumed it.
FfmpegVideoDecodeResult
decodeFfmpegVideoPackets(FfmpegVideoDecodeRequest const& request, AVCodec const& decoder,
                         AVCodecParameters const& streamParameters, VideoFrameRational streamTimeBase,
                         VideoFrameRational streamAspectRatio, FfmpegVideoStreamDiagnostics baseDiagnostics,
                         FfmpegVideoPacketSource const& source, FfmpegVideoFrameSink const& sink,
                         std::stop_token stopToken, bool* hardwareSelected);
