#include "audio/AudioTypes.h"

#include <limits>

bool AudioStreamFormat::isValid() const {
    return sampleRate > 0 && channelCount > 0;
}

std::size_t PcmAudioBlock::frameCount() const {
    if (!format.isValid())
        return 0;
    return samples.size()
        / static_cast<std::size_t>(format.channelCount);
}

bool PcmAudioBlock::isValid() const {
    if (playbackGeneration == 0
            || !format.isValid()
            || samples.empty()
            || samples.size()
                % static_cast<std::size_t>(format.channelCount)
                != 0) {
        return false;
    }
    const std::size_t frames = frameCount();
    return frames != 0
        && frames
            <= std::numeric_limits<std::uint64_t>::max()
                - streamFrameIndex;
}
