#include <cstdint>
#include <limits>

#include <QtTest>

extern "C" {
#include <libavformat/avformat.h>
}

#include "media/FfmpegVideoDecoder.h"
#include "media/ffmpeg/FfmpegStreamMetadata.h"

class FfmpegVideoTimelineTest final : public QObject {
    Q_OBJECT

private slots:
    void convertsLongPlaybackPositionWithoutNarrowing();
    void includesNonzeroAndNegativeOrigins();
    void rejectsUnrepresentableTimestamp();
    void preservesFfmpegDurationSemantics();
    void finalizesFromObservedPlaybackEndpoints();
    void rejectsTimestampOverflow();
    void requiresTheLatestVideoFrameEndpoint();
    void resolvesOneSharedSelectedStreamOrigin();
};

void FfmpegVideoTimelineTest::
convertsLongPlaybackPositionWithoutNarrowing() {
    const VideoTimelineOrigin origin{
        .timestamp = 0,
        .timeBase = {1, 1000},
    };

    QCOMPARE(
        videoStreamTimestampForPosition(
            origin, {1, 1000}, 3'000'000'000LL),
        std::optional<std::int64_t>(3'000'000));
    QCOMPARE(
        videoStreamTimestampForPosition(
            origin,
            {1, 1000},
            static_cast<std::int64_t>(
                std::numeric_limits<int>::max())
                + 1),
        std::optional<std::int64_t>(2'147'484));
}

void FfmpegVideoTimelineTest::
includesNonzeroAndNegativeOrigins() {
    QCOMPARE(
        videoStreamTimestampForPosition(
            {
                .timestamp = 90'000,
                .timeBase = {1, 90'000},
            },
            {1, 90'000},
            3'000'000'000LL),
        std::optional<std::int64_t>(270'090'000));
    QCOMPARE(
        videoStreamTimestampForPosition(
            {
                .timestamp = -1'000,
                .timeBase = {1, 1'000},
            },
            {1, 1'000},
            3'000'000'000LL),
        std::optional<std::int64_t>(2'999'000));
}

void FfmpegVideoTimelineTest::
rejectsUnrepresentableTimestamp() {
    QVERIFY(!videoStreamTimestampForPosition(
        {
            .timestamp =
                std::numeric_limits<std::int64_t>::max(),
            .timeBase = {1, 1000},
        },
        {1, 1000},
        1'000'000));
    QVERIFY(!videoStreamTimestampForPosition(
        {
            .timestamp = 0,
            .timeBase = {1, 1000},
        },
        {1, 1000},
        -1));
}

void FfmpegVideoTimelineTest::
preservesFfmpegDurationSemantics() {
    AVFormatContext format{};
    AVStream stream{};
    stream.time_base = {1, 1000};
    stream.duration = AV_NOPTS_VALUE;
    format.start_time = 5'000'000;
    format.duration = 8'000'000;
    format.duration_estimation_method =
        AVFMT_DURATION_FROM_PTS;
    QCOMPARE(
        ffmpegProvisionalDurationMicroseconds(format, stream),
        std::optional<std::int64_t>(8'000'000));

    format.duration = 10'000'000;
    format.duration_estimation_method =
        AVFMT_DURATION_FROM_STREAM;
    QCOMPARE(
        ffmpegProvisionalDurationMicroseconds(format, stream),
        std::optional<std::int64_t>(10'000'000));

    format.duration = AV_NOPTS_VALUE;
    stream.duration = 3'000;
    QCOMPARE(
        ffmpegProvisionalDurationMicroseconds(format, stream),
        std::optional<std::int64_t>(3'000'000));
}

void FfmpegVideoTimelineTest::
finalizesFromObservedPlaybackEndpoints() {
    QCOMPARE(
        observedPlaybackDurationMicroseconds(
            3'000'000, 3'000'000),
        std::optional<std::int64_t>(3'000'000));
    QCOMPARE(
        observedPlaybackDurationMicroseconds(
            10'000'000, std::nullopt),
        std::optional<std::int64_t>(10'000'000));
    QCOMPARE(
        observedPlaybackDurationMicroseconds(
            10'000'000, 12'000'000),
        std::optional<std::int64_t>(12'000'000));
    QVERIFY(!observedPlaybackDurationMicroseconds(
        std::nullopt, 3'000'000));
}

void FfmpegVideoTimelineTest::rejectsTimestampOverflow() {
    QCOMPARE(checkedTimestampAdd(5, 7),
        std::optional<std::int64_t>(12));
    QCOMPARE(checkedTimestampSubtract(5, 7),
        std::optional<std::int64_t>(-2));
    QVERIFY(!checkedTimestampAdd(
        std::numeric_limits<std::int64_t>::max(), 1));
    QVERIFY(!checkedTimestampAdd(
        std::numeric_limits<std::int64_t>::min(), -1));
    QVERIFY(!checkedTimestampSubtract(
        std::numeric_limits<std::int64_t>::min(), 1));
    QVERIFY(!checkedTimestampSubtract(
        std::numeric_limits<std::int64_t>::max(), -1));
}

void FfmpegVideoTimelineTest::
requiresTheLatestVideoFrameEndpoint() {
    ObservedVideoRange range;
    range.observeFrame(0, 250'000);
    QCOMPARE(range.endMicroseconds(),
        std::optional<std::int64_t>(250'000));

    range.observeFrame(250'000, std::nullopt);
    QVERIFY(!range.endMicroseconds());

    range.observeFrame(std::nullopt, 250'000);
    QVERIFY(!range.endMicroseconds());

    range.observeFrame(250'000, 250'000);
    QCOMPARE(range.endMicroseconds(),
        std::optional<std::int64_t>(500'000));

    range.observeFrame(500'000, 250'000);
    QCOMPARE(range.endMicroseconds(),
        std::optional<std::int64_t>(750'000));

    range.observeFrame(0, 1'000'000);
    QCOMPARE(range.endMicroseconds(),
        std::optional<std::int64_t>(1'000'000));

    range.observeFrame(1'000'000, std::nullopt);
    QVERIFY(!range.endMicroseconds());

    range.observeFrame(1'000'000, 250'000);
    QCOMPARE(range.endMicroseconds(),
        std::optional<std::int64_t>(1'250'000));
}

void FfmpegVideoTimelineTest::
resolvesOneSharedSelectedStreamOrigin() {
    AVFormatContext format{};
    AVStream video{};
    AVStream audio{};
    video.time_base = {1, 1'000};
    audio.time_base = {1, 48'000};
    format.start_time = AV_NOPTS_VALUE;
    video.start_time = AV_NOPTS_VALUE;
    audio.start_time = AV_NOPTS_VALUE;
    QVERIFY(!ffmpegSharedTimelineOrigin(
        format, video, &audio, std::nullopt));

    video.start_time = 7'000;
    audio.start_time = 240'000;
    const auto streams = ffmpegSharedTimelineOrigin(
        format, video, &audio, std::nullopt);
    QVERIFY(streams);
    QCOMPARE(streams->microseconds(),
        std::optional<std::int64_t>(5'000'000));

    format.start_time = 4'000'000;
    const auto container = ffmpegSharedTimelineOrigin(
        format, video, &audio, std::nullopt);
    QVERIFY(container);
    QCOMPARE(container->microseconds(),
        std::optional<std::int64_t>(4'000'000));

    const VideoTimelineOrigin requested{
        .timestamp = 3'000,
        .timeBase = {1, 1'000},
    };
    QCOMPARE(
        ffmpegSharedTimelineOrigin(
            format, video, &audio, requested),
        std::optional<VideoTimelineOrigin>(requested));
}

QTEST_APPLESS_MAIN(FfmpegVideoTimelineTest)

#include "tst_FfmpegVideoTimeline.moc"
