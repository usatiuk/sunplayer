#include <cstdint>
#include <limits>

#include <QtTest>

#include "media/FfmpegVideoDecoder.h"

class FfmpegVideoTimelineTest final : public QObject {
    Q_OBJECT

private slots:
    void convertsLongPlaybackPositionWithoutNarrowing();
    void includesNonzeroAndNegativeOrigins();
    void rejectsUnrepresentableTimestamp();
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

QTEST_APPLESS_MAIN(FfmpegVideoTimelineTest)

#include "tst_FfmpegVideoTimeline.moc"
