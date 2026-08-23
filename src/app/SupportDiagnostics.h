#pragma once

#include <optional>

#include <QString>
#include <QUrl>

enum class SupportMediaState {
    Empty,
    Opening,
    Ready,
    Error,
};

struct SupportMediaSnapshot {
    SupportMediaState state = SupportMediaState::Empty;
    bool hasVideoFrame = false;
    bool videoHdr = false;
    bool hasAudioOutput = false;
    qint64 durationMilliseconds = -1;
    quint64 decodedVideoFrames = 0;
    quint64 droppedVideoFrames = 0;
    QString decoder;
    QString decodePath;
    QString audioBackend;
};

struct SupportSnapshot {
    QString version;
    QString buildId;
    QString qtVersion;
    QString operatingSystem;
    QString cpuArchitecture;
    bool debugLoggingEnabled = false;
    QString graphicsApi;
    QString swapChainFormat;
    QString videoSurfaceProducer;
    QString videoColorPolicy;
    QString displayColorMode;
    bool displayHdrEnabled = false;
    bool hdrPresentationActive = false;
    bool sdrWhiteKnown = false;
    float sdrWhiteNits = 0.0f;
    float effectiveTargetHeadroom = 1.0f;
    std::optional<SupportMediaSnapshot> currentMedia;
    std::optional<SupportMediaSnapshot> lastMedia;
    QString applicationErrorCode;
    QString applicationErrorSubsystem;
};

namespace SupportDiagnostics {
QString sanitizedToken(QString const& value, qsizetype maximumLength = 96);
QString detailedReport(SupportSnapshot const& snapshot);
QString issueBody(SupportSnapshot const& snapshot);
QUrl issueUrl(QUrl const& baseUrl, SupportSnapshot const& snapshot);
} // namespace SupportDiagnostics
