#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "audio/AudioSink.h"
#include "media/DecodedVideoFrame.h"
#include "media/FfmpegMediaDecoder.h"
#include "media/FfmpegVideoDecoder.h"
#include "media/FfmpegHardwareDevice.h"
#include "playback/CoalescedGenerationWake.h"
#include "playback/VideoFrameQueue.h"
#include "playback/VideoFrameScheduler.h"
#include "video/DecodedVideoSource.h"

class MediaSession final
    : public QObject,
      private DecodedVideoFrameSelector {
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
    Q_PROPERTY(bool playing READ playing NOTIFY sessionChanged)
    Q_PROPERTY(bool ended READ ended NOTIFY sessionChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY timelineChanged)
    Q_PROPERTY(bool seeking READ seeking NOTIFY timelineChanged)
    Q_PROPERTY(qlonglong positionMilliseconds
               READ positionMilliseconds NOTIFY timelineChanged)
    Q_PROPERTY(qlonglong durationMilliseconds
               READ durationMilliseconds NOTIFY timelineChanged)
    Q_PROPERTY(qulonglong decodedVideoFrames
               READ decodedFrameCount NOTIFY playbackMetricsChanged)
    Q_PROPERTY(qulonglong selectedVideoFrames
               READ selectedFrameCount NOTIFY playbackMetricsChanged)
    Q_PROPERTY(qulonglong droppedVideoFrames
               READ droppedFrameCount NOTIFY playbackMetricsChanged)
    Q_PROPERTY(int queuedVideoFrames
               READ queuedVideoFrames NOTIFY playbackMetricsChanged)

public:
    enum class State {
        Empty,
        Opening,
        Ready,
        Error,
    };
    Q_ENUM(State)

    using DecodeOperation = std::function<
        FfmpegVideoDecodeResult(
            const FfmpegVideoDecodeRequest &,
            const FfmpegVideoFrameSink &,
            std::stop_token)>;
    using MediaDecodeOperation = std::function<
        FfmpegMediaDecodeResult(
            const FfmpegMediaDecodeRequest &,
            const FfmpegVideoFrameSink &,
            const FfmpegAudioOutputSink &,
            const FfmpegMediaStreamSink &,
            std::stop_token)>;

    explicit MediaSession(
        VideoTargetReadback readback,
        QObject *parent = nullptr);
    MediaSession(
        VideoTargetReadback readback,
        DecodeOperation decodeOperation,
        QObject *parent = nullptr);
    MediaSession(
        VideoTargetReadback readback,
        MediaDecodeOperation decodeOperation,
        std::shared_ptr<AudioSink> audioSink,
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
    bool playing() const;
    bool ended() const;
    bool seekable() const;
    bool seeking() const;
    qlonglong positionMilliseconds() const;
    qlonglong durationMilliseconds() const;
    std::uint64_t playbackGeneration() const;
    std::uint64_t decodedFrameCount() const;
    std::uint64_t selectedFrameCount() const;
    std::uint64_t droppedFrameCount() const;
    std::size_t queuedFrameCount() const;
    std::size_t maximumQueuedFrameCount() const;
    int queuedVideoFrames() const;
    std::optional<AudioPresentationSnapshot>
        currentAudioPresentation() const;

    DecodedVideoSource &videoSource();
    const DecodedVideoSource &videoSource() const;
    void invalidateGraphicsDevice();
    void setVideoDecodeCapability(
        VideoHardwareDecodeCapability capability);

    Q_INVOKABLE void openMedia(const QUrl &url);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seekToMilliseconds(
        qlonglong positionMilliseconds);

signals:
    void sessionChanged();
    void playbackMetricsChanged();
    void timelineChanged();

private:
    struct OpenRequest {
        std::uint64_t generation = 0;
        FfmpegMediaDecodeRequest decode;
    };

    void startOpen(
        const QUrl &url,
        const QString &path,
        VideoHardwareDecodeCapability hardwareDecode);
    void restartAt(
        std::int64_t positionMicroseconds,
        VideoHardwareDecodeCapability hardwareDecode,
        bool seeking);
    void startDecode(
        const QUrl &url,
        const QString &path,
        VideoHardwareDecodeCapability hardwareDecode,
        std::int64_t requestedPositionMicroseconds,
        bool newMedia,
        bool seeking);
    void submitOpen(OpenRequest request);
    void cancelPipeline();
    void workerLoop(std::stop_token workerStopToken);
    void completeDecode(
        std::uint64_t generation,
        FfmpegMediaDecodeResult result);
    void postFramesAvailable(
        std::uint64_t generation);
    bool recordDecodedFrame(
        std::uint64_t generation);
    void postPlaybackMetricsChanged(
        std::uint64_t generation);
    void handleFramesAvailable(
        std::uint64_t generation);
    void handleVideoFrameChanged();
    void failWithoutWorker(
        const QUrl &url,
        const QString &message);
    void handlePresentationFailure(
        const VideoFailure &failure);
    void shutdownWorker();
    void cancelAudioOutput();
    void advanceGeneration();
    void resetDiagnostics();
    void resetPlayback(
        std::int64_t positionMicroseconds = 0);
    void publishSessionAndPlaybackMetrics(
        std::uint64_t generation);
    void applyDiagnostics(
        const FfmpegVideoStreamDiagnostics &diagnostics);
    bool observeAudioOutput(
        std::chrono::steady_clock::time_point now,
        std::optional<AudioPresentationSnapshot> &observation);
    bool updateAudioOutputState(
        std::chrono::steady_clock::time_point now,
        const AudioPresentationSnapshot &snapshot);
    bool failCurrentAudioOutput(
        const AudioPresentationSnapshot &snapshot,
        const QString &fallbackReason = {});
    MediaClockSnapshot mediaClockSnapshotAt(
        std::chrono::steady_clock::time_point now,
        const AudioPresentationSnapshot *audio = nullptr) const;
    bool currentGenerationUsesAudioClock() const;
    bool currentGenerationStreamsDiscovered() const;
    std::optional<FfmpegVideoStreamDiagnostics>
        currentGenerationInitialVideoDiagnostics() const;
    bool enterReady(
        std::chrono::steady_clock::time_point now);
    void updateVideoSummary(const QueuedVideoFrame &frame);
    void monitorPlayback();

    std::shared_ptr<const DecodedVideoFrame>
        selectFrameForPresentation(
            std::chrono::steady_clock::time_point now) override;
    bool wantsContinuousVideoFrames() const override;

    MediaDecodeOperation m_decodeOperation;
    std::shared_ptr<AudioSink> m_audioSink;
    DecodedVideoSource m_videoSource;
    VideoFrameQueue m_frameQueue;
    VideoFrameScheduler m_frameScheduler;
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
    VideoHardwareDecodeCapability
        m_activeVideoDecodeCapability;
    bool m_reopenAfterGraphicsRecovery = false;
    std::int64_t m_graphicsRecoveryPositionMicroseconds = 0;
    bool m_graphicsRecoverySeeking = false;
    bool m_hardwareImportFallbackConsumed = false;
    bool m_userWantsPlaying = true;
    bool m_decoderDrained = false;
    bool m_ended = false;
    bool m_seekable = false;
    bool m_seeking = false;
    std::optional<std::int64_t> m_durationMicroseconds;
    std::optional<VideoTimelineOrigin> m_timelineOrigin;
    std::int64_t m_requestedPositionMicroseconds = 0;
    std::optional<std::chrono::steady_clock::time_point>
        m_clockAnchorTime;
    std::int64_t m_clockAnchorMediaMicroseconds = 0;
    bool m_audioClockEstablished = false;
    bool m_audioTailClockActive = false;
    std::uint64_t m_selectedFrameCount = 0;
    std::uint64_t m_droppedFrameCount = 0;
    std::uint64_t m_pendingPublicationGeneration = 0;
    bool m_sessionNotificationPending = false;
    bool m_playbackMetricsNotificationPending = false;
    std::uint64_t m_playbackGeneration = 1;
    CoalescedGenerationWake m_frameWake;
    CoalescedGenerationWake m_metricsWake;
    mutable std::mutex m_streamDiscoveryMutex;
    std::uint64_t m_streamDiscoveryGeneration = 0;
    bool m_streamDiscoveryComplete = false;
    bool m_audioOutputExpected = false;
    bool m_audioOutputEndedWithoutFrames = false;
    std::optional<FfmpegVideoStreamDiagnostics>
        m_initialVideoDiagnostics;
    std::optional<std::chrono::steady_clock::time_point>
        m_audioClockUnavailableSince;
    QTimer m_playbackMonitorTimer;
    std::atomic<std::uint64_t> m_audioSinkGeneration{0};
    std::atomic_bool m_audioPlayIntent{true};
    mutable std::mutex m_audioSinkLifecycleMutex;
    mutable std::mutex m_playbackMetricsMutex;
    std::uint64_t m_playbackMetricsGeneration = 1;
    std::uint64_t m_decodedFrameCount = 0;
    std::mutex m_workerMutex;
    std::condition_variable_any m_workerWake;
    std::optional<OpenRequest> m_pendingOpen;
    std::stop_source m_operationStopSource;
    std::jthread m_worker;
};
