#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "subtitles/SubtitleTypes.h"

class SubtitleSource final {
public:
    void reset(std::uint64_t playbackGeneration);
    void configure(SubtitleStreamConfiguration configuration);
    bool append(SubtitleEvent event);
    void fail(std::uint64_t playbackGeneration, QString error);
    SubtitleStateSnapshot snapshot() const;

private:
    void advanceRevision();

    mutable std::mutex m_mutex;
    std::uint64_t m_playbackGeneration = 0;
    std::uint64_t m_revision = 0;
    std::shared_ptr<const SubtitleStreamConfiguration> m_configuration;
    std::shared_ptr<std::vector<SubtitleEvent>> m_events =
        std::make_shared<std::vector<SubtitleEvent>>();
    std::size_t m_retainedPayloadBytes = 0;
    QString m_error;
};
