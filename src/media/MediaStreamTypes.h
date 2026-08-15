#pragma once

#include <cstdint>
#include <optional>

#include <QString>

// A stream discovered inside the currently opened media container. The index
// is scoped to that one demuxer and is not a persistent track identity.
struct EmbeddedMediaStreamDescriptor {
    int streamIndex = -1;
    QString label;
    QString language;
    QString title;
    QString codec;
    bool isDefault = false;
    bool isForced = false;
    bool isHearingImpaired = false;
    bool isVisualImpaired = false;
    bool isCommentary = false;
    bool supported = false;
    std::optional<std::int64_t> endMicroseconds;

    bool isValid() const { return streamIndex >= 0 && !label.isEmpty() && !codec.isEmpty(); }
};
