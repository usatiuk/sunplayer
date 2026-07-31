#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QtTest>

extern "C" {
#include <libavutil/frame.h>
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "media/DecodedVideoFrame.h"
#include "media/FfmpegMediaDecoder.h"
#include "audio/ControlledAudioSink.h"

namespace {
QString synchronizedFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-flac-sync.mkv");
}

QString synchronizedManifestPath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-flac-sync.toml");
}

QString videoOnlyFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1.mkv");
}

FfmpegMediaDecodeRequest requestFor(
        const QString &path,
        std::uint64_t generation = 1) {
    return {
        .video = {
            .path = path,
            .firstFrameIdentity = {
                .playbackGeneration = generation,
                .decoderRevision = 1,
                .frameId = 1,
            },
            .hardwareDecode = {
                .device = {},
                .unavailableReason = QStringLiteral(
                    "Deterministic software decode"),
            },
        },
    };
}

struct DecodeCapture {
    std::vector<std::shared_ptr<const DecodedVideoFrame>> video;
    std::vector<PcmAudioBlock> audio;
    std::optional<FfmpegVideoStreamDiagnostics> videoDiagnostics;
    std::optional<FfmpegAudioStreamDiagnostics> audioDiagnostics;
};

FfmpegMediaDecodeResult decode(
        const FfmpegMediaDecodeRequest &request,
        DecodeCapture &capture) {
    return decodeMediaFrames(
        request,
        [&capture](
                std::shared_ptr<const DecodedVideoFrame> frame,
                const FfmpegVideoStreamDiagnostics &diagnostics) {
            capture.videoDiagnostics = diagnostics;
            capture.video.push_back(std::move(frame));
            return true;
        },
        [&capture](
                PcmAudioBlock block,
                const FfmpegAudioStreamDiagnostics &diagnostics,
                std::stop_token) {
            capture.audioDiagnostics = diagnostics;
            capture.audio.push_back(std::move(block));
            return true;
        });
}

std::vector<float> flattenAudio(
        const std::vector<PcmAudioBlock> &blocks) {
    std::vector<float> samples;
    for (const PcmAudioBlock &block : blocks) {
        samples.insert(
            samples.end(),
            block.samples.begin(),
            block.samples.end());
    }
    return samples;
}

float strongestLeftSample(
        const std::vector<float> &samples,
        std::size_t centerFrame,
        std::size_t radius) {
    float maximum = 0.0F;
    const std::size_t first = centerFrame - radius;
    const std::size_t last = centerFrame + radius;
    for (std::size_t frame = first; frame <= last; ++frame) {
        maximum = std::max(
            maximum,
            std::abs(samples[frame * 2]));
    }
    return maximum;
}

QByteArray expectedFixtureHash(const QString &manifestPath) {
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly))
        return {};
    const QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral(
            "^sha256\\s*=\\s*\"([0-9a-f]{64})\"\\s*$"),
        QRegularExpression::MultilineOption)
        .match(QString::fromUtf8(manifest.readAll()));
    return match.hasMatch()
        ? match.captured(1).toLatin1()
        : QByteArray();
}

QByteArray fixtureHash(const QString &fixturePath) {
    QFile fixture(fixturePath);
    if (!fixture.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(
        fixture.readAll(), QCryptographicHash::Sha256)
        .toHex();
}

float strongestLeftSampleInRange(
        const std::vector<float> &samples,
        std::size_t firstFrame,
        std::size_t lastFrame) {
    float maximum = 0.0F;
    for (std::size_t frame = firstFrame;
            frame < lastFrame;
            ++frame) {
        maximum = std::max(
            maximum,
            std::abs(samples[frame * 2]));
    }
    return maximum;
}
}

class FfmpegMediaDecoderTest final : public QObject {
    Q_OBJECT

public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(
            SEM_FAILCRITICALERRORS
            | SEM_NOGPFAULTERRORBOX
            | SEM_NOOPENFILEERRORBOX);
#endif
    }

private slots:
    void rejectsUnsupportedOutputFormat();
    void decodesSynchronizedFixtureThroughOneDemuxOperation();
    void streamsRealDecodeThroughBoundedSink();
    void seeksAndTrimsAudioOnTheSharedTimeline();
    void preservesVideoOnlyPlayback();
    void distinguishesVideoSinkStopFromCancellation();
    void distinguishesAudioSinkStopFromCancellation();
    void cancelsWhileAudioSubmissionIsBackpressured();
    void honorsCancellationBeforeOpeningMedia();
};

void FfmpegMediaDecoderTest::rejectsUnsupportedOutputFormat() {
    FfmpegMediaDecodeRequest request =
        requestFor(synchronizedFixturePath());
    request.audioOutput = {48'000, 1};
    DecodeCapture capture;
    const FfmpegMediaDecodeResult result = decode(request, capture);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error, QStringLiteral("Media decode request is invalid"));
    QVERIFY(capture.audio.empty());
    QVERIFY(capture.video.empty());
}

void FfmpegMediaDecoderTest::
decodesSynchronizedFixtureThroughOneDemuxOperation() {
    const QByteArray declaredHash =
        expectedFixtureHash(synchronizedManifestPath());
    QVERIFY2(!declaredHash.isEmpty(),
        "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(synchronizedFixturePath()), declaredHash);

    DecodeCapture capture;
    const FfmpegMediaDecodeResult result = decode(
        requestFor(synchronizedFixturePath()), capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(result.audioStreamPresent);
    QVERIFY(result.audioEndOfStream);
    QCOMPARE(result.video.framesDecoded, 12U);
    QVERIFY(result.decodedAudioFrames > 0);
    QCOMPARE(result.outputAudioFrames, 144'000U);
    QCOMPARE(capture.video.size(), 12U);
    QVERIFY(!capture.audio.empty());
    QVERIFY(capture.videoDiagnostics);
    QVERIFY(capture.audioDiagnostics);
    QCOMPARE(
        capture.videoDiagnostics->durationMicroseconds,
        std::optional<std::int64_t>(8'000'000));
    QVERIFY(!capture.videoDiagnostics->durationFinal);
    QCOMPARE(
        result.video.diagnostics.durationMicroseconds,
        std::optional<std::int64_t>(3'000'000));
    QVERIFY(result.video.diagnostics.durationFinal);
    QCOMPARE(
        result.video.observedEndMicroseconds,
        std::optional<std::int64_t>(3'000'000));
    QCOMPARE(
        result.observedAudioEndMicroseconds,
        std::optional<std::int64_t>(3'000'000));
    QVERIFY(result.packetCountLimit > 0);
    QVERIFY(result.packetByteLimit > 0);
    QVERIFY(result.maximumQueuedPacketCount
        <= result.packetCountLimit);
    QVERIFY(result.maximumQueuedPacketBytes
        <= std::max(
            result.packetByteLimit,
            result.largestQueuedPacketBytes));
    QCOMPARE(
        capture.videoDiagnostics->timelineOrigin->microseconds(),
        std::optional<std::int64_t>(5'000'000));
    QCOMPARE(
        capture.audioDiagnostics->outputFormat,
        AudioStreamFormat(48'000, 2));
    QCOMPARE(capture.audioDiagnostics->sourceSampleRate, 32'000);
    QCOMPARE(capture.audioDiagnostics->sourceChannelCount, 1);

    for (std::size_t index = 0;
            index < capture.video.size();
            ++index) {
        const DecodedVideoFrame &frame = *capture.video[index];
        QCOMPARE(frame.identity().playbackGeneration, 1U);
        QCOMPARE(
            frame.timing().ptsMicroseconds(),
            std::optional<std::int64_t>(
                5'000'000
                + static_cast<std::int64_t>(index) * 250'000));
        const AVFrame &avFrame = frame.ffmpegFrame();
        QVERIFY(avFrame.data[0] != nullptr);
        const int luma = avFrame.data[0][
            32 * avFrame.linesize[0] + 48];
        const bool white = index == 2 || index == 6 || index == 10;
        if (white)
            QVERIFY(luma >= 230);
        else
            QVERIFY(luma <= 20);
    }

    std::uint64_t expectedFrame = 0;
    for (const PcmAudioBlock &block : capture.audio) {
        QVERIFY(block.isValid());
        QCOMPARE(block.playbackGeneration, 1U);
        QCOMPARE(block.streamFrameIndex, expectedFrame);
        const std::int64_t expectedTime =
            static_cast<std::int64_t>(
                expectedFrame * 1'000'000ULL / 48'000ULL);
        QVERIFY(std::abs(
            block.mediaStartMicroseconds - expectedTime) <= 1);
        for (std::size_t frame = 0;
                frame < block.frameCount();
                ++frame) {
            const float left = block.samples[frame * 2];
            const float right = block.samples[frame * 2 + 1];
            QVERIFY(std::isfinite(left));
            QVERIFY(std::isfinite(right));
            QCOMPARE(left, right);
        }
        expectedFrame += block.frameCount();
    }
    QCOMPARE(expectedFrame, 144'000U);

    const std::vector<float> samples =
        flattenAudio(capture.audio);
    QCOMPARE(samples.size(), 288'000U);
    for (const std::size_t impulse :
            {24'000U, 72'000U, 120'000U}) {
        QVERIFY(strongestLeftSample(samples, impulse, 32) > 0.25F);
    }
    QVERIFY(strongestLeftSampleInRange(
        samples, 30'000, 60'000) < 0.0001F);
}

void FfmpegMediaDecoderTest::
streamsRealDecodeThroughBoundedSink() {
    using namespace std::chrono_literals;

    constexpr std::size_t capacity = 4'096;
    ControlledAudioSink sink(capacity);
    sink.reset(40, {48'000, 2});
    sink.start();

    DecodeCapture capture;
    FfmpegMediaDecodeResult result;
    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();
    std::jthread decoder([&](std::stop_token stopToken) {
        result = decodeMediaFrames(
            requestFor(synchronizedFixturePath(), 40),
            [&capture](
                    std::shared_ptr<const DecodedVideoFrame> frame,
                    const FfmpegVideoStreamDiagnostics &diagnostics) {
                capture.videoDiagnostics = diagnostics;
                capture.video.push_back(std::move(frame));
                return true;
            },
            [&sink](
                    PcmAudioBlock block,
                    const FfmpegAudioStreamDiagnostics &,
                    std::stop_token stopToken) {
                return sink.submit(
                    std::move(block), stopToken);
            },
            stopToken);
        sink.finish();
        completedPromise.set_value();
    });

    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (sink.bufferedFrames() == 0
            && completed.wait_for(0ms)
                == std::future_status::timeout
            && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    QVERIFY(sink.bufferedFrames() != 0);
    QVERIFY(sink.bufferedFrames() <= capacity);
    QVERIFY(completed.wait_for(0ms) == std::future_status::timeout);

    std::vector<float> samples;
    while ((completed.wait_for(0ms)
                == std::future_status::timeout
            || sink.bufferedFrames() != 0
            || sink.submittedFrames() != sink.presentedFrames())
            && std::chrono::steady_clock::now() < deadline) {
        ControlledAudioRender rendered = sink.render(257);
        if (rendered.frames == 0) {
            std::this_thread::yield();
            continue;
        }
        samples.insert(
            samples.end(),
            rendered.samples.begin(),
            rendered.samples.end());
        sink.advancePresentedFrames(rendered.frames);
    }
    if (completed.wait_for(0ms) == std::future_status::timeout) {
        decoder.request_stop();
        sink.reset(41, {48'000, 2});
    }
    QVERIFY(completed.wait_for(2s) == std::future_status::ready);
    decoder.join();

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(samples.size(), 288'000U);
    QVERIFY(sink.maximumObservedBufferedFrames() <= capacity);
    QVERIFY(sink.snapshot().drained);
}

void FfmpegMediaDecoderTest::
seeksAndTrimsAudioOnTheSharedTimeline() {
    DecodeCapture initial;
    const FfmpegMediaDecodeResult initialResult = decode(
        requestFor(synchronizedFixturePath(), 20), initial);
    QVERIFY2(initialResult.isSuccess(), qPrintable(initialResult.error));
    QVERIFY(initial.videoDiagnostics);
    QVERIFY(initial.videoDiagnostics->timelineOrigin);

    FfmpegMediaDecodeRequest request =
        requestFor(synchronizedFixturePath(), 21);
    request.video.start = {
        .targetPositionMicroseconds = 1'250'000,
        .timelineOrigin = initial.videoDiagnostics->timelineOrigin,
        .performDemuxSeek = true,
    };
    DecodeCapture capture;
    const FfmpegMediaDecodeResult result = decode(request, capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!capture.audio.empty());
    QCOMPARE(capture.audio.front().streamFrameIndex, 0U);
    QCOMPARE(
        capture.audio.front().mediaStartMicroseconds,
        1'250'000);
    QCOMPARE(result.outputAudioFrames, 84'000U);
    QVERIFY(!capture.video.empty());
    QVERIFY(std::any_of(
        capture.video.begin(),
        capture.video.end(),
        [](const auto &frame) {
            return frame->timing().ptsMicroseconds()
                == std::optional<std::int64_t>(6'250'000);
        }));
}

void FfmpegMediaDecoderTest::preservesVideoOnlyPlayback() {
    DecodeCapture capture;
    const FfmpegMediaDecodeResult result = decode(
        requestFor(videoOnlyFixturePath(), 30), capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!result.audioStreamPresent);
    QVERIFY(!result.audio);
    QCOMPARE(result.outputAudioFrames, 0U);
    QCOMPARE(capture.video.size(), 3U);
    QVERIFY(capture.audio.empty());
}

void FfmpegMediaDecoderTest::
distinguishesVideoSinkStopFromCancellation() {
    const auto stoppingVideoSink = [](
            std::shared_ptr<const DecodedVideoFrame>,
            const FfmpegVideoStreamDiagnostics &) {
        return false;
    };
    const auto acceptingAudioSink = [](
            PcmAudioBlock,
            const FfmpegAudioStreamDiagnostics &,
            std::stop_token) {
        return true;
    };
    const FfmpegMediaDecodeResult result = decodeMediaFrames(
        requestFor(synchronizedFixturePath(), 50),
        stoppingVideoSink,
        acceptingAudioSink);
    QVERIFY(result.isStopped());
    QVERIFY(result.video.stopped);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.isCancelled());
    QVERIFY(result.error.isEmpty());

    const FfmpegMediaDecodeResult videoOnly = decodeMediaFrames(
        requestFor(videoOnlyFixturePath(), 54),
        stoppingVideoSink,
        acceptingAudioSink);
    QVERIFY(videoOnly.isStopped());
    QVERIFY(videoOnly.video.stopped);
    QVERIFY(!videoOnly.isSuccess());
    QVERIFY(!videoOnly.isCancelled());
}

void FfmpegMediaDecoderTest::
cancelsWhileAudioSubmissionIsBackpressured() {
    using namespace std::chrono_literals;

    ControlledAudioSink sink(4'096);
    sink.reset(52, {48'000, 2});
    FfmpegMediaDecodeResult result;
    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();
    std::jthread decoder([&](std::stop_token stopToken) {
        result = decodeMediaFrames(
            requestFor(synchronizedFixturePath(), 52),
            [](std::shared_ptr<const DecodedVideoFrame>,
                    const FfmpegVideoStreamDiagnostics &) {
                return true;
            },
            [&sink](PcmAudioBlock block,
                    const FfmpegAudioStreamDiagnostics &,
                    std::stop_token stopToken) {
                return sink.submit(std::move(block), stopToken);
            },
            stopToken);
        completedPromise.set_value();
    });

    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (sink.bufferedFrames() == 0
            && completed.wait_for(0ms)
                == std::future_status::timeout
            && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    QVERIFY(sink.bufferedFrames() != 0);
    QVERIFY(completed.wait_for(0ms) == std::future_status::timeout);

    decoder.request_stop();
    QVERIFY(completed.wait_for(2s) == std::future_status::ready);
    decoder.join();
    QVERIFY(result.isCancelled());
    QVERIFY(!result.isStopped());
    QVERIFY(!result.isSuccess());
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.maximumQueuedPacketCount
        <= result.packetCountLimit);
    QVERIFY(result.maximumQueuedPacketBytes
        <= std::max(
            result.packetByteLimit,
            result.largestQueuedPacketBytes));
}

void FfmpegMediaDecoderTest::
honorsCancellationBeforeOpeningMedia() {
    std::stop_source stop;
    stop.request_stop();
    const FfmpegMediaDecodeResult result = decodeMediaFrames(
        requestFor(synchronizedFixturePath(), 53),
        [](std::shared_ptr<const DecodedVideoFrame>,
                const FfmpegVideoStreamDiagnostics &) {
            return true;
        },
        [](PcmAudioBlock,
                const FfmpegAudioStreamDiagnostics &,
                std::stop_token) {
            return true;
        },
        stop.get_token());
    QVERIFY(result.isCancelled());
    QVERIFY(!result.isStopped());
    QVERIFY(!result.isSuccess());
    QVERIFY(result.error.isEmpty());
}

void FfmpegMediaDecoderTest::
distinguishesAudioSinkStopFromCancellation() {
    const FfmpegMediaDecodeResult result = decodeMediaFrames(
        requestFor(synchronizedFixturePath(), 51),
        [](std::shared_ptr<const DecodedVideoFrame>,
                const FfmpegVideoStreamDiagnostics &) {
            return true;
        },
        [](PcmAudioBlock,
                const FfmpegAudioStreamDiagnostics &,
                std::stop_token) {
            return false;
        });
    QVERIFY(result.isStopped());
    QVERIFY(result.audioStopped);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.isCancelled());
    QVERIFY(result.error.isEmpty());
}

QTEST_APPLESS_MAIN(FfmpegMediaDecoderTest)
#include "tst_FfmpegMediaDecoder.moc"
