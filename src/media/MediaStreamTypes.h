#pragma once

#include <cstdint>
#include <optional>

#include <QString>

enum class SubtitleStreamKind {
    Unknown,
    Text,
    Bitmap,
};

// A stream discovered inside the currently opened media container. The index
// is scoped to that one demuxer and is not a persistent track identity.
struct EmbeddedMediaStreamDescriptor {
    int streamIndex = -1;
    QString label;
    QString language;
    QString title;
    QString codec;
    QString channelLayout;
    int sampleRate = 0;
    SubtitleStreamKind subtitleKind = SubtitleStreamKind::Unknown;
    bool isDefault = false;
    bool isForced = false;
    bool isHearingImpaired = false;
    bool isVisualImpaired = false;
    bool isCommentary = false;
    bool supported = false;
    std::optional<std::int64_t> endMicroseconds;

    bool isValid() const { return streamIndex >= 0 && !label.isEmpty() && !codec.isEmpty(); }
};
