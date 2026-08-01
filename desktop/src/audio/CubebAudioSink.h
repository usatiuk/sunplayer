#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "audio/AudioOutputLedger.h"
#include "audio/AudioSink.h"
#include "audio/RealtimePcmQueue.h"

// Physical shared-mode audio sink. The cubeb callback touches only the
// preallocated PCM queue, fixed-capacity output ledger, and atomics. Stream
// lifecycle, device queries, and error formatting remain on control/decode
// threads.
class CubebAudioSink final : public AudioSink {
public:
    explicit CubebAudioSink(
        std::size_t queueCapacityFrames = 96'000);
    ~CubebAudioSink() override;

    void reset(
        std::uint64_t playbackGeneration,
        AudioStreamFormat format) override;
    bool submit(
        PcmAudioBlock block,
        std::stop_token stopToken = {}) override;
    void cancel(std::uint64_t playbackGeneration) override;
    void finish(std::uint64_t playbackGeneration) override;
    void start() override;
    void pause() override;
    void setGain(float linearGain) override;
    AudioPresentationSnapshot snapshot() const override;
    std::string failureReason() const override;

    AudioSinkDiagnostics diagnostics() const override;

private:
    enum class Error {
        None,
        ComInitialization,
        ContextInitialization,
        UnsupportedFormat,
        StreamInitialization,
        StreamStart,
        StreamStop,
        Callback,
        InvalidPcm,
    };

    static std::string errorMessage(Error error);
    void destroyStream();
    void failEpoch(Error error);
    void failAndDestroyEpoch(
        Error error,
        std::uint64_t playbackGeneration);
    void maybeStart();
    long render(float *output, long requestedFrames);
    void handleState(int state);

    struct Impl;

    const std::size_t m_queueCapacityFrames;
    RealtimePcmQueue m_queue;
    AudioOutputLedger m_outputLedger;
    std::string m_backendName;
    AudioStreamFormat m_format;
    std::size_t m_prerollFrames = 0;
    std::uint32_t m_requestedLatencyFrames = 0;
    std::atomic<Error> m_error{Error::None};
    std::atomic<std::uint64_t> m_playbackGeneration{0};
    std::atomic<std::uint64_t> m_audioOutputEpoch{0};
    std::atomic<std::uint64_t> m_deviceFramesWritten{0};
    std::atomic<std::uint64_t> m_underrunFrames{0};
    std::atomic<std::uint64_t> m_deviceRevision{0};
    std::atomic<std::size_t> m_maximumSubmitFrames{0};
    std::atomic<float> m_gain{1.0F};
    std::atomic_bool m_wantsRunning{false};
    std::atomic_bool m_streamStarted{false};
    std::atomic_bool m_producerFinished{false};
    std::atomic_bool m_drained{false};
    std::atomic_bool m_streamReady{false};
    // An explicit pause/stop can provoke a backend DRAINED callback. Ignore
    // drain until a subsequent STARTED callback proves a new running epoch.
    std::atomic_bool m_ignoreDrainUntilStarted{false};
    std::atomic_bool m_deviceNotificationsAvailable{false};
    std::unique_ptr<Impl> m_impl;
};
