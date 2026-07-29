#pragma once

#include <QString>

enum class VideoFailureKind {
    None,
    General,
    HardwareFrameImportUnavailable,
};

struct VideoFailure {
    VideoFailureKind kind = VideoFailureKind::None;
    QString reason;

    bool isValid() const {
        return kind != VideoFailureKind::None
            && !reason.isEmpty();
    }
};
