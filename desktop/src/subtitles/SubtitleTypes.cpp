#include "subtitles/SubtitleTypes.h"

#include <limits>

namespace {
bool validRgbaSize(const QSize &size, qsizetype byteCount) {
    if (size.isEmpty() || size.width() <= 0 || size.height() <= 0)
        return false;
    constexpr qsizetype channels = 4;
    if (size.width()
            > std::numeric_limits<qsizetype>::max() / channels) {
        return false;
    }
    const qsizetype rowBytes = size.width() * channels;
    if (size.height()
            > std::numeric_limits<qsizetype>::max() / rowBytes) {
        return false;
    }
    return byteCount == rowBytes * size.height();
}
}

bool SubtitleTrackDescriptor::isValid() const {
    return streamIndex >= 0 && !label.isEmpty() && !codec.isEmpty();
}

bool SubtitleStreamConfiguration::isValid() const {
    return playbackGeneration != 0 && streamIndex >= 0 && !codec.isEmpty();
}

bool SubtitleBitmapRegion::isValid() const {
    return x >= 0 && y >= 0 && validRgbaSize(size, rgba.size());
}

bool SubtitleBitmapComposition::isValid() const {
    if (canvasSize.isEmpty() || regions.empty())
        return false;
    for (const SubtitleBitmapRegion &region : regions) {
        if (!region.isValid()
                || region.x > canvasSize.width()
                || region.size.width() > canvasSize.width() - region.x
                || region.y > canvasSize.height()
                || region.size.height() > canvasSize.height() - region.y) {
            return false;
        }
    }
    return true;
}

bool SubtitleEvent::isValid() const {
    if (playbackGeneration == 0 || startMicroseconds < 0)
        return false;
    if (endMicroseconds && *endMicroseconds <= startMicroseconds)
        return false;
    switch (type) {
    case SubtitlePayloadType::AssText:
        return !ass.isEmpty() && !bitmap;
    case SubtitlePayloadType::Bitmap:
        return ass.isEmpty() && bitmap && bitmap->isValid();
    case SubtitlePayloadType::Clear:
        return ass.isEmpty() && !bitmap;
    }
    return false;
}

bool SubtitleStateSnapshot::isEnabled() const {
    return playbackGeneration != 0 && configuration
        && configuration->isValid() && events && error.isEmpty();
}
