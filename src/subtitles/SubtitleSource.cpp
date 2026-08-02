#include "subtitles/SubtitleSource.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace {
constexpr std::size_t maximumRetainedEvents = 16'384;
constexpr std::size_t maximumRetainedPayloadBytes =
    256U * 1024U * 1024U;

std::optional<std::size_t> retainedPayloadBytes(
        const SubtitleEvent &event) {
    std::size_t bytes = static_cast<std::size_t>(event.ass.size());
    if (!event.bitmap)
        return bytes;
    for (const SubtitleBitmapRegion &region : event.bitmap->regions) {
        const std::size_t regionBytes =
            static_cast<std::size_t>(region.rgba.size());
        if (regionBytes > std::numeric_limits<std::size_t>::max() - bytes)
            return std::nullopt;
        bytes += regionBytes;
    }
    return bytes;
}
}

void SubtitleSource::reset(std::uint64_t playbackGeneration) {
    std::lock_guard lock(m_mutex);
    m_playbackGeneration = playbackGeneration;
    m_configuration.reset();
    m_events = std::make_shared<std::vector<SubtitleEvent>>();
    m_retainedPayloadBytes = 0;
    m_error.clear();
    advanceRevision();
}

void SubtitleSource::configure(
        SubtitleStreamConfiguration configuration) {
    if (!configuration.isValid())
        return;
    std::lock_guard lock(m_mutex);
    if (configuration.playbackGeneration
            != m_playbackGeneration) {
        return;
    }
    m_configuration =
        std::make_shared<const SubtitleStreamConfiguration>(
            std::move(configuration));
    m_events = std::make_shared<std::vector<SubtitleEvent>>();
    m_retainedPayloadBytes = 0;
    m_error.clear();
    advanceRevision();
}

bool SubtitleSource::append(SubtitleEvent event) {
    if (!event.isValid())
        return false;
    std::lock_guard lock(m_mutex);
    if (event.playbackGeneration != m_playbackGeneration
            || !m_configuration || !m_error.isEmpty()) {
        return false;
    }
    const std::optional<std::size_t> eventBytes =
        retainedPayloadBytes(event);
    if (!eventBytes
            || m_events->size() >= maximumRetainedEvents
            || *eventBytes > maximumRetainedPayloadBytes
                - std::min(
                    m_retainedPayloadBytes,
                    maximumRetainedPayloadBytes)) {
        m_error = QStringLiteral("Subtitle history budget exceeded");
        advanceRevision();
        return false;
    }
    // A reader keeps its immutable vector alive. Most appends are amortized;
    // copying is needed only while a snapshot is concurrently retained.
    if (m_events.use_count() != 1) {
        m_events = std::make_shared<std::vector<SubtitleEvent>>(*m_events);
    }
    m_events->push_back(std::move(event));
    m_retainedPayloadBytes += *eventBytes;
    advanceRevision();
    return true;
}

void SubtitleSource::fail(
        std::uint64_t playbackGeneration, QString error) {
    if (error.isEmpty())
        return;
    std::lock_guard lock(m_mutex);
    if (playbackGeneration != m_playbackGeneration)
        return;
    if (m_error.isEmpty()) {
        m_error = std::move(error);
        advanceRevision();
    }
}

SubtitleStateSnapshot SubtitleSource::snapshot() const {
    std::lock_guard lock(m_mutex);
    return {
        .playbackGeneration = m_playbackGeneration,
        .revision = m_revision,
        .configuration = m_configuration,
        .events = m_events,
        .error = m_error,
    };
}

void SubtitleSource::advanceRevision() {
    ++m_revision;
    if (m_revision == 0)
        ++m_revision;
}
