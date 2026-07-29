#pragma once

struct AVCodecParameters;
struct AVFrame;

// Copies stream defaults onto a decoded frame only when the decoder did not
// provide a frame-level value. The caller still owns both FFmpeg objects.
bool mergeStreamVideoMetadata(
    AVFrame &frame,
    const AVCodecParameters &parameters);
