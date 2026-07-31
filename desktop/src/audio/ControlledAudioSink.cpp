#include "audio/ControlledAudioSink.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <QtGlobal>

namespace {
std::int64_t framesToMicroseconds(
        std::uint64_t frames,
        int sampleRate) {
    Q_ASSERT(sampleRate > 0);
    const std::uint64_t seconds =
        frames / static_cast<std::uint64_t>(sampleRate);
    const std::uint64_t remainder =
        frames % static_cast<std::uint64_t>(sampleRate);
    constexpr std::uint64_t maximum =
        std::numeric_limits<std::int64_t>::max();
    if (seconds > maximum / 1'000'000ULL)
        return std::numeric_limits<std::int64_t>::max();
    const std::uint64_t whole = seconds * 1'000'000ULL;
    const std::uint64_t fraction = remainder * 1'000'000ULL
        / static_cast<std::uint64_t>(sampleRate);
    return fraction > maximum - whole
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(whole + fraction);
}
}

ControlledAudioSink::ControlledAudioSink(
        std::size_t maximumBufferedFrames)
    : m_maximumBufferedFrames(maximumBufferedFrames) {
    Q_ASSERT(m_maximumBufferedFrames != 0);
}

void ControlledAudioSink::reset(
        std::uint64_t playbackGeneration,
        AudioStreamFormat format) {
    Q_ASSERT(playbackGeneration != 0);
    Q_ASSERT(format.isValid());
    {
        std::lock_guard lock(m_mutex);
        m_blocks.clear();
        m_mapping.clear();
        m_format = format;
        m_playbackGeneration = playbackGeneration;
        m_submittedFrames = 0;
        m_presentedFrames = 0;
        m_bufferedFrames = 0;
        m_maximumObservedBufferedFrames = 0;
        m_running = false;
        m_finished = false;
    }
    m_wake.notify_all();
}

bool ControlledAudioSink::submit(
        PcmAudioBlock block,
        std::stop_token stopToken) {
    if (!block.isValid())
        return false;
    const std::size_t frames = block.frameCount();
    if (frames > m_maximumBufferedFrames)
        return false;

    std::unique_lock lock(m_mutex);
    const std::uint64_t generation =
        block.playbackGeneration;
    const AudioStreamFormat format = block.format;
    const bool ready = m_wake.wait(
        lock,
        stopToken,
        [this, generation, frames, format] {
            return generation != m_playbackGeneration
                || m_finished
                || format != m_format
                || outstandingFramesLocked()
                        <= m_maximumBufferedFrames - frames;
        });
    if (!ready
            || generation != m_playbackGeneration
            || m_finished
            || block.format != m_format) {
        return false;
    }
    m_bufferedFrames += frames;
    m_maximumObservedBufferedFrames = std::max(
        m_maximumObservedBufferedFrames,
        static_cast<std::size_t>(outstandingFramesLocked()));
    m_blocks.push_back({.block = std::move(block)});
    lock.unlock();
    m_wake.notify_all();
    return true;
}

void ControlledAudioSink::finish() {
    {
        std::lock_guard lock(m_mutex);
        m_finished = true;
    }
    m_wake.notify_all();
}

void ControlledAudioSink::start() {
    std::lock_guard lock(m_mutex);
    m_running = true;
}

void ControlledAudioSink::pause() {
    std::lock_guard lock(m_mutex);
    m_running = false;
}

AudioPresentationSnapshot ControlledAudioSink::snapshot() const {
    std::lock_guard lock(m_mutex);
    AudioPresentationSnapshot result{
        .playbackGeneration = m_playbackGeneration,
        .submittedFrames = m_submittedFrames,
        .presentedFrames = m_presentedFrames,
        .producerFinished = m_finished,
        .drained = m_finished
            && m_bufferedFrames == 0
            && m_presentedFrames == m_submittedFrames,
        .valid = m_playbackGeneration != 0
            && m_format.isValid()
            && !m_mapping.empty(),
    };
    result.advancing = result.valid
        && m_running
        && !result.drained
        && m_presentedFrames < m_submittedFrames;
    if (result.valid) {
        result.mediaPositionMicroseconds =
            mediaPositionForPresentedFrameLocked(
                m_presentedFrames);
    }
    return result;
}

ControlledAudioRender ControlledAudioSink::render(
        std::size_t requestedFrames) {
    ControlledAudioRender result;
    std::unique_lock lock(m_mutex);
    if (!m_running
            || requestedFrames == 0
            || !m_format.isValid()) {
        return result;
    }

    const std::size_t channels =
        static_cast<std::size_t>(m_format.channelCount);
    const std::size_t available = std::min({
        requestedFrames,
        m_bufferedFrames,
    });
    result.samples.reserve(available * channels);

    while (result.frames < available) {
        Q_ASSERT(!m_blocks.empty());
        QueuedBlock &queued = m_blocks.front();
        const std::size_t blockFrames =
            queued.block.frameCount();
        const std::size_t remaining =
            blockFrames - queued.consumedFrames;
        const std::size_t taken = std::min(
            remaining, available - result.frames);
        const std::size_t firstSample =
            queued.consumedFrames * channels;
        const std::size_t sampleCount = taken * channels;
        result.samples.insert(
            result.samples.end(),
            queued.block.samples.begin()
                + static_cast<std::ptrdiff_t>(firstSample),
            queued.block.samples.begin()
                + static_cast<std::ptrdiff_t>(
                    firstSample + sampleCount));

        const std::uint64_t sourceFrame =
            queued.block.streamFrameIndex
            + queued.consumedFrames;
        const std::uint64_t sourceOffset =
            sourceFrame - queued.block.streamFrameIndex;
        const MappingSegment mapping{
            .submittedStartFrame = m_submittedFrames,
            .frameCount = taken,
            .mediaStartMicroseconds =
                queued.block.mediaStartMicroseconds
                + framesToMicroseconds(
                    sourceOffset, m_format.sampleRate),
        };
        if (!m_mapping.empty()) {
            MappingSegment &last = m_mapping.back();
            const std::uint64_t lastEnd =
                last.submittedStartFrame + last.frameCount;
            const std::int64_t expectedMediaStart =
                last.mediaStartMicroseconds
                + framesToMicroseconds(
                    last.frameCount, m_format.sampleRate);
            if (lastEnd == mapping.submittedStartFrame
                    && expectedMediaStart
                        == mapping.mediaStartMicroseconds) {
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
        m_bufferedFrames -= taken;
        if (queued.consumedFrames == blockFrames)
            m_blocks.pop_front();
    }

    lock.unlock();
    m_wake.notify_all();
    return result;
}

void ControlledAudioSink::advancePresentedFrames(
        std::size_t frames) {
    {
        std::lock_guard lock(m_mutex);
        if (!m_running)
            return;
        const std::uint64_t available =
            m_submittedFrames - m_presentedFrames;
        m_presentedFrames += std::min<std::uint64_t>(
            frames, available);
        while (m_mapping.size() > 1
                && m_mapping[1].submittedStartFrame
                    <= m_presentedFrames) {
            m_mapping.erase(m_mapping.begin());
        }
    }
    m_wake.notify_all();
}

std::size_t ControlledAudioSink::bufferedFrames() const {
    std::lock_guard lock(m_mutex);
    return m_bufferedFrames;
}

std::size_t
ControlledAudioSink::maximumObservedBufferedFrames() const {
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

std::int64_t
ControlledAudioSink::mediaPositionForPresentedFrameLocked(
        std::uint64_t presentedFrame) const {
    Q_ASSERT(!m_mapping.empty());
    for (const MappingSegment &segment : m_mapping) {
        const std::uint64_t end =
            segment.submittedStartFrame
            + segment.frameCount;
        if (presentedFrame <= end) {
            return segment.mediaStartMicroseconds
                + framesToMicroseconds(
                    presentedFrame
                        - segment.submittedStartFrame,
                    m_format.sampleRate);
        }
    }
    const MappingSegment &last = m_mapping.back();
    return last.mediaStartMicroseconds
        + framesToMicroseconds(
            last.frameCount, m_format.sampleRate);
}

std::uint64_t ControlledAudioSink::outstandingFramesLocked() const {
    return static_cast<std::uint64_t>(m_bufferedFrames)
        + m_submittedFrames - m_presentedFrames;
}
