#include "audio/CubebAudioSink.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <objbase.h>
#endif

extern "C" {
#include <cubeb/cubeb.h>
}

namespace {
constexpr AudioStreamFormat supportedFormat{48'000, 2};
#ifdef _WIN32
constexpr const char *requestedBackend = "wasapi";
#else
constexpr const char *requestedBackend = nullptr;
#endif
static_assert(std::atomic<float>::is_always_lock_free);

std::optional<std::int64_t> timestampForFrame(
        std::int64_t firstTimestamp,
        std::uint64_t frame,
        int sampleRate) {
    const std::uint64_t rate = static_cast<std::uint64_t>(
        sampleRate);
    const std::uint64_t wholeSeconds = frame / rate;
    const std::uint64_t remainingFrames = frame % rate;
    if (wholeSeconds
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()
                / 1'000'000)) {
        return std::nullopt;
    }
    const std::uint64_t offset = wholeSeconds * 1'000'000
        + remainingFrames * 1'000'000 / rate;
    if (offset
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto signedOffset = static_cast<std::int64_t>(offset);
    if (firstTimestamp
            > std::numeric_limits<std::int64_t>::max()
                - signedOffset) {
        return std::nullopt;
    }
    return firstTimestamp + signedOffset;
}
}

struct CubebAudioSink::Impl {
    explicit Impl(CubebAudioSink &sink)
        : owner(sink) {
        std::future<void> ready = initialized.get_future();
        controlThread = std::jthread(
            [this](std::stop_token stopToken) {
                run(stopToken);
            });
        ready.get();
    }

    ~Impl() {
        shutdown();
    }

    void shutdown() {
        if (shutdownStarted.exchange(
                true, std::memory_order_acq_rel)) {
            return;
        }
        controlThread.request_stop();
        commandReady.notify_all();
        if (controlThread.joinable())
            controlThread.join();
    }

    template<typename Function>
    auto invoke(Function function)
            -> std::invoke_result_t<Function> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::move(function));
        std::future<Result> result = task->get_future();
        {
            std::lock_guard lock(commandMutex);
            commands.emplace_back([task] { (*task)(); });
        }
        commandReady.notify_one();
        if constexpr (std::is_void_v<Result>) {
            result.get();
        } else {
            return result.get();
        }
    }

    void requestStartCheck() {
        if (startCheckPending.exchange(
                true, std::memory_order_acq_rel)) {
            return;
        }
        {
            std::lock_guard lock(commandMutex);
            commands.emplace_back([this] {
                startCheckPending.store(
                    false, std::memory_order_release);
                owner.maybeStart();
            });
        }
        commandReady.notify_one();
    }

    static long dataCallback(
            cubeb_stream *,
            void *user,
            const void *,
            void *output,
            long requestedFrames) {
        auto &self = *static_cast<Impl *>(user);
        return self.owner.render(
            static_cast<float *>(output),
            requestedFrames);
    }

    static void stateCallback(
            cubeb_stream *,
            void *user,
            cubeb_state state) {
        auto &self = *static_cast<Impl *>(user);
        self.owner.handleState(static_cast<int>(state));
    }

    static void deviceChangedCallback(void *user) {
        auto &self = *static_cast<Impl *>(user);
        self.owner.m_deviceRevision.fetch_add(
            1, std::memory_order_relaxed);
    }

    static void deviceCollectionChangedCallback(
            cubeb *,
            void *user) {
        deviceChangedCallback(user);
    }

    void run(std::stop_token stopToken) {
#ifdef _WIN32
        const HRESULT comResult = CoInitializeEx(
            nullptr, COINIT_MULTITHREADED);
        const bool comInitialized = SUCCEEDED(comResult);
        if (!comInitialized) {
            owner.m_error.store(
                CubebAudioSink::Error::ComInitialization,
                std::memory_order_release);
        } else
#endif
        if (cubeb_init(
                &context, "Sunroom", requestedBackend) != CUBEB_OK) {
            owner.m_error.store(
                CubebAudioSink::Error::ContextInitialization,
                std::memory_order_release);
        } else {
            const char *backend = cubeb_get_backend_id(context);
            if (backend)
                owner.m_backendName = backend;
            owner.m_deviceNotificationsAvailable.store(
                cubeb_register_device_collection_changed(
                    context,
                    CUBEB_DEVICE_TYPE_OUTPUT,
                    &Impl::deviceCollectionChangedCallback,
                    this) == CUBEB_OK,
                std::memory_order_release);
        }
        initialized.set_value();

        while (true) {
            std::function<void()> command;
            {
                std::unique_lock lock(commandMutex);
                commandReady.wait(
                    lock,
                    stopToken,
                    [this] { return !commands.empty(); });
                if (commands.empty()) {
                    if (stopToken.stop_requested())
                        break;
                    continue;
                }
                command = std::move(commands.front());
                commands.pop_front();
            }
            command();
        }

        owner.destroyStream();
        if (context) {
            if (owner.m_deviceNotificationsAvailable.load(
                    std::memory_order_acquire)) {
                cubeb_register_device_collection_changed(
                    context,
                    CUBEB_DEVICE_TYPE_OUTPUT,
                    nullptr,
                    nullptr);
            }
            cubeb_destroy(context);
        }
#ifdef _WIN32
        if (comInitialized)
            CoUninitialize();
#endif
    }

    CubebAudioSink &owner;
    cubeb *context = nullptr;
    cubeb_stream *stream = nullptr;
    std::promise<void> initialized;
    std::mutex commandMutex;
    std::condition_variable_any commandReady;
    std::deque<std::function<void()>> commands;
    std::atomic_bool startCheckPending{false};
    std::atomic_bool shutdownStarted{false};
    std::jthread controlThread;
};

CubebAudioSink::CubebAudioSink(
        std::size_t queueCapacityFrames)
    : m_queueCapacityFrames(
          std::max<std::size_t>(2, queueCapacityFrames)),
      m_queue(m_queueCapacityFrames, 2),
      m_outputLedger(4'096),
      m_impl(std::make_unique<Impl>(*this)) {}

CubebAudioSink::~CubebAudioSink() {
    m_impl->shutdown();
}

void CubebAudioSink::reset(
        std::uint64_t playbackGeneration,
        AudioStreamFormat format) {
    m_impl->invoke([this, playbackGeneration, format] {
        destroyStream();
        m_streamReady.store(false, std::memory_order_relaxed);
        m_format = format;
        m_playbackGeneration.store(
            playbackGeneration,
            std::memory_order_relaxed);
        std::uint64_t audioOutputEpoch =
            m_audioOutputEpoch.load(std::memory_order_relaxed) + 1;
        if (audioOutputEpoch == 0)
            audioOutputEpoch = 1;
        m_audioOutputEpoch.store(
            audioOutputEpoch,
            std::memory_order_release);
        m_deviceFramesWritten.store(0, std::memory_order_relaxed);
        m_underrunFrames.store(0, std::memory_order_relaxed);
        m_wantsRunning.store(false, std::memory_order_relaxed);
        m_streamStarted.store(false, std::memory_order_relaxed);
        m_producerFinished.store(false, std::memory_order_relaxed);
        m_drained.store(false, std::memory_order_relaxed);
        m_ignoreDrainUntilStarted.store(
            false, std::memory_order_relaxed);
        m_maximumSubmitFrames.store(0, std::memory_order_relaxed);
        m_queue.reset(playbackGeneration, supportedFormat);
        m_outputLedger.reset();

        if (!m_impl->context) {
            if (m_error.load(std::memory_order_acquire)
                    == Error::None) {
                m_error.store(
                    Error::ContextInitialization,
                    std::memory_order_release);
            }
            m_queue.cancel();
            return;
        }
        if (format != supportedFormat) {
            m_error.store(
                Error::UnsupportedFormat,
                std::memory_order_release);
            m_queue.cancel();
            return;
        }

        cubeb_stream_params outputParameters{
            .format = CUBEB_SAMPLE_FLOAT32NE,
            .rate = static_cast<std::uint32_t>(
                format.sampleRate),
            .channels = static_cast<std::uint32_t>(
                format.channelCount),
            .layout = CUBEB_LAYOUT_STEREO,
            .prefs = CUBEB_STREAM_PREF_NONE,
        };
        std::uint32_t minimumLatency = 0;
        if (cubeb_get_min_latency(
                m_impl->context,
                &outputParameters,
                &minimumLatency) != CUBEB_OK
                || minimumLatency == 0) {
            minimumLatency = static_cast<std::uint32_t>(
                format.sampleRate / 100);
        }
        m_requestedLatencyFrames = minimumLatency;
        m_prerollFrames = std::max<std::size_t>(
            1,
            std::min(
                m_queueCapacityFrames / 2,
                std::max<std::size_t>(
                static_cast<std::size_t>(
                    format.sampleRate / 10),
                static_cast<std::size_t>(
                    minimumLatency) * 2)));
        m_maximumSubmitFrames.store(
            m_queueCapacityFrames - m_prerollFrames,
            std::memory_order_release);

        const int streamResult = cubeb_stream_init(
            m_impl->context,
            &m_impl->stream,
            "Sunroom playback",
            nullptr,
            nullptr,
            nullptr,
            &outputParameters,
            minimumLatency,
            &Impl::dataCallback,
            &Impl::stateCallback,
            m_impl.get());
        if (streamResult != CUBEB_OK) {
            m_impl->stream = nullptr;
            m_error.store(
                Error::StreamInitialization,
                std::memory_order_release);
            m_queue.cancel();
            return;
        }
        m_streamReady.store(true, std::memory_order_release);
        m_error.store(Error::None, std::memory_order_release);
    });
}

bool CubebAudioSink::submit(
        PcmAudioBlock block,
        std::stop_token stopToken) {
    const std::uint64_t generation = block.playbackGeneration;
    if (generation != m_playbackGeneration.load(
            std::memory_order_acquire)
            || !m_streamReady.load(std::memory_order_acquire)) {
        return false;
    }
    if (block.frameCount()
            > m_maximumSubmitFrames.load(
                std::memory_order_acquire)) {
        failAndDestroyEpoch(Error::InvalidPcm, generation);
        return false;
    }
    const RealtimePcmSubmitResult submitted =
        m_queue.submitResult(block, stopToken);
    if (submitted != RealtimePcmSubmitResult::Accepted) {
        if (submitted == RealtimePcmSubmitResult::Invalid) {
            failAndDestroyEpoch(Error::InvalidPcm, generation);
        }
        return false;
    }
    if (generation != m_playbackGeneration.load(
            std::memory_order_acquire)
            || !m_streamReady.load(std::memory_order_acquire)) {
        return false;
    }
    m_impl->requestStartCheck();
    return true;
}

void CubebAudioSink::cancel(
        std::uint64_t playbackGeneration) {
    m_impl->invoke([this, playbackGeneration] {
        if (playbackGeneration
                != m_playbackGeneration.load(
                    std::memory_order_acquire)) {
            return;
        }
        m_wantsRunning.store(false, std::memory_order_release);
        destroyStream();
        m_playbackGeneration.store(0, std::memory_order_release);
        m_format = {};
        m_producerFinished.store(false, std::memory_order_release);
        m_drained.store(false, std::memory_order_release);
        m_outputLedger.reset();
    });
}

void CubebAudioSink::finish(
        std::uint64_t playbackGeneration) {
    m_impl->invoke([this, playbackGeneration] {
        if (playbackGeneration
                != m_playbackGeneration.load(
                    std::memory_order_acquire)) {
            return;
        }
        m_producerFinished.store(true, std::memory_order_release);
        maybeStart();
    });
}

void CubebAudioSink::start() {
    m_wantsRunning.store(true, std::memory_order_release);
    m_impl->invoke([this] { maybeStart(); });
}

void CubebAudioSink::pause() {
    m_wantsRunning.store(false, std::memory_order_release);
    m_impl->invoke([this] {
        if (!m_impl->stream
                || !m_streamStarted.load(
                    std::memory_order_acquire)) {
            return;
        }
        m_ignoreDrainUntilStarted.store(
            true, std::memory_order_release);
        if (cubeb_stream_stop(m_impl->stream) != CUBEB_OK) {
            failEpoch(Error::StreamStop);
            cubeb_stream_destroy(m_impl->stream);
            m_impl->stream = nullptr;
            return;
        }
        m_streamStarted.store(false, std::memory_order_release);
    });
}

void CubebAudioSink::setGain(float linearGain) {
    if (!std::isfinite(linearGain))
        linearGain = 1.0F;
    m_gain.store(
        std::clamp(linearGain, 0.0F, 1.0F),
        std::memory_order_release);
}

AudioPresentationSnapshot CubebAudioSink::snapshot() const {
    return m_impl->invoke([this] {
        const Error error = m_error.load(
            std::memory_order_acquire);
        const std::optional<std::int64_t> firstMedia =
            m_queue.firstMediaTimestampMicroseconds();
        std::uint64_t presentedFrames = 0;
        const bool hasPosition = m_impl->stream
            && cubeb_stream_get_position(
                m_impl->stream,
                &presentedFrames) == CUBEB_OK;
        const std::uint64_t written =
            m_deviceFramesWritten.load(
                std::memory_order_acquire);
        presentedFrames = std::min(presentedFrames, written);
        const std::optional<AudioOutputPosition> outputPosition =
            hasPosition
            ? m_outputLedger.positionForOutputFrame(
                presentedFrames)
            : std::nullopt;
        const bool drained = m_drained.load(
            std::memory_order_acquire);
        const bool terminalPositionAvailable = drained
            && firstMedia.has_value();
        const std::uint64_t terminalMediaFrames =
            m_outputLedger.mediaFrames();
        // CUBEB_STATE_DRAINED is the authoritative completion boundary. A
        // backend position may stop updating just before the final buffered
        // frames drain, so do not let a still-queryable stale clock move the
        // terminal media endpoint backward.
        const std::uint64_t mediaPositionFrames =
            terminalPositionAvailable
                ? terminalMediaFrames
                : outputPosition
                    ? outputPosition->mediaFrame
                    : 0;
        const std::optional<std::int64_t> position =
            firstMedia && (outputPosition
                || terminalPositionAvailable)
            ? timestampForFrame(
                *firstMedia,
                mediaPositionFrames,
                m_format.sampleRate)
            : std::nullopt;
        const bool valid = hasPosition
            && outputPosition.has_value()
            && position.has_value()
            && error == Error::None;
        return AudioPresentationSnapshot{
            .playbackGeneration =
                m_playbackGeneration.load(
                    std::memory_order_relaxed),
            .audioOutputEpoch =
                m_audioOutputEpoch.load(
                    std::memory_order_acquire),
            .submittedFrames = m_outputLedger.mediaFrames(),
            .presentedFrames = mediaPositionFrames,
            .mediaPositionMicroseconds = position
                ? *position
                : firstMedia.value_or(0),
            .producerFinished = m_producerFinished.load(
                std::memory_order_acquire),
            .drained = drained,
            .holding = valid
                && !drained
                && outputPosition->holding,
            .advancing = valid
                && m_streamStarted.load(
                    std::memory_order_acquire)
                && !drained
                && !outputPosition->holding,
            .terminalPositionValid = terminalPositionAvailable
                && position.has_value(),
            .failed = error != Error::None,
            .valid = valid,
        };
    });
}

std::string CubebAudioSink::failureReason() const {
    return errorMessage(m_error.load(std::memory_order_acquire));
}

AudioSinkDiagnostics CubebAudioSink::diagnostics() const {
    return m_impl->invoke([this] {
        std::optional<std::uint32_t> reportedLatency;
        if (m_impl->stream) {
            std::uint32_t value = 0;
            if (cubeb_stream_get_latency(
                    m_impl->stream,
                    &value) == CUBEB_OK) {
                reportedLatency = value;
            }
        }
        std::uint64_t deviceFramesPresented = 0;
        const bool hasDevicePosition = m_impl->stream
            && cubeb_stream_get_position(
                m_impl->stream,
                &deviceFramesPresented) == CUBEB_OK;
        if (hasDevicePosition) {
            deviceFramesPresented = std::min(
                deviceFramesPresented,
                m_deviceFramesWritten.load(
                    std::memory_order_acquire));
        }
        const std::optional<AudioOutputPosition> outputPosition =
            hasDevicePosition
            ? m_outputLedger.positionForOutputFrame(
                deviceFramesPresented)
            : std::nullopt;
        const Error error = m_error.load(
            std::memory_order_acquire);
        const bool drained = m_drained.load(
            std::memory_order_acquire);
        const std::uint64_t terminalMediaFrames =
            m_outputLedger.mediaFrames();
        const bool terminalPositionAvailable = drained
            && terminalMediaFrames != 0;
        const bool reliable = ((hasDevicePosition
                    && outputPosition.has_value())
                || terminalPositionAvailable)
            && error == Error::None;
        return AudioSinkDiagnostics{
            .backendName = m_backendName,
            .errorMessage = errorMessage(error),
            .format = m_format,
            .queueCapacityFrames = m_queueCapacityFrames,
            .maximumSubmitFrames =
                m_maximumSubmitFrames.load(
                    std::memory_order_relaxed),
            .queuedFrames = m_queue.queuedFrames(),
            .maximumQueuedFrames =
                m_queue.maximumObservedQueuedFrames(),
            .requestedLatencyFrames =
                m_requestedLatencyFrames,
            .reportedLatencyFrames = reportedLatency,
            .mediaFramesSubmitted =
                terminalMediaFrames,
            .mediaFramesPresented =
                terminalPositionAvailable
                ? terminalMediaFrames
                : outputPosition
                ? outputPosition->mediaFrame
                : 0,
            .deviceFramesWritten =
                m_deviceFramesWritten.load(
                    std::memory_order_relaxed),
            .deviceFramesPresented = hasDevicePosition
                ? std::optional<std::uint64_t>(
                    deviceFramesPresented)
                : std::nullopt,
            .underrunFrames = m_underrunFrames.load(
                std::memory_order_relaxed),
            .audioOutputEpoch =
                m_audioOutputEpoch.load(
                    std::memory_order_acquire),
            .deviceRevision = m_deviceRevision.load(
                std::memory_order_relaxed),
            .streamOpen = m_impl->stream != nullptr,
            .followsSystemDefault = true,
            .positionAvailable = hasDevicePosition,
            .deviceNotificationsAvailable =
                m_deviceNotificationsAvailable.load(
                    std::memory_order_relaxed),
            .clockReliable = reliable,
        };
    });
}

std::string CubebAudioSink::errorMessage(Error error) {
    switch (error) {
    case Error::None:
        return {};
    case Error::ComInitialization:
        return "Could not initialize the MTA audio control thread";
    case Error::ContextInitialization:
        return "Could not initialize the cubeb audio backend";
    case Error::UnsupportedFormat:
        return "The requested audio output format is unsupported";
    case Error::StreamInitialization:
        return "Could not open the default audio output device";
    case Error::StreamStart:
        return "Could not start audio output";
    case Error::StreamStop:
        return "Could not stop audio output cleanly";
    case Error::Callback:
        return "The audio output callback failed";
    case Error::InvalidPcm:
        return "Decoded PCM violated the audio sink contract";
    }
    return "Unknown audio output error";
}

void CubebAudioSink::destroyStream() {
    m_streamReady.store(false, std::memory_order_release);
    m_queue.cancel();
    if (!m_impl->stream)
        return;
    cubeb_stream_stop(m_impl->stream);
    cubeb_stream_destroy(m_impl->stream);
    m_impl->stream = nullptr;
    m_streamStarted.store(false, std::memory_order_release);
}

void CubebAudioSink::failEpoch(Error error) {
    m_streamReady.store(false, std::memory_order_release);
    m_streamStarted.store(false, std::memory_order_release);
    m_error.store(error, std::memory_order_release);
    m_queue.cancel();
}

void CubebAudioSink::failAndDestroyEpoch(
        Error error,
        std::uint64_t playbackGeneration) {
    m_impl->invoke([this, error, playbackGeneration] {
        if (playbackGeneration
                != m_playbackGeneration.load(
                    std::memory_order_acquire)) {
            return;
        }
        failEpoch(error);
        destroyStream();
    });
}

void CubebAudioSink::maybeStart() {
    if (!m_impl->stream
            || !m_streamReady.load(std::memory_order_acquire)
            || !m_wantsRunning.load(std::memory_order_acquire)
            || m_streamStarted.load(std::memory_order_acquire)
            || (m_queue.queuedFrames() < m_prerollFrames
                && !m_producerFinished.load(
                    std::memory_order_acquire))) {
        return;
    }
    if (cubeb_stream_start(m_impl->stream) != CUBEB_OK) {
        failEpoch(Error::StreamStart);
        return;
    }
    m_streamStarted.store(true, std::memory_order_release);
}

long CubebAudioSink::render(
        float *output,
        long requestedFrames) {
    if (!output || requestedFrames <= 0) {
        m_error.store(Error::Callback, std::memory_order_release);
        return CUBEB_ERROR;
    }
    const std::size_t frames =
        static_cast<std::size_t>(requestedFrames);
    const std::size_t samples = frames
        * static_cast<std::size_t>(m_format.channelCount);
    const RealtimePcmRead read = m_queue.consume(
        std::span<float>(output, samples),
        frames);
    const float gain = m_gain.load(std::memory_order_acquire);
    if (gain != 1.0F) {
        std::transform(
            output,
            output + samples,
            output,
            [gain](float sample) { return sample * gain; });
    }
    if (read.silentFrames != 0
            && m_producerFinished.load(
                std::memory_order_acquire)) {
        m_outputLedger.record(read.mediaFrames, 0);
        m_deviceFramesWritten.fetch_add(
            read.mediaFrames,
            std::memory_order_release);
        return static_cast<long>(read.mediaFrames);
    }
    if (read.silentFrames != 0) {
        m_underrunFrames.fetch_add(
            read.silentFrames,
            std::memory_order_relaxed);
    }
    m_outputLedger.record(
        read.mediaFrames, read.silentFrames);
    m_deviceFramesWritten.fetch_add(
        frames,
        std::memory_order_release);
    return requestedFrames;
}

void CubebAudioSink::handleState(int state) {
    switch (static_cast<cubeb_state>(state)) {
    case CUBEB_STATE_STARTED:
        m_ignoreDrainUntilStarted.store(
            false, std::memory_order_release);
        m_streamStarted.store(true, std::memory_order_release);
        break;
    case CUBEB_STATE_STOPPED:
        m_streamStarted.store(false, std::memory_order_release);
        break;
    case CUBEB_STATE_DRAINED:
        m_streamStarted.store(false, std::memory_order_release);
        if (!m_ignoreDrainUntilStarted.load(
                    std::memory_order_acquire)
                && m_producerFinished.load(
                    std::memory_order_acquire)
                && m_wantsRunning.load(
                    std::memory_order_acquire)) {
            m_drained.store(true, std::memory_order_release);
        }
        break;
    case CUBEB_STATE_ERROR:
        failEpoch(Error::Callback);
        break;
    }
}
