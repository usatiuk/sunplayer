#include "audio/ControlledAudioSink.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <QtGlobal>

namespace {
std::int64_t framesToMicroseconds(std::uint64_t frames, int sampleRate) {
    Q_ASSERT(sampleRate > 0);
    std::uint64_t const seconds = frames / static_cast<std::uint64_t>(sampleRate);
    std::uint64_t const remainder = frames % static_cast<std::uint64_t>(sampleRate);
    constexpr std::uint64_t maximum = std::numeric_limits<std::int64_t>::max();
    if (seconds > maximum / 1'000'000ULL) {
        return std::numeric_limits<std::int64_t>::max();
    }
    std::uint64_t const whole = seconds * 1'000'000ULL;
    std::uint64_t const fraction = remainder * 1'000'000ULL / static_cast<std::uint64_t>(sampleRate);
    return fraction > maximum - whole ? std::numeric_limits<std::int64_t>::max()
                                      : static_cast<std::int64_t>(whole + fraction);
}
} // namespace

ControlledAudioSink::ControlledAudioSink(std::size_t maximumBufferedFrames)
    : m_maximumBufferedFrames(maximumBufferedFrames) {
    Q_ASSERT(m_maximumBufferedFrames != 0);
}

void ControlledAudioSink::reset(std::uint64_t playbackGeneration, AudioStreamFormat format) {
    Q_ASSERT(playbackGeneration != 0);
    Q_ASSERT(format.isValid());
    {
        std::lock_guard lock(m_mutex);
        m_blocks.clear();
        m_mapping.clear();
        m_format = format;
        m_playbackGeneration = playbackGeneration;
        if (++m_audioOutputEpoch == 0) {
            ++m_audioOutputEpoch;
        }
        m_submittedFrames = 0;
        m_presentedFrames = 0;
        m_deviceFramesWritten = 0;
        m_deviceFramesPresented = 0;
        m_underrunFrames = 0;
        m_bufferedFrames = 0;
        m_maximumObservedBufferedFrames = 0;
        m_running = false;
        m_finished = false;
        m_positionAvailable = true;
        m_holding = false;
        m_failureReason.clear();
    }
    m_wake.notify_all();
}

bool ControlledAudioSink::submit(PcmAudioBlock block, std::stop_token stopToken) {
    if (!block.isValid()) {
        return false;
    }
    std::size_t const frames = block.frameCount();
    if (frames > m_maximumBufferedFrames) {
        return false;
    }

    std::unique_lock lock(m_mutex);
    std::uint64_t const generation = block.playbackGeneration;
    AudioStreamFormat const format = block.format;
    bool const ready = m_wake.wait(lock, stopToken, [this, generation, frames, format] {
        return generation != m_playbackGeneration || m_finished || !m_failureReason.empty() || format != m_format ||
               outstandingFramesLocked() <= m_maximumBufferedFrames - frames;
    });
    if (!ready || generation != m_playbackGeneration || m_finished || !m_failureReason.empty() ||
        block.format != m_format) {
        return false;
    }
    m_bufferedFrames += frames;
    m_maximumObservedBufferedFrames =
        std::max(m_maximumObservedBufferedFrames, static_cast<std::size_t>(outstandingFramesLocked()));
    m_blocks.push_back({.block = std::move(block)});
    lock.unlock();
    m_wake.notify_all();
    return true;
}

void ControlledAudioSink::cancel(std::uint64_t playbackGeneration) {
    {
        std::lock_guard lock(m_mutex);
        if (playbackGeneration != m_playbackGeneration) {
            return;
        }
        m_blocks.clear();
        m_mapping.clear();
        m_format = {};
        m_playbackGeneration = 0;
        m_submittedFrames = 0;
        m_presentedFrames = 0;
        m_deviceFramesWritten = 0;
        m_deviceFramesPresented = 0;
        m_underrunFrames = 0;
        m_bufferedFrames = 0;
        m_running = false;
        m_finished = false;
        m_holding = false;
        m_failureReason.clear();
    }
    m_wake.notify_all();
}

void ControlledAudioSink::finish(std::uint64_t playbackGeneration) {
    {
        std::lock_guard lock(m_mutex);
        if (playbackGeneration != m_playbackGeneration) {
            return;
        }
        m_finished = true;
    }
    m_wake.notify_all();
}

void ControlledAudioSink::start() {
    std::lock_guard lock(m_mutex);
    if (m_failureReason.empty()) {
        m_running = true;
    }
}

void ControlledAudioSink::pause() {
    std::lock_guard lock(m_mutex);
    m_running = false;
}

void ControlledAudioSink::setGain(float linearGain) {
    Q_ASSERT(linearGain >= 0.0F && linearGain <= 1.0F);
    std::lock_guard lock(m_mutex);
    m_gain = std::clamp(linearGain, 0.0F, 1.0F);
}

AudioPresentationSnapshot ControlledAudioSink::snapshot() const {
    std::lock_guard lock(m_mutex);
    bool const drained = m_finished && m_bufferedFrames == 0 && m_presentedFrames == m_submittedFrames;
    AudioPresentationSnapshot result{
        .playbackGeneration = m_playbackGeneration,
        .audioOutputEpoch = m_audioOutputEpoch,
        .submittedFrames = m_submittedFrames,
        .presentedFrames = m_presentedFrames,
        .producerFinished = m_finished,
        .drained = drained,
        .holding = m_holding && !drained,
        .terminalPositionValid = drained && !m_mapping.empty(),
        .valid = m_playbackGeneration != 0 && m_format.isValid() && !m_mapping.empty() && m_positionAvailable,
    };
    result.advancing =
        result.valid && m_running && !result.drained && !result.holding && m_presentedFrames < m_submittedFrames;
    result.failed = !m_failureReason.empty();
    if (result.failed) {
        result.valid = false;
        result.advancing = false;
    }
    if (result.valid || result.terminalPositionValid) {
        result.mediaPositionMicroseconds = mediaPositionForPresentedFrameLocked(m_presentedFrames);
    }
    return result;
}

AudioSinkDiagnostics ControlledAudioSink::diagnostics() const {
    std::lock_guard lock(m_mutex);
    bool const hasPosition = m_playbackGeneration != 0 && m_format.isValid() && !m_mapping.empty() &&
                             m_positionAvailable && m_failureReason.empty();
    return {
        .backendName = "controlled",
        .errorMessage = m_failureReason,
        .format = m_format,
        .queueCapacityFrames = m_maximumBufferedFrames,
        .maximumSubmitFrames = m_maximumBufferedFrames,
        .queuedFrames = m_bufferedFrames,
        .maximumQueuedFrames = m_maximumObservedBufferedFrames,
        .mediaFramesSubmitted = m_submittedFrames,
        .mediaFramesPresented = m_presentedFrames,
        .deviceFramesWritten = m_deviceFramesWritten,
        .deviceFramesPresented = hasPosition ? std::optional<std::uint64_t>(m_deviceFramesPresented) : std::nullopt,
        .underrunFrames = m_underrunFrames,
        .audioOutputEpoch = m_audioOutputEpoch,
        .streamOpen = m_playbackGeneration != 0 && m_format.isValid(),
        .positionAvailable = hasPosition,
        .clockReliable = hasPosition,
    };
}

std::string ControlledAudioSink::failureReason() const {
    std::lock_guard lock(m_mutex);
    return m_failureReason;
}

void ControlledAudioSink::fail(std::uint64_t playbackGeneration, std::string reason) {
    {
        std::lock_guard lock(m_mutex);
        if (playbackGeneration != m_playbackGeneration) {
            return;
        }
        m_failureReason = reason.empty() ? "Injected audio output failure" : std::move(reason);
        m_running = false;
    }
    m_wake.notify_all();
}

void ControlledAudioSink::setPositionAvailable(bool available) {
    std::lock_guard lock(m_mutex);
    m_positionAvailable = available;
}

ControlledAudioRender ControlledAudioSink::render(std::size_t requestedFrames) {
    std::unique_lock lock(m_mutex);
    ControlledAudioRender result = renderLocked(requestedFrames);
    lock.unlock();
    m_wake.notify_all();
    return result;
}

ControlledAudioRender ControlledAudioSink::renderLocked(std::size_t requestedFrames) {
    ControlledAudioRender result;
    if (!m_running || requestedFrames == 0 || !m_format.isValid()) {
        return result;
    }

    std::size_t const channels = static_cast<std::size_t>(m_format.channelCount);
    std::size_t const available = std::min({
        requestedFrames,
        m_bufferedFrames,
    });
    result.samples.reserve(available * channels);

    while (result.frames < available) {
        Q_ASSERT(!m_blocks.empty());
        QueuedBlock& queued = m_blocks.front();
        std::size_t const blockFrames = queued.block.frameCount();
        std::size_t const remaining = blockFrames - queued.consumedFrames;
        std::size_t const taken = std::min(remaining, available - result.frames);
        std::size_t const firstSample = queued.consumedFrames * channels;
        std::size_t const sampleCount = taken * channels;
        result.samples.insert(result.samples.end(),
                              queued.block.samples.begin() + static_cast<std::ptrdiff_t>(firstSample),
                              queued.block.samples.begin() + static_cast<std::ptrdiff_t>(firstSample + sampleCount));
        if (m_gain != 1.0F) {
            std::transform(result.samples.end() - static_cast<std::ptrdiff_t>(sampleCount), result.samples.end(),
                           result.samples.end() - static_cast<std::ptrdiff_t>(sampleCount),
                           [gain = m_gain](float sample) { return sample * gain; });
        }

        std::uint64_t const sourceFrame = queued.block.streamFrameIndex + queued.consumedFrames;
        std::uint64_t const sourceOffset = sourceFrame - queued.block.streamFrameIndex;
        MappingSegment const mapping{
            .submittedStartFrame = m_submittedFrames,
            .frameCount = taken,
            .mediaStartMicroseconds =
                queued.block.mediaStartMicroseconds + framesToMicroseconds(sourceOffset, m_format.sampleRate),
        };
        if (!m_mapping.empty()) {
            MappingSegment& last = m_mapping.back();
            std::uint64_t const lastEnd = last.submittedStartFrame + last.frameCount;
            std::int64_t const expectedMediaStart =
                last.mediaStartMicroseconds + framesToMicroseconds(last.frameCount, m_format.sampleRate);
            if (lastEnd == mapping.submittedStartFrame && expectedMediaStart == mapping.mediaStartMicroseconds) {
                last.frameCount += mapping.frameCount;
            } else {
                m_mapping.push_back(mapping);
            }
        } else {
            m_mapping.push_back(mapping);
        }

        queued.consumedFrames += taken;
        result.frames += taken;
        m_submittedFrames += taken;
        m_deviceFramesWritten += taken;
        m_bufferedFrames -= taken;
        if (queued.consumedFrames == blockFrames) {
            m_blocks.pop_front();
        }
    }
    return result;
}

void ControlledAudioSink::prunePresentedMappingsLocked() {
    while (m_mapping.size() > 1 && m_mapping[1].submittedStartFrame <= m_presentedFrames) {
        m_mapping.erase(m_mapping.begin());
    }
}

void ControlledAudioSink::advancePresentedFrames(std::size_t frames) {
    {
        std::lock_guard lock(m_mutex);
        if (!m_running) {
            return;
        }
        std::uint64_t const available = m_submittedFrames - m_presentedFrames;
        std::uint64_t const presented = std::min<std::uint64_t>(frames, available);
        m_presentedFrames += presented;
        m_deviceFramesPresented += presented;
        if (presented != 0) {
            m_holding = false;
        }
        prunePresentedMappingsLocked();
    }
    m_wake.notify_all();
}

ControlledAudioAdvance ControlledAudioSink::advanceOutput(std::size_t requestedFrames) {
    ControlledAudioAdvance advanced;
    std::unique_lock lock(m_mutex);
    if (!m_running || requestedFrames == 0 || !m_format.isValid() || m_submittedFrames != m_presentedFrames ||
        !m_failureReason.empty()) {
        return advanced;
    }

    ControlledAudioRender const media = renderLocked(requestedFrames);
    advanced.mediaFrames = media.frames;
    m_presentedFrames += media.frames;
    m_deviceFramesPresented += media.frames;
    prunePresentedMappingsLocked();

    if (!m_finished && media.frames < requestedFrames) {
        advanced.holdFrames = requestedFrames - media.frames;
        m_deviceFramesWritten += advanced.holdFrames;
        m_deviceFramesPresented += advanced.holdFrames;
        m_underrunFrames += advanced.holdFrames;
    }
    m_holding = advanced.holdFrames != 0;
    lock.unlock();
    m_wake.notify_all();
    return advanced;
}

std::size_t ControlledAudioSink::bufferedFrames() const {
    std::lock_guard lock(m_mutex);
    return m_bufferedFrames;
}

std::size_t ControlledAudioSink::maximumObservedBufferedFrames() const {
    std::lock_guard lock(m_mutex);
    return m_maximumObservedBufferedFrames;
}

std::uint64_t ControlledAudioSink::submittedFrames() const {
    std::lock_guard lock(m_mutex);
    return m_submittedFrames;
}

std::uint64_t ControlledAudioSink::presentedFrames() const {
    std::lock_guard lock(m_mutex);
    return m_presentedFrames;
}

std::int64_t ControlledAudioSink::mediaPositionForPresentedFrameLocked(std::uint64_t presentedFrame) const {
    Q_ASSERT(!m_mapping.empty());
    for (MappingSegment const& segment : m_mapping) {
        std::uint64_t const end = segment.submittedStartFrame + segment.frameCount;
        if (presentedFrame <= end) {
            return segment.mediaStartMicroseconds +
                   framesToMicroseconds(presentedFrame - segment.submittedStartFrame, m_format.sampleRate);
        }
    }
    MappingSegment const& last = m_mapping.back();
    return last.mediaStartMicroseconds + framesToMicroseconds(last.frameCount, m_format.sampleRate);
}

std::uint64_t ControlledAudioSink::outstandingFramesLocked() const {
    return static_cast<std::uint64_t>(m_bufferedFrames) + m_submittedFrames - m_presentedFrames;
}
