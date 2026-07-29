#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "media/DecodedVideoFrame.h"
#include "media/FfmpegFirstFrameDecoder.h"
#include "media/FfmpegHardwareDevice.h"
#include "video/DecodedVideoSource.h"

class MediaSession final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MediaSession is owned by the application")

    Q_PROPERTY(State state READ state NOTIFY sessionChanged)
    Q_PROPERTY(QUrl mediaUrl READ mediaUrl NOTIFY sessionChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY sessionChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY sessionChanged)
    Q_PROPERTY(QString containerFormat READ containerFormat NOTIFY sessionChanged)
    Q_PROPERTY(QString decoderName READ decoderName NOTIFY sessionChanged)
    Q_PROPERTY(QString decodePath READ decodePath NOTIFY sessionChanged)
    Q_PROPERTY(QString hardwareFallbackReason
               READ hardwareFallbackReason NOTIFY sessionChanged)
    Q_PROPERTY(QString videoSummary READ videoSummary NOTIFY sessionChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY sessionChanged)

public:
    enum class State {
        Empty,
        Opening,
        Ready,
        Error,
    };
    Q_ENUM(State)

    using DecodeOperation = std::function<
        FfmpegFirstFrameResult(
            const QString &,
            const VideoFrameIdentity &,
            const VideoHardwareDecodeCapability &,
            std::stop_token)>;

    explicit MediaSession(
        VideoTargetReadback readback,
        QObject *parent = nullptr);
    MediaSession(
        VideoTargetReadback readback,
        DecodeOperation decodeOperation,
        QObject *parent = nullptr);
    ~MediaSession() override;

    State state() const;
    QUrl mediaUrl() const;
    QString displayName() const;
    QString errorMessage() const;
    QString containerFormat() const;
    QString decoderName() const;
    QString decodePath() const;
    QString hardwareFallbackReason() const;
    QString videoSummary() const;
    bool hasFrame() const;
    std::uint64_t playbackGeneration() const;

    DecodedVideoSource &videoSource();
    const DecodedVideoSource &videoSource() const;
    void invalidateGraphicsDevice();
    void setVideoDecodeCapability(
        VideoHardwareDecodeCapability capability);

    Q_INVOKABLE void openMedia(const QUrl &url);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retry();

signals:
    void sessionChanged();

private:
    struct OpenRequest {
        std::uint64_t generation = 0;
        QString path;
        VideoFrameIdentity identity;
        VideoHardwareDecodeCapability hardwareDecode;
    };

    void startOpen(
        const QUrl &url,
        const QString &path,
        VideoHardwareDecodeCapability hardwareDecode);
    void submitOpen(OpenRequest request);
    void cancelOpen();
    void workerLoop(std::stop_token workerStopToken);
    void completeOpen(
        std::uint64_t generation,
        FfmpegFirstFrameResult result);
    void failWithoutWorker(
        const QUrl &url,
        const QString &message);
    void handlePresentationFailure(
        const VideoFailure &failure);
    void shutdownWorker();
    void advanceGeneration();
    void resetDiagnostics();

    DecodeOperation m_decodeOperation;
    DecodedVideoSource m_videoSource;
    State m_state = State::Empty;
    QUrl m_mediaUrl;
    QString m_displayName;
    QString m_errorMessage;
    QString m_containerFormat;
    QString m_decoderName;
    QString m_decodePath;
    QString m_hardwareFallbackReason;
    QString m_videoSummary;
    VideoHardwareDecodeCapability m_videoDecodeCapability;
    bool m_reopenAfterGraphicsRecovery = false;
    bool m_hardwareImportFallbackConsumed = false;
    std::uint64_t m_playbackGeneration = 1;
    std::mutex m_workerMutex;
    std::condition_variable_any m_workerWake;
    std::optional<OpenRequest> m_pendingOpen;
    std::stop_source m_operationStopSource;
    std::jthread m_worker;
};
