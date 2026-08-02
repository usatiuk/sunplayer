#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QSize>
#include <QString>

enum class SubtitlePayloadType {
    AssText,
    Bitmap,
    Clear,
};

struct SubtitleTrackDescriptor {
    int streamIndex = -1;
    QString label;
    QString language;
    QString title;
    QString codec;
    bool isDefault = false;
    bool isForced = false;
    bool isHearingImpaired = false;
    bool isCommentary = false;
    bool supported = false;

    bool isValid() const;
};

struct SubtitleFontAttachment {
    QString name;
    QByteArray bytes;
};

struct SubtitleStreamConfiguration {
    std::uint64_t playbackGeneration = 0;
    int streamIndex = -1;
    QString codec;
    QByteArray codecPrivate;
    QSize canvasSize;
    std::vector<SubtitleFontAttachment> fonts;

    bool isValid() const;
};

struct SubtitleBitmapRegion {
    int x = 0;
    int y = 0;
    QSize size;
    QByteArray rgba;

    bool isValid() const;
};

struct SubtitleBitmapComposition {
    QSize canvasSize;
    std::vector<SubtitleBitmapRegion> regions;

    bool isValid() const;
};

struct SubtitleEvent {
    std::uint64_t playbackGeneration = 0;
    std::int64_t startMicroseconds = 0;
    std::optional<std::int64_t> endMicroseconds;
    SubtitlePayloadType type = SubtitlePayloadType::Clear;
    QByteArray ass;
    std::shared_ptr<const SubtitleBitmapComposition> bitmap;

    bool isValid() const;
};

struct SubtitleStateSnapshot {
    std::uint64_t playbackGeneration = 0;
    std::uint64_t revision = 0;
    std::shared_ptr<const SubtitleStreamConfiguration> configuration;
    std::shared_ptr<const std::vector<SubtitleEvent>> events;
    QString error;

    bool isEnabled() const;
};

struct SubtitlePresentationSnapshot {
    SubtitleStateSnapshot state;
    std::int64_t mediaTimeMicroseconds = 0;
};
