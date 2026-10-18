#pragma once
#include "media/MediaInput.h"
extern "C" {
#include <libavformat/avformat.h>
}
int openFfmpegInput(AVFormatContext** context, MediaInput const& input);
