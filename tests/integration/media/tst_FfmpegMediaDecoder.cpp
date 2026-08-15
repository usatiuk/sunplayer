#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <numeric>
#include <thread>
#include <utility>
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

#include "audio/ControlledAudioSink.h"
#include "media/DecodedVideoFrame.h"
#include "media/FfmpegMediaDecoder.h"

namespace {
QString synchronizedFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-flac-sync.mkv");
}

QString synchronizedManifestPath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-flac-sync.toml");
}

QString videoOnlyFixturePath() { return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1.mkv"); }

QString shortAudioFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-short-flac.mkv");
}

QString audioGapFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-audio-gap-flac.mkv");
}

QString coarseTimeBaseDtsFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-dts-coarse-timebase.mkv");
}

QString coarseTimeBaseDtsManifestPath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-dts-coarse-timebase.toml");
}

QString pgsFixturePath() { return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-pgs.mkv"); }

QString compressedPgsFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-pgs-zlib.mkv");
}

QString pgsManifestPath() { return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-pgs.toml"); }

QString compressedPgsManifestPath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-pgs-zlib.toml");
}

QString textSubtitleFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-subtitles.mkv");
}

QString textSubtitleManifestPath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-subtitles.toml");
}

QString multitrackFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-multitrack-flac.mkv");
}

QString multitrackManifestPath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-multitrack-flac.toml");
}

QString programFixturePath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-mpegts-two-programs.ts");
}

QString programManifestPath() {
    return QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/sdr-bt709-mpegts-two-programs.toml");
}

FfmpegMediaDecodeRequest requestFor(QString const& path, std::uint64_t generation = 1) {
    return {
        .video =
            {
                .path = path,
                .firstFrameIdentity =
                    {
                        .playbackGeneration = generation,
                        .decoderRevision = 1,
                        .frameId = 1,
                    },
                .hardwareDecode =
                    {
                        .device = {},
                        .unavailableReason = QStringLiteral("Deterministic software decode"),
                    },
            },
    };
}

struct DecodeCapture {
    std::vector<std::shared_ptr<DecodedVideoFrame const>> video;
    std::vector<PcmAudioBlock> audio;
    std::optional<FfmpegVideoStreamDiagnostics> videoDiagnostics;
    std::optional<FfmpegAudioStreamDiagnostics> audioDiagnostics;
    std::optional<FfmpegMediaStreamSelection> streamSelection;
};

FfmpegMediaDecodeResult decode(FfmpegMediaDecodeRequest const& request, DecodeCapture& capture) {
    return decodeMediaFrames(
        request,
        [&capture](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const& diagnostics) {
            capture.videoDiagnostics = diagnostics;
            capture.video.push_back(std::move(frame));
            return true;
        },
        FfmpegAudioOutputSink{
            .submit =
                [&capture](PcmAudioBlock block, FfmpegAudioStreamDiagnostics const& diagnostics, std::stop_token) {
                    capture.audioDiagnostics = diagnostics;
                    capture.audio.push_back(std::move(block));
                    return true;
                },
            .endOfStream = [](std::uint64_t) {},
        },
        [&capture](FfmpegMediaStreamSelection const& selection) { capture.streamSelection = selection; });
}

std::vector<float> flattenAudio(std::vector<PcmAudioBlock> const& blocks) {
    std::vector<float> samples;
    for (PcmAudioBlock const& block : blocks) {
        samples.insert(samples.end(), block.samples.begin(), block.samples.end());
    }
    return samples;
}

float strongestLeftSample(std::vector<float> const& samples, std::size_t centerFrame, std::size_t radius) {
    float maximum = 0.0F;
    std::size_t const first = centerFrame - radius;
    std::size_t const last = centerFrame + radius;
    for (std::size_t frame = first; frame <= last; ++frame) {
        maximum = std::max(maximum, std::abs(samples[frame * 2]));
    }
    return maximum;
}

QByteArray expectedFixtureHash(QString const& manifestPath) {
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) {
        return {};
    }
    QRegularExpressionMatch const match = QRegularExpression(QStringLiteral("^sha256\\s*=\\s*\"([0-9a-f]{64})\"\\s*$"),
                                                             QRegularExpression::MultilineOption)
                                              .match(QString::fromUtf8(manifest.readAll()));
    return match.hasMatch() ? match.captured(1).toLatin1() : QByteArray();
}

QByteArray fixtureHash(QString const& fixturePath) {
    QFile fixture(fixturePath);
    if (!fixture.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256).toHex();
}

float strongestLeftSampleInRange(std::vector<float> const& samples, std::size_t firstFrame, std::size_t lastFrame) {
    float maximum = 0.0F;
    for (std::size_t frame = firstFrame; frame < lastFrame; ++frame) {
        maximum = std::max(maximum, std::abs(samples[frame * 2]));
    }
    return maximum;
}
} // namespace

class FfmpegMediaDecoderTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void rejectsUnsupportedOutputFormat();
    void rejectsIncompleteAudioLifecycleSink();
    void selectsEmbeddedVideoAndAudioTracks_data();
    void selectsEmbeddedVideoAndAudioTracks();
    void rejectsInvalidEmbeddedTrackSelections_data();
    void rejectsInvalidEmbeddedTrackSelections();
    void keepsEmbeddedTrackSelectionWithinOneProgram();
    void decodesSynchronizedFixtureThroughOneDemuxOperation();
    void streamsRealDecodeThroughBoundedSink();
    void seeksAndTrimsAudioOnTheSharedTimeline();
    void fillsMidStreamAudioTimestampGapWithSourceSilence();
    void preservesSamplesAcrossCoarseAudioTimestamps();
    void seekingPastAudioEndIsACleanVideoInterval();
    void preservesVideoOnlyPlayback();
    void discoversAndDecodesSelectedPgsInTheSingleMediaOperation_data();
    void discoversAndDecodesSelectedPgsInTheSingleMediaOperation();
    void continuesAvAfterSubtitleOutputFailure();
    void discoversTextTracksFontsAndFfmpegAssConversion();
    void distinguishesVideoSinkStopFromCancellation();
    void distinguishesAudioSinkStopFromCancellation();
    void cancelsWhileAudioSubmissionIsBackpressured();
    void honorsCancellationBeforeOpeningMedia();
};

void FfmpegMediaDecoderTest::rejectsUnsupportedOutputFormat() {
    FfmpegMediaDecodeRequest request = requestFor(synchronizedFixturePath());
    request.audioOutput = {48'000, 1};
    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(request, capture);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error, QStringLiteral("Media decode request is invalid"));
    QVERIFY(capture.audio.empty());
    QVERIFY(capture.video.empty());
}

void FfmpegMediaDecoderTest::rejectsIncompleteAudioLifecycleSink() {
    FfmpegMediaDecodeResult const result = decodeMediaFrames(
        requestFor(synchronizedFixturePath()),
        [](std::shared_ptr<DecodedVideoFrame const>, FfmpegVideoStreamDiagnostics const&) { return true; },
        FfmpegAudioOutputSink{
            .submit = [](PcmAudioBlock, FfmpegAudioStreamDiagnostics const&, std::stop_token) { return true; },
        },
        FfmpegMediaStreamSink{}, std::stop_token{});
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error, QStringLiteral("Media decode request is invalid"));
}

void FfmpegMediaDecoderTest::selectsEmbeddedVideoAndAudioTracks_data() {
    QTest::addColumn<int>("requestedVideo");
    QTest::addColumn<int>("requestedAudio");
    QTest::addColumn<int>("expectedVideo");
    QTest::addColumn<int>("expectedAudio");
    QTest::addColumn<int>("expectedLuma");
    QTest::addColumn<double>("expectedAudioSample");

    QTest::newRow("container defaults") << -1 << -1 << 2 << 3 << 235 << -0.25;
    QTest::newRow("dark video and negative audio") << 0 << 3 << 0 << 3 << 16 << -0.25;
    QTest::newRow("light video and positive audio") << 2 << 1 << 2 << 1 << 235 << 0.25;
}

void FfmpegMediaDecoderTest::selectsEmbeddedVideoAndAudioTracks() {
    QFETCH(int, requestedVideo);
    QFETCH(int, requestedAudio);
    QFETCH(int, expectedVideo);
    QFETCH(int, expectedAudio);
    QFETCH(int, expectedLuma);
    QFETCH(double, expectedAudioSample);

    QByteArray const declaredHash = expectedFixtureHash(multitrackManifestPath());
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(multitrackFixturePath()), declaredHash);

    FfmpegMediaDecodeRequest request = requestFor(multitrackFixturePath());
    request.selectedVideoStreamIndex = requestedVideo;
    request.selectedAudioStreamIndex = requestedAudio;
    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(request, capture);

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(capture.streamSelection.has_value());
    FfmpegMediaStreamSelection const& selection = *capture.streamSelection;
    QCOMPARE(selection.selectedVideoStreamIndex, expectedVideo);
    QCOMPARE(selection.selectedAudioStreamIndex, expectedAudio);
    QCOMPARE(selection.videoTracks.size(), 2U);
    QCOMPARE(selection.audioTracks.size(), 2U);
    QCOMPARE(selection.videoTracks[0].streamIndex, 0);
    QCOMPARE(selection.videoTracks[1].streamIndex, 2);
    QCOMPARE(selection.audioTracks[0].streamIndex, 1);
    QCOMPARE(selection.audioTracks[1].streamIndex, 3);
    QCOMPARE(selection.videoTracks[0].title, QStringLiteral("Dark"));
    QCOMPARE(selection.videoTracks[0].endMicroseconds, std::optional<std::int64_t>(1'250'000));
    QCOMPARE(selection.videoTracks[1].language, QStringLiteral("ces"));
    QVERIFY(selection.videoTracks[1].isDefault);
    QCOMPARE(selection.audioTracks[0].title, QStringLiteral("Positive"));
    QCOMPARE(selection.audioTracks[0].channelLayout, QStringLiteral("mono"));
    QCOMPARE(selection.audioTracks[0].sampleRate, 48'000);
    QVERIFY(selection.audioTracks[1].isDefault);
    QVERIFY(std::ranges::all_of(selection.videoTracks, &EmbeddedMediaStreamDescriptor::supported));
    QVERIFY(std::ranges::all_of(selection.audioTracks, &EmbeddedMediaStreamDescriptor::supported));

    QVERIFY(capture.videoDiagnostics.has_value());
    QCOMPARE(capture.videoDiagnostics->videoStreamIndex, expectedVideo);
    QVERIFY(capture.audioDiagnostics.has_value());
    QCOMPARE(capture.audioDiagnostics->audioStreamIndex, expectedAudio);
    QVERIFY(!capture.video.empty());
    AVFrame const& frame = capture.video.front()->ffmpegFrame();
    QVERIFY(frame.data[0]);
    int const centerLuma = frame.data[0][frame.height / 2 * frame.linesize[0] + frame.width / 2];
    QVERIFY(std::abs(centerLuma - expectedLuma) <= 1);

    std::vector<float> const samples = flattenAudio(capture.audio);
    QVERIFY(!samples.empty());
    double const mean = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    QVERIFY(std::abs(mean) > 0.10);
    QCOMPARE(mean > 0.0, expectedAudioSample > 0.0);
}

void FfmpegMediaDecoderTest::rejectsInvalidEmbeddedTrackSelections_data() {
    QTest::addColumn<int>("requestedVideo");
    QTest::addColumn<int>("requestedAudio");
    QTest::addColumn<QString>("expectedError");

    QTest::newRow("audio index requested as video")
        << 1 << -1 << QStringLiteral("The selected video track is unavailable");
    QTest::newRow("video index requested as audio")
        << -1 << 2 << QStringLiteral("The selected audio track is unavailable");
    QTest::newRow("missing video index") << 99 << -1 << QStringLiteral("The selected video track is unavailable");
    QTest::newRow("missing audio index") << -1 << 99 << QStringLiteral("The selected audio track is unavailable");
}

void FfmpegMediaDecoderTest::rejectsInvalidEmbeddedTrackSelections() {
    QFETCH(int, requestedVideo);
    QFETCH(int, requestedAudio);
    QFETCH(QString, expectedError);

    FfmpegMediaDecodeRequest request = requestFor(multitrackFixturePath());
    request.selectedVideoStreamIndex = requestedVideo;
    request.selectedAudioStreamIndex = requestedAudio;
    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(request, capture);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error, expectedError);
    QVERIFY(capture.video.empty());
    QVERIFY(capture.audio.empty());
}

void FfmpegMediaDecoderTest::keepsEmbeddedTrackSelectionWithinOneProgram() {
    QCOMPARE(fixtureHash(programFixturePath()), expectedFixtureHash(programManifestPath()));

    FfmpegMediaDecodeRequest request = requestFor(programFixturePath());
    DecodeCapture capture;
    FfmpegMediaDecodeResult result = decode(request, capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(capture.streamSelection);
    QCOMPARE(capture.streamSelection->selectedVideoStreamIndex, 0);
    QCOMPARE(capture.streamSelection->selectedAudioStreamIndex, 1);
    QCOMPARE(capture.streamSelection->videoTracks.size(), 1U);
    QCOMPARE(capture.streamSelection->videoTracks.front().streamIndex, 0);
    QCOMPARE(capture.streamSelection->audioTracks.size(), 1U);
    QCOMPARE(capture.streamSelection->audioTracks.front().streamIndex, 1);

    request.selectedVideoStreamIndex = 2;
    capture = {};
    result = decode(request, capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(capture.streamSelection);
    QCOMPARE(capture.streamSelection->selectedVideoStreamIndex, 2);
    QCOMPARE(capture.streamSelection->selectedAudioStreamIndex, 3);
    QCOMPARE(capture.streamSelection->videoTracks.size(), 1U);
    QCOMPARE(capture.streamSelection->videoTracks.front().streamIndex, 2);
    QCOMPARE(capture.streamSelection->audioTracks.size(), 1U);
    QCOMPARE(capture.streamSelection->audioTracks.front().streamIndex, 3);

    request.selectedAudioStreamIndex = 1;
    capture = {};
    result = decode(request, capture);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error, QStringLiteral("The selected audio track is unavailable"));
    QVERIFY(capture.video.empty());
    QVERIFY(capture.audio.empty());
}

void FfmpegMediaDecoderTest::decodesSynchronizedFixtureThroughOneDemuxOperation() {
    QByteArray const declaredHash = expectedFixtureHash(synchronizedManifestPath());
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(synchronizedFixturePath()), declaredHash);

    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(requestFor(synchronizedFixturePath()), capture);
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
    QCOMPARE(capture.videoDiagnostics->durationMicroseconds, std::optional<std::int64_t>(8'000'000));
    QVERIFY(!capture.videoDiagnostics->durationFinal);
    QCOMPARE(result.video.diagnostics.durationMicroseconds, std::optional<std::int64_t>(3'000'000));
    QVERIFY(result.video.diagnostics.durationFinal);
    QCOMPARE(result.video.observedEndMicroseconds, std::optional<std::int64_t>(3'000'000));
    QCOMPARE(result.observedAudioEndMicroseconds, std::optional<std::int64_t>(3'000'000));
    QVERIFY(result.packetCountLimit > 0);
    QVERIFY(result.packetByteLimit > 0);
    QVERIFY(result.maximumQueuedPacketCount <= result.packetCountLimit);
    QVERIFY(result.maximumQueuedPacketBytes <= std::max(result.packetByteLimit, result.largestQueuedPacketBytes));
    QCOMPARE(capture.videoDiagnostics->timelineOrigin->microseconds(), std::optional<std::int64_t>(5'000'000));
    QCOMPARE(capture.audioDiagnostics->outputFormat, AudioStreamFormat(48'000, 2));
    QCOMPARE(capture.audioDiagnostics->sourceSampleRate, 32'000);
    QCOMPARE(capture.audioDiagnostics->sourceChannelCount, 1);

    for (std::size_t index = 0; index < capture.video.size(); ++index) {
        DecodedVideoFrame const& frame = *capture.video[index];
        QCOMPARE(frame.identity().playbackGeneration, 1U);
        QCOMPARE(frame.timing().ptsMicroseconds(),
                 std::optional<std::int64_t>(5'000'000 + static_cast<std::int64_t>(index) * 250'000));
        AVFrame const& avFrame = frame.ffmpegFrame();
        QVERIFY(avFrame.data[0] != nullptr);
        int const luma = avFrame.data[0][32 * avFrame.linesize[0] + 48];
        bool const white = index == 2 || index == 6 || index == 10;
        if (white) {
            QVERIFY(luma >= 230);
        } else {
            QVERIFY(luma <= 20);
        }
    }

    std::uint64_t expectedFrame = 0;
    for (PcmAudioBlock const& block : capture.audio) {
        QVERIFY(block.isValid());
        QCOMPARE(block.playbackGeneration, 1U);
        QCOMPARE(block.streamFrameIndex, expectedFrame);
        std::int64_t const expectedTime = static_cast<std::int64_t>(expectedFrame * 1'000'000ULL / 48'000ULL);
        QVERIFY(std::abs(block.mediaStartMicroseconds - expectedTime) <= 1);
        for (std::size_t frame = 0; frame < block.frameCount(); ++frame) {
            float const left = block.samples[frame * 2];
            float const right = block.samples[frame * 2 + 1];
            QVERIFY(std::isfinite(left));
            QVERIFY(std::isfinite(right));
            QCOMPARE(left, right);
        }
        expectedFrame += block.frameCount();
    }
    QCOMPARE(expectedFrame, 144'000U);

    std::vector<float> const samples = flattenAudio(capture.audio);
    QCOMPARE(samples.size(), 288'000U);
    for (std::size_t const impulse : {24'000U, 72'000U, 120'000U}) {
        QVERIFY(strongestLeftSample(samples, impulse, 32) > 0.25F);
    }
    QVERIFY(strongestLeftSampleInRange(samples, 30'000, 60'000) < 0.0001F);
}

void FfmpegMediaDecoderTest::streamsRealDecodeThroughBoundedSink() {
    using namespace std::chrono_literals;

    constexpr std::size_t capacity = 4'096;
    ControlledAudioSink sink(capacity);
    sink.reset(40, {48'000, 2});
    sink.start();

    DecodeCapture capture;
    FfmpegMediaDecodeResult result;
    std::atomic_int streamSelections = 0;
    std::atomic_bool selectedAudio = false;
    std::optional<FfmpegVideoStreamDiagnostics> selectedVideoDiagnostics;
    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();
    std::jthread decoder([&](std::stop_token stopToken) {
        result = decodeMediaFrames(
            requestFor(synchronizedFixturePath(), 40),
            [&capture](std::shared_ptr<DecodedVideoFrame const> frame,
                       FfmpegVideoStreamDiagnostics const& diagnostics) {
                capture.videoDiagnostics = diagnostics;
                capture.video.push_back(std::move(frame));
                return true;
            },
            FfmpegAudioOutputSink{
                .submit = [&sink](PcmAudioBlock block, FfmpegAudioStreamDiagnostics const&,
                                  std::stop_token stopToken) { return sink.submit(std::move(block), stopToken); },
                .endOfStream = [&sink](std::uint64_t generation) { sink.finish(generation); },
            },
            [&streamSelections, &selectedAudio,
             &selectedVideoDiagnostics](FfmpegMediaStreamSelection const& selection) {
                ++streamSelections;
                selectedAudio = selection.audioStreamPresent;
                selectedVideoDiagnostics = selection.videoDiagnostics;
            },
            stopToken);
        completedPromise.set_value();
    });

    auto const deadline = std::chrono::steady_clock::now() + 20s;
    while (sink.bufferedFrames() == 0 && completed.wait_for(0ms) == std::future_status::timeout &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    QVERIFY(sink.bufferedFrames() != 0);
    QVERIFY(sink.bufferedFrames() <= capacity);
    QVERIFY(completed.wait_for(0ms) == std::future_status::timeout);

    std::vector<float> samples;
    while ((completed.wait_for(0ms) == std::future_status::timeout || sink.bufferedFrames() != 0 ||
            sink.submittedFrames() != sink.presentedFrames()) &&
           std::chrono::steady_clock::now() < deadline) {
        ControlledAudioRender rendered = sink.render(257);
        if (rendered.frames == 0) {
            std::this_thread::yield();
            continue;
        }
        samples.insert(samples.end(), rendered.samples.begin(), rendered.samples.end());
        sink.advancePresentedFrames(rendered.frames);
    }
    if (completed.wait_for(0ms) == std::future_status::timeout) {
        decoder.request_stop();
        sink.reset(41, {48'000, 2});
    }
    QVERIFY(completed.wait_for(2s) == std::future_status::ready);
    decoder.join();

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(streamSelections.load(), 1);
    QVERIFY(selectedAudio.load());
    QVERIFY(selectedVideoDiagnostics);
    QVERIFY(selectedVideoDiagnostics->isValid());
    QCOMPARE(samples.size(), 288'000U);
    QVERIFY(sink.maximumObservedBufferedFrames() <= capacity);
    QVERIFY(sink.snapshot().drained);
}

void FfmpegMediaDecoderTest::seeksAndTrimsAudioOnTheSharedTimeline() {
    DecodeCapture initial;
    FfmpegMediaDecodeResult const initialResult = decode(requestFor(synchronizedFixturePath(), 20), initial);
    QVERIFY2(initialResult.isSuccess(), qPrintable(initialResult.error));
    QVERIFY(initial.videoDiagnostics);
    QVERIFY(initial.videoDiagnostics->timelineOrigin);

    FfmpegMediaDecodeRequest request = requestFor(synchronizedFixturePath(), 21);
    request.video.start = {
        .targetPositionMicroseconds = 1'250'000,
        .timelineOrigin = initial.videoDiagnostics->timelineOrigin,
        .performDemuxSeek = true,
    };
    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(request, capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!capture.audio.empty());
    QCOMPARE(capture.audio.front().streamFrameIndex, 0U);
    QCOMPARE(capture.audio.front().mediaStartMicroseconds, 1'250'000);
    QCOMPARE(result.outputAudioFrames, 84'000U);
    QVERIFY(!capture.video.empty());
    QVERIFY(std::any_of(capture.video.begin(), capture.video.end(), [](auto const& frame) {
        return frame->timing().ptsMicroseconds() == std::optional<std::int64_t>(6'250'000);
    }));
}

void FfmpegMediaDecoderTest::fillsMidStreamAudioTimestampGapWithSourceSilence() {
    QByteArray const declaredHash("00947436b22bb52d317c0896edeb173c"
                                  "6e06dd68f681447453f3b354ab06aa44");
    QCOMPARE(fixtureHash(audioGapFixturePath()), declaredHash);

    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(requestFor(audioGapFixturePath(), 24), capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(result.audioEndOfStream);
    QCOMPARE(result.outputAudioFrames, 96'000U);
    QVERIFY(capture.audio.size() > 1U);

    std::uint64_t expectedFrame = 0;
    for (PcmAudioBlock const& block : capture.audio) {
        QCOMPARE(block.streamFrameIndex, expectedFrame);
        expectedFrame += block.frameCount();
    }
    QCOMPARE(expectedFrame, 96'000U);
    QCOMPARE(result.observedAudioEndMicroseconds, std::optional<std::int64_t>(2'000'000));
}

void FfmpegMediaDecoderTest::preservesSamplesAcrossCoarseAudioTimestamps() {
    QCOMPARE(fixtureHash(coarseTimeBaseDtsFixturePath()), expectedFixtureHash(coarseTimeBaseDtsManifestPath()));

    auto const verify = [](DecodeCapture const& capture, FfmpegMediaDecodeResult const& result) {
        std::uint64_t expectedFrame = 0;
        for (PcmAudioBlock const& block : capture.audio) {
            QCOMPARE(block.streamFrameIndex, expectedFrame);
            std::int64_t const expectedTime = static_cast<std::int64_t>(expectedFrame * 1'000'000ULL / 48'000ULL);
            QVERIFY(std::abs(block.mediaStartMicroseconds - expectedTime) <= 1);
            expectedFrame += block.frameCount();
        }
        QCOMPARE(expectedFrame, result.outputAudioFrames);
    };

    DecodeCapture initial;
    FfmpegMediaDecodeResult const initialResult = decode(requestFor(coarseTimeBaseDtsFixturePath(), 25), initial);
    QVERIFY2(initialResult.isSuccess(), qPrintable(initialResult.error));
    QVERIFY(initial.audioDiagnostics);
    QCOMPARE(initial.audioDiagnostics->sourceSampleRate, 48'000);
    QCOMPARE(initial.audioDiagnostics->sourceChannelCount, 6);
    QCOMPARE(initialResult.decodedAudioFrames, 282U);
    QCOMPARE(initialResult.outputAudioFrames, 144'384U);
    QCOMPARE(initialResult.observedAudioEndMicroseconds, std::optional<std::int64_t>(3'008'000));
    verify(initial, initialResult);

    DecodeCapture reopened;
    FfmpegMediaDecodeResult const reopenedResult = decode(requestFor(coarseTimeBaseDtsFixturePath(), 26), reopened);
    QVERIFY2(reopenedResult.isSuccess(), qPrintable(reopenedResult.error));
    QCOMPARE(reopenedResult.decodedAudioFrames, initialResult.decodedAudioFrames);
    QCOMPARE(reopenedResult.outputAudioFrames, initialResult.outputAudioFrames);
    QCOMPARE(reopenedResult.observedAudioEndMicroseconds, initialResult.observedAudioEndMicroseconds);
    verify(reopened, reopenedResult);
}

void FfmpegMediaDecoderTest::seekingPastAudioEndIsACleanVideoInterval() {
    DecodeCapture initial;
    FfmpegMediaDecodeResult const initialResult = decode(requestFor(shortAudioFixturePath(), 22), initial);
    QVERIFY2(initialResult.isSuccess(), qPrintable(initialResult.error));
    QVERIFY(initial.videoDiagnostics);
    QVERIFY(initial.videoDiagnostics->timelineOrigin);

    FfmpegMediaDecodeRequest request = requestFor(shortAudioFixturePath(), 23);
    request.video.start = {
        .targetPositionMicroseconds = 2'000'000,
        .timelineOrigin = initial.videoDiagnostics->timelineOrigin,
        .performDemuxSeek = true,
    };
    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(request, capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(result.audioStreamPresent);
    QVERIFY(result.audioEndOfStream);
    QCOMPARE(result.outputAudioFrames, 0U);
    QVERIFY(capture.audio.empty());
    QVERIFY(!capture.video.empty());
    QCOMPARE(result.video.diagnostics.durationMicroseconds, std::optional<std::int64_t>(3'000'000));
    QVERIFY(result.video.diagnostics.durationFinal);
}

void FfmpegMediaDecoderTest::preservesVideoOnlyPlayback() {
    DecodeCapture capture;
    FfmpegMediaDecodeResult const result = decode(requestFor(videoOnlyFixturePath(), 30), capture);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!result.audioStreamPresent);
    QVERIFY(!result.audio);
    QCOMPARE(result.outputAudioFrames, 0U);
    QCOMPARE(capture.video.size(), 3U);
    QVERIFY(capture.audio.empty());
}

void FfmpegMediaDecoderTest::discoversAndDecodesSelectedPgsInTheSingleMediaOperation_data() {
    QTest::addColumn<QString>("fixturePath");
    QTest::addColumn<QString>("manifestPath");

    QTest::newRow("uncompressed") << pgsFixturePath() << pgsManifestPath();
    QTest::newRow("matroska-zlib") << compressedPgsFixturePath() << compressedPgsManifestPath();
}

void FfmpegMediaDecoderTest::discoversAndDecodesSelectedPgsInTheSingleMediaOperation() {
    QFETCH(QString, fixturePath);
    QFETCH(QString, manifestPath);
    QCOMPARE(fixtureHash(fixturePath), expectedFixtureHash(manifestPath));

    FfmpegMediaDecodeRequest request = requestFor(fixturePath, 31);
    request.selectedSubtitleStreamIndex = 2;
    DecodeCapture capture;
    std::vector<EmbeddedMediaStreamDescriptor> tracks;
    std::optional<SubtitleStreamConfiguration> configuration;
    std::vector<SubtitleEvent> events;
    QString subtitleFailure;
    FfmpegMediaDecodeResult const result = decodeMediaFrames(
        request,
        [&capture](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const& diagnostics) {
            capture.videoDiagnostics = diagnostics;
            capture.video.push_back(std::move(frame));
            return true;
        },
        FfmpegAudioOutputSink{
            .submit =
                [&capture](PcmAudioBlock block, FfmpegAudioStreamDiagnostics const& diagnostics, std::stop_token) {
                    capture.audioDiagnostics = diagnostics;
                    capture.audio.push_back(std::move(block));
                    return true;
                },
            .endOfStream = [](std::uint64_t) {},
        },
        [&tracks, &configuration](FfmpegMediaStreamSelection const& selection) {
            tracks = selection.subtitleTracks;
            configuration = selection.subtitleConfiguration;
        },
        FfmpegSubtitleOutputSink{
            .submit =
                [&events](SubtitleEvent event, std::stop_token) {
                    events.push_back(std::move(event));
                    return true;
                },
            .failed = [&subtitleFailure](QString error) { subtitleFailure = std::move(error); },
        });

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY2(subtitleFailure.isEmpty(), qPrintable(subtitleFailure));
    QVERIFY2(result.subtitleError.isEmpty(), qPrintable(result.subtitleError));
    QVERIFY(result.subtitleEndOfStream);
    QCOMPARE(tracks.size(), 1U);
    QCOMPARE(tracks.front().streamIndex, 2);
    QCOMPARE(tracks.front().codec, QStringLiteral("hdmv_pgs_subtitle"));
    QCOMPARE(tracks.front().subtitleKind, SubtitleStreamKind::Bitmap);
    QVERIFY(tracks.front().supported);
    QVERIFY(configuration);
    QCOMPARE(configuration->playbackGeneration, 31U);
    QCOMPARE(configuration->streamIndex, 2);
    QCOMPARE(configuration->canvasSize, QSize(320, 180));

    QCOMPARE(events.size(), 3U);
    QCOMPARE(events[0].startMicroseconds, 0);
    QCOMPARE(events[0].type, SubtitlePayloadType::Bitmap);
    QVERIFY(events[0].bitmap);
    QCOMPARE(events[0].bitmap->regions.size(), 2U);
    QCOMPARE(events[0].bitmap->regions[0].x, 30);
    QCOMPARE(events[0].bitmap->regions[0].y, 120);
    QCOMPARE(events[0].bitmap->regions[0].size, QSize(60, 20));
    QVERIFY(!events[0].endMicroseconds);
    QByteArray const& white = events[0].bitmap->regions[0].rgba;
    QVERIFY(static_cast<unsigned char>(white[0]) > 240);
    QVERIFY(static_cast<unsigned char>(white[1]) > 240);
    QVERIFY(static_cast<unsigned char>(white[2]) > 240);
    QCOMPARE(static_cast<unsigned char>(white[3]), 255);
    QCOMPARE(events[0].bitmap->regions[1].x, 230);
    QCOMPARE(events[0].bitmap->regions[1].y, 30);
    QCOMPARE(events[0].bitmap->regions[1].size, QSize(40, 24));
    QByteArray const& yellow = events[0].bitmap->regions[1].rgba;
    QVERIFY(static_cast<unsigned char>(yellow[0]) > 220);
    QVERIFY(static_cast<unsigned char>(yellow[1]) > 180);
    QVERIFY(static_cast<unsigned char>(yellow[2]) < 80);
    QCOMPARE(static_cast<unsigned char>(yellow[3]), 255);

    QCOMPARE(events[1].startMicroseconds, 2'000'000);
    QCOMPARE(events[1].type, SubtitlePayloadType::Bitmap);
    QVERIFY(events[1].bitmap);
    QCOMPARE(events[1].bitmap->regions.size(), 1U);
    QCOMPARE(events[1].bitmap->regions[0].x, 110);
    QCOMPARE(events[1].bitmap->regions[0].y, 125);
    QCOMPARE(events[2].startMicroseconds, 4'000'000);
    QCOMPARE(events[2].type, SubtitlePayloadType::Clear);
}

void FfmpegMediaDecoderTest::discoversTextTracksFontsAndFfmpegAssConversion() {
    QCOMPARE(fixtureHash(textSubtitleFixturePath()), expectedFixtureHash(textSubtitleManifestPath()));
    struct SubtitleCapture {
        std::vector<EmbeddedMediaStreamDescriptor> tracks;
        std::optional<SubtitleStreamConfiguration> configuration;
        std::vector<SubtitleEvent> events;
        QString error;
    };
    auto const decodeTrack = [](int streamIndex, std::uint64_t generation) {
        SubtitleCapture capture;
        FfmpegMediaDecodeRequest request = requestFor(textSubtitleFixturePath(), generation);
        request.selectedSubtitleStreamIndex = streamIndex;
        FfmpegMediaDecodeResult const result = decodeMediaFrames(
            request, [](std::shared_ptr<DecodedVideoFrame const>, FfmpegVideoStreamDiagnostics const&) { return true; },
            FfmpegAudioOutputSink{
                .submit = [](PcmAudioBlock, FfmpegAudioStreamDiagnostics const&, std::stop_token) { return true; },
                .endOfStream = [](std::uint64_t) {},
            },
            [&capture](FfmpegMediaStreamSelection const& selection) {
                capture.tracks = selection.subtitleTracks;
                capture.configuration = selection.subtitleConfiguration;
            },
            FfmpegSubtitleOutputSink{
                .submit =
                    [&capture](SubtitleEvent event, std::stop_token) {
                        capture.events.push_back(std::move(event));
                        return true;
                    },
                .failed = [&capture](QString error) { capture.error = std::move(error); },
            });
        return std::pair(std::move(capture), result);
    };

    auto [ass, assResult] = decodeTrack(2, 32);
    QVERIFY2(assResult.isSuccess(), qPrintable(assResult.error));
    QVERIFY2(ass.error.isEmpty(), qPrintable(ass.error));
    QCOMPARE(ass.tracks.size(), 2U);
    QCOMPARE(ass.tracks[0].streamIndex, 2);
    QCOMPARE(ass.tracks[0].language, QStringLiteral("eng"));
    QCOMPARE(ass.tracks[0].title, QStringLiteral("Styled Ahem"));
    QCOMPARE(ass.tracks[0].label, QStringLiteral("English - Styled Ahem"));
    QCOMPARE(ass.tracks[0].subtitleKind, SubtitleStreamKind::Text);
    QCOMPARE(ass.tracks[1].streamIndex, 3);
    QCOMPARE(ass.tracks[1].language, QStringLiteral("ces"));
    QCOMPARE(ass.tracks[1].label, QStringLiteral("Czech - Plain Czech (SDH)"));
    QCOMPARE(ass.tracks[1].subtitleKind, SubtitleStreamKind::Text);
    QVERIFY(ass.tracks[1].isHearingImpaired);
    QVERIFY(ass.configuration);
    QCOMPARE(ass.configuration->codec, QStringLiteral("ass"));
    QVERIFY(!ass.configuration->codecPrivate.isEmpty());
    QCOMPARE(ass.configuration->fonts.size(), 1U);
    QCOMPARE(ass.configuration->fonts[0].name, QStringLiteral("Ahem.ttf"));
    QCOMPARE(QCryptographicHash::hash(ass.configuration->fonts[0].bytes, QCryptographicHash::Sha256).toHex(),
             QByteArrayLiteral("b719ecb31c5b21fc573c03f6421c74ac"
                               "63c271a5a3ff841e34f9705fb94b8448"));
    QCOMPARE(ass.events.size(), 2U);
    QCOMPARE(ass.events[0].startMicroseconds, 500'000);
    QCOMPARE(ass.events[0].endMicroseconds, 2'500'000);
    QCOMPARE(ass.events[0].type, SubtitlePayloadType::AssText);
    QVERIFY(ass.events[0].ass.contains("ABCD"));
    QVERIFY(ass.events[0].ass.contains("\\pos(40,40)"));
    QCOMPARE(ass.events[1].startMicroseconds, 3'000'000);
    QCOMPARE(ass.events[1].endMicroseconds, 6'000'000);
    QVERIFY(ass.events[1].ass.contains("ANIMATE"));
    QVERIFY(ass.events[1].ass.contains("\\t("));

    auto [plain, plainResult] = decodeTrack(3, 33);
    QVERIFY2(plainResult.isSuccess(), qPrintable(plainResult.error));
    QVERIFY2(plain.error.isEmpty(), qPrintable(plain.error));
    QVERIFY(plain.configuration);
    QCOMPARE(plain.configuration->codec, QStringLiteral("subrip"));
    QVERIFY(!plain.configuration->codecPrivate.isEmpty());
    QCOMPARE(plain.events.size(), 2U);
    QCOMPARE(plain.events[0].startMicroseconds, 1'000'000);
    QCOMPARE(plain.events[0].endMicroseconds, 2'000'000);
    QCOMPARE(plain.events[0].type, SubtitlePayloadType::AssText);
    QVERIFY(plain.events[0].ass.contains(QByteArray::fromStdString("Příliš")));
    QCOMPARE(plain.events[1].startMicroseconds, 4'000'000);
    QCOMPARE(plain.events[1].endMicroseconds, 5'500'000);
}

void FfmpegMediaDecoderTest::continuesAvAfterSubtitleOutputFailure() {
    FfmpegMediaDecodeRequest request = requestFor(pgsFixturePath(), 34);
    request.selectedSubtitleStreamIndex = 2;
    DecodeCapture capture;
    int rejectedEvents = 0;
    QString subtitleFailure;
    FfmpegMediaDecodeResult const result = decodeMediaFrames(
        request,
        [&capture](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const& diagnostics) {
            capture.videoDiagnostics = diagnostics;
            capture.video.push_back(std::move(frame));
            return true;
        },
        FfmpegAudioOutputSink{
            .submit =
                [&capture](PcmAudioBlock block, FfmpegAudioStreamDiagnostics const& diagnostics, std::stop_token) {
                    capture.audioDiagnostics = diagnostics;
                    capture.audio.push_back(std::move(block));
                    return true;
                },
            .endOfStream = [](std::uint64_t) {},
        },
        [](FfmpegMediaStreamSelection const&) {},
        FfmpegSubtitleOutputSink{
            .submit =
                [&rejectedEvents](SubtitleEvent, std::stop_token) {
                    ++rejectedEvents;
                    return false;
                },
            .failed = [&subtitleFailure](QString error) { subtitleFailure = std::move(error); },
        });

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(rejectedEvents, 1);
    QVERIFY(!subtitleFailure.isEmpty());
    QVERIFY(!result.subtitleError.isEmpty());
    QVERIFY(result.subtitleEndOfStream);
    QVERIFY(result.video.endOfStream);
    QVERIFY(result.audioEndOfStream);
    QVERIFY(!capture.video.empty());
    QVERIFY(!capture.audio.empty());
}

void FfmpegMediaDecoderTest::distinguishesVideoSinkStopFromCancellation() {
    auto const stoppingVideoSink = [](std::shared_ptr<DecodedVideoFrame const>, FfmpegVideoStreamDiagnostics const&) {
        return false;
    };
    auto const acceptingAudioSink = [](PcmAudioBlock, FfmpegAudioStreamDiagnostics const&, std::stop_token) {
        return true;
    };
    FfmpegMediaDecodeResult const result =
        decodeMediaFrames(requestFor(synchronizedFixturePath(), 50), stoppingVideoSink, acceptingAudioSink);
    QVERIFY(result.isStopped());
    QVERIFY(result.video.stopped);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.isCancelled());
    QVERIFY(result.error.isEmpty());

    FfmpegMediaDecodeResult const videoOnly =
        decodeMediaFrames(requestFor(videoOnlyFixturePath(), 54), stoppingVideoSink, acceptingAudioSink);
    QVERIFY(videoOnly.isStopped());
    QVERIFY(videoOnly.video.stopped);
    QVERIFY(!videoOnly.isSuccess());
    QVERIFY(!videoOnly.isCancelled());
}

void FfmpegMediaDecoderTest::cancelsWhileAudioSubmissionIsBackpressured() {
    using namespace std::chrono_literals;

    ControlledAudioSink sink(4'096);
    sink.reset(52, {48'000, 2});
    FfmpegMediaDecodeResult result;
    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();
    std::jthread decoder([&](std::stop_token stopToken) {
        result = decodeMediaFrames(
            requestFor(synchronizedFixturePath(), 52),
            [](std::shared_ptr<DecodedVideoFrame const>, FfmpegVideoStreamDiagnostics const&) { return true; },
            [&sink](PcmAudioBlock block, FfmpegAudioStreamDiagnostics const&, std::stop_token stopToken) {
                return sink.submit(std::move(block), stopToken);
            },
            stopToken);
        completedPromise.set_value();
    });

    auto const deadline = std::chrono::steady_clock::now() + 10s;
    while (sink.bufferedFrames() == 0 && completed.wait_for(0ms) == std::future_status::timeout &&
           std::chrono::steady_clock::now() < deadline) {
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
    QVERIFY(result.maximumQueuedPacketCount <= result.packetCountLimit);
    QVERIFY(result.maximumQueuedPacketBytes <= std::max(result.packetByteLimit, result.largestQueuedPacketBytes));
}

void FfmpegMediaDecoderTest::honorsCancellationBeforeOpeningMedia() {
    std::stop_source stop;
    stop.request_stop();
    FfmpegMediaDecodeResult const result = decodeMediaFrames(
        requestFor(synchronizedFixturePath(), 53),
        [](std::shared_ptr<DecodedVideoFrame const>, FfmpegVideoStreamDiagnostics const&) { return true; },
        [](PcmAudioBlock, FfmpegAudioStreamDiagnostics const&, std::stop_token) { return true; }, stop.get_token());
    QVERIFY(result.isCancelled());
    QVERIFY(!result.isStopped());
    QVERIFY(!result.isSuccess());
    QVERIFY(result.error.isEmpty());
}

void FfmpegMediaDecoderTest::distinguishesAudioSinkStopFromCancellation() {
    FfmpegMediaDecodeResult const result = decodeMediaFrames(
        requestFor(synchronizedFixturePath(), 51),
        [](std::shared_ptr<DecodedVideoFrame const>, FfmpegVideoStreamDiagnostics const&) { return true; },
        [](PcmAudioBlock, FfmpegAudioStreamDiagnostics const&, std::stop_token) { return false; });
    QVERIFY(result.isStopped());
    QVERIFY(result.audioStopped);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.isCancelled());
    QVERIFY(result.error.isEmpty());
}

QTEST_APPLESS_MAIN(FfmpegMediaDecoderTest)
#include "tst_FfmpegMediaDecoder.moc"
