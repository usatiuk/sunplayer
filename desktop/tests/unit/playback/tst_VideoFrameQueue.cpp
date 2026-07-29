#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <QtTest>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "media/DecodedVideoFrame.h"
#include "playback/CoalescedGenerationWake.h"
#include "playback/VideoFrameQueue.h"
#include "playback/VideoFrameScheduler.h"

namespace {
std::shared_ptr<const DecodedVideoFrame> makeFrame(
        std::uint64_t generation,
        std::uint64_t frameId,
        std::optional<std::int64_t> pts,
        std::optional<std::int64_t> duration = 1) {
    AVFrame *source = av_frame_alloc();
    Q_ASSERT(source);
    source->format = AV_PIX_FMT_RGB24;
    source->width = 2;
    source->height = 2;
    source->pts = pts.value_or(AV_NOPTS_VALUE);
    source->best_effort_timestamp =
        pts.value_or(AV_NOPTS_VALUE);
    source->duration = duration.value_or(0);
    source->color_primaries = AVCOL_PRI_BT709;
    source->color_trc = AVCOL_TRC_IEC61966_2_1;
    source->colorspace = AVCOL_SPC_RGB;
    source->color_range = AVCOL_RANGE_JPEG;
    source->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    Q_ASSERT(av_frame_get_buffer(source, 0) >= 0);

    QString error;
    std::shared_ptr<const DecodedVideoFrame> frame =
        DecodedVideoFrame::clone(
            *source,
            {
                .playbackGeneration = generation,
                .decoderRevision = 1,
                .frameId = frameId,
            },
            {1, 4},
            std::nullopt,
            &error);
    av_frame_free(&source);
    Q_ASSERT_X(frame, "makeFrame", qPrintable(error));
    return frame;
}

FfmpegVideoStreamDiagnostics diagnostics() {
    return {
        .containerFormat = QStringLiteral("test"),
        .decoderName = QStringLiteral("test"),
        .decodePath = QStringLiteral("Software"),
        .videoStreamIndex = 0,
        .hardwareAccelerated = false,
        .durationMicroseconds = 1'000'000,
        .timelineOriginMicroseconds = 25'000'000,
        .nominalFrameDurationMicroseconds = 250'000,
    };
}
}

class VideoFrameQueueTest final : public QObject {
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
    void timelinePreservesIntegerTimingAndFillsMissingPts();
    void schedulerUsesClockSnapshotForDropAndEndPolicy();
    void coalescedWakeBoundsBurstAndHandsOff();
    void boundedQueueBackpressuresAndReleasesCapacity();
    void generationResetWakesAndRejectsOldProducer();
    void stopRequestWakesBlockedProducer();
};

void VideoFrameQueueTest::
timelinePreservesIntegerTimingAndFillsMissingPts() {
    VideoFrameTimeline timeline;
    const FfmpegVideoStreamDiagnostics stream =
        diagnostics();

    const QueuedVideoFrame first = timeline.schedule(
        makeFrame(7, 1, 100), stream);
    const QueuedVideoFrame second = timeline.schedule(
        makeFrame(7, 2, 101), stream);
    const QueuedVideoFrame missing = timeline.schedule(
        makeFrame(7, 3, std::nullopt), stream);
    const QueuedVideoFrame repeated = timeline.schedule(
        makeFrame(7, 4, 101), stream);

    QCOMPARE(first.presentationTimeMicroseconds, 0);
    QCOMPARE(first.durationMicroseconds, 250'000);
    QCOMPARE(second.presentationTimeMicroseconds, 250'000);
    QCOMPARE(missing.presentationTimeMicroseconds, 500'000);
    QCOMPARE(repeated.presentationTimeMicroseconds, 500'000);
}

void VideoFrameQueueTest::
schedulerUsesClockSnapshotForDropAndEndPolicy() {
    VideoFrameQueue queue;
    queue.reset(9);
    VideoFrameTimeline timeline;
    VideoFrameScheduler scheduler;
    const FfmpegVideoStreamDiagnostics stream =
        diagnostics();

    QVERIFY(queue.push(
        9,
        timeline.schedule(
            makeFrame(9, 1, 100), stream),
        {}));
    VideoFrameSelection first =
        scheduler.selectFirst(queue, 9);
    QVERIFY(first.frame);
    QCOMPARE(
        first.frame->frame->identity().frameId,
        1U);

    for (std::uint64_t frameId = 2;
            frameId <= 4;
            ++frameId) {
        QVERIFY(queue.push(
            9,
            timeline.schedule(
                makeFrame(
                    9,
                    frameId,
                    99 + frameId),
                stream),
            {}));
    }

    const VideoFrameSelection paused =
        scheduler.selectForPresentation(
            queue,
            9,
            {
                .positionMicroseconds = 1'000'000,
                .advancing = false,
            },
            true);
    QVERIFY(!paused.frame);
    QVERIFY(!paused.reachedEnd);
    QCOMPARE(queue.size(9), 3U);

    VideoFrameSelection due =
        scheduler.selectForPresentation(
            queue,
            9,
            {
                .positionMicroseconds = 750'000,
                .advancing = true,
            },
            true);
    QVERIFY(due.frame);
    QCOMPARE(
        due.frame->frame->identity().frameId,
        4U);
    QCOMPARE(due.droppedFrames, 2U);
    QVERIFY(!due.reachedEnd);
    QCOMPARE(queue.size(9), 0U);

    const VideoFrameSelection ended =
        scheduler.selectForPresentation(
            queue,
            9,
            {
                .positionMicroseconds = 1'000'000,
                .advancing = true,
            },
            true);
    QVERIFY(!ended.frame);
    QVERIFY(ended.reachedEnd);
    QVERIFY(ended.mediaEndMicroseconds);
    QCOMPARE(
        *ended.mediaEndMicroseconds,
        1'000'000);
}

void VideoFrameQueueTest::
coalescedWakeBoundsBurstAndHandsOff() {
    CoalescedGenerationWake wake;
    QVERIFY(wake.request(1));
    for (std::uint64_t generation = 2;
            generation <= 1'000;
            ++generation) {
        QVERIFY(!wake.request(generation));
    }

    std::vector<std::uint64_t> delivered;
    wake.drain(
        [&](std::uint64_t generation) {
            delivered.push_back(generation);
        });
    QCOMPARE(delivered.size(), 1U);
    QCOMPARE(delivered.front(), 1'000U);

    QVERIFY(wake.request(1'001));
    bool nestedRequestNeedsOwner = true;
    wake.drain(
        [&](std::uint64_t generation) {
            delivered.push_back(generation);
            if (generation == 1'001) {
                nestedRequestNeedsOwner =
                    wake.request(1'002);
            }
        });
    QVERIFY(!nestedRequestNeedsOwner);
    QCOMPARE(delivered.size(), 3U);
    QCOMPARE(delivered[1], 1'001U);
    QCOMPARE(delivered[2], 1'002U);
    QVERIFY(wake.request(1'003));
}

void VideoFrameQueueTest::
boundedQueueBackpressuresAndReleasesCapacity() {
    VideoFrameQueue queue;
    queue.reset(3);
    VideoFrameTimeline timeline;
    const FfmpegVideoStreamDiagnostics stream =
        diagnostics();

    for (std::uint64_t frameId = 1;
            frameId <= VideoFrameQueue::capacity;
            ++frameId) {
        QVERIFY(queue.push(
            3,
            timeline.schedule(
                makeFrame(3, frameId, 100 + frameId),
                stream),
            {}));
    }
    QCOMPARE(queue.size(3), VideoFrameQueue::capacity);
    QCOMPARE(
        queue.totalPushed(3),
        VideoFrameQueue::capacity);

    std::atomic_bool started = false;
    std::atomic_bool completed = false;
    std::jthread producer(
        [&](std::stop_token stopToken) {
            started = true;
            completed = queue.push(
                3,
                timeline.schedule(
                    makeFrame(3, 4, 104), stream),
                stopToken);
        });
    QTRY_VERIFY_WITH_TIMEOUT(started.load(), 2000);
    QVERIFY(!completed.load());
    QCOMPARE(
        queue.totalPushed(3),
        VideoFrameQueue::capacity);

    QVERIFY(queue.pop(3));
    QTRY_VERIFY_WITH_TIMEOUT(completed.load(), 2000);
    QCOMPARE(queue.size(3), VideoFrameQueue::capacity);
    QCOMPARE(
        queue.totalPushed(3),
        VideoFrameQueue::capacity + 1);
    QCOMPARE(
        queue.maximumObservedSize(),
        VideoFrameQueue::capacity);
}

void VideoFrameQueueTest::
generationResetWakesAndRejectsOldProducer() {
    VideoFrameQueue queue;
    queue.reset(11);
    VideoFrameTimeline timeline;
    const FfmpegVideoStreamDiagnostics stream =
        diagnostics();
    for (std::uint64_t frameId = 1;
            frameId <= VideoFrameQueue::capacity;
            ++frameId) {
        QVERIFY(queue.push(
            11,
            timeline.schedule(
                makeFrame(11, frameId, frameId),
                stream),
            {}));
    }

    std::atomic_bool started = false;
    std::atomic_bool accepted = true;
    std::atomic_bool completed = false;
    std::jthread producer(
        [&](std::stop_token stopToken) {
            started = true;
            accepted = queue.push(
                11,
                timeline.schedule(
                    makeFrame(11, 4, 4), stream),
                stopToken);
            completed = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(started.load(), 2000);
    QVERIFY(!completed.load());

    queue.reset(12);
    QTRY_VERIFY_WITH_TIMEOUT(completed.load(), 2000);
    QVERIFY(!accepted.load());
    QCOMPARE(queue.size(11), 0U);
    QCOMPARE(queue.size(12), 0U);
}

void VideoFrameQueueTest::
stopRequestWakesBlockedProducer() {
    VideoFrameQueue queue;
    queue.reset(21);
    VideoFrameTimeline timeline;
    const FfmpegVideoStreamDiagnostics stream =
        diagnostics();
    for (std::uint64_t frameId = 1;
            frameId <= VideoFrameQueue::capacity;
            ++frameId) {
        QVERIFY(queue.push(
            21,
            timeline.schedule(
                makeFrame(21, frameId, frameId),
                stream),
            {}));
    }

    std::atomic_bool started = false;
    std::atomic_bool accepted = true;
    std::jthread producer(
        [&](std::stop_token stopToken) {
            started = true;
            accepted = queue.push(
                21,
                timeline.schedule(
                    makeFrame(21, 4, 4), stream),
                stopToken);
        });
    QTRY_VERIFY_WITH_TIMEOUT(started.load(), 2000);
    QVERIFY(accepted.load());

    producer.request_stop();
    producer.join();
    QVERIFY(!accepted.load());
    QCOMPARE(queue.size(21), VideoFrameQueue::capacity);
}

QTEST_GUILESS_MAIN(VideoFrameQueueTest)
#include "tst_VideoFrameQueue.moc"
