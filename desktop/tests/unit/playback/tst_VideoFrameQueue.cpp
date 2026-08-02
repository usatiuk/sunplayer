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
    if (!source)
        qFatal("Could not allocate the test video frame");
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
    if (av_frame_get_buffer(source, 0) < 0) {
        av_frame_free(&source);
        qFatal("Could not allocate the test video-frame buffer");
    }

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
    if (!frame)
        qFatal("Could not clone the test video frame: %s", qPrintable(error));
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
        .timelineOrigin = VideoTimelineOrigin{
            .timestamp = 25,
            .timeBase = {1, 1},
        },
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
    void timelineOriginSurvivesDecoderRestart();
    void seekPrerollGateAdmitsTargetFrame();
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
timelineOriginSurvivesDecoderRestart() {
    FfmpegVideoStreamDiagnostics stream = diagnostics();
    stream.timelineOrigin.reset();

    VideoFrameTimeline initialTimeline;
    const QueuedVideoFrame initial =
        initialTimeline.schedule(
            makeFrame(31, 1, 40), stream);
    QVERIFY(initial.diagnostics.timelineOrigin);
    QCOMPARE(initial.presentationTimeMicroseconds, 0);

    VideoFrameTimeline restartedTimeline(
        initial.diagnostics.timelineOrigin);
    const QueuedVideoFrame afterRestart =
        restartedTimeline.schedule(
            makeFrame(32, 1, 48), stream);
    QCOMPARE(
        afterRestart.presentationTimeMicroseconds,
        2'000'000);
    QVERIFY(
        afterRestart.diagnostics.timelineOrigin
        == initial.diagnostics.timelineOrigin);
}

void VideoFrameQueueTest::
seekPrerollGateAdmitsTargetFrame() {
    FfmpegVideoStreamDiagnostics stream = diagnostics();
    stream.timelineOrigin = VideoTimelineOrigin{
        .timestamp = 0,
        .timeBase = {1, 4},
    };
    VideoFrameTimeline timeline(stream.timelineOrigin);
    VideoSeekPrerollGate gate(750'000);

    for (std::uint64_t frameId = 1;
            frameId <= 3;
            ++frameId) {
        const auto rejected = gate.admit(
            timeline.schedule(
                makeFrame(
                    41,
                    frameId,
                    static_cast<std::int64_t>(
                        frameId - 1)),
                stream));
        QVERIFY(!rejected.first);
        QVERIFY(!rejected.second);
    }

    const auto target = gate.admit(
        timeline.schedule(
            makeFrame(41, 4, 3), stream));
    QVERIFY(target.first);
    QVERIFY(!target.second);
    QCOMPARE(
        target.first->presentationTimeMicroseconds,
        750'000);
    QCOMPARE(
        target.first->frame->identity().frameId,
        4U);

    const auto following = gate.admit(
        timeline.schedule(
            makeFrame(41, 5, 4), stream));
    QVERIFY(following.first);
    QVERIFY(!following.second);
    QCOMPARE(
        following.first->frame->identity().frameId,
        5U);

    VideoFrameTimeline endTimeline(stream.timelineOrigin);
    VideoSeekPrerollGate endGate(2'000'000);
    for (std::uint64_t frameId = 1;
            frameId <= 4;
            ++frameId) {
        const auto rejected = endGate.admit(
            endTimeline.schedule(
                makeFrame(
                    42,
                    frameId,
                    static_cast<std::int64_t>(
                        frameId - 1)),
                stream));
        QVERIFY(!rejected.first);
        QVERIFY(!rejected.second);
    }
    const auto endFallback = endGate.finish();
    QVERIFY(endFallback);
    QCOMPARE(
        endFallback->frame->identity().frameId,
        4U);

    VideoFrameTimeline variableTimeline(
        stream.timelineOrigin);
    VideoSeekPrerollGate variableGate(600'000);
    auto variableRejected = variableGate.admit(
        variableTimeline.schedule(
            makeFrame(43, 1, 0, std::nullopt),
            stream));
    QVERIFY(!variableRejected.first);
    QVERIFY(!variableRejected.second);
    variableRejected = variableGate.admit(
        variableTimeline.schedule(
            makeFrame(43, 2, 2, std::nullopt),
            stream));
    QVERIFY(!variableRejected.first);
    QVERIFY(!variableRejected.second);
    const auto variableTarget = variableGate.admit(
        variableTimeline.schedule(
            makeFrame(43, 3, 3, std::nullopt),
            stream));
    QVERIFY(variableTarget.first);
    QVERIFY(variableTarget.second);
    QCOMPARE(
        variableTarget.first->frame->identity().frameId,
        2U);
    QCOMPARE(
        variableTarget.second->frame->identity().frameId,
        3U);

    VideoFrameTimeline exactTimeline(
        stream.timelineOrigin);
    VideoSeekPrerollGate exactGate(500'000);
    const auto exactTarget = exactGate.admit(
        exactTimeline.schedule(
            makeFrame(45, 1, 2, std::nullopt),
            stream));
    QVERIFY(exactTarget.first);
    QVERIFY(!exactTarget.second);
    QCOMPARE(
        exactTarget.first->frame->identity().frameId,
        1U);

    FfmpegVideoStreamDiagnostics offsetStream = stream;
    offsetStream.timelineOrigin = VideoTimelineOrigin{
        .timestamp = 4,
        .timeBase = {1, 4},
    };
    VideoFrameTimeline offsetTimeline(
        offsetStream.timelineOrigin);
    VideoSeekPrerollGate zeroGate(0);
    const auto negativePreroll = zeroGate.admit(
        offsetTimeline.schedule(
            makeFrame(44, 1, 3), offsetStream));
    QVERIFY(!negativePreroll.first);
    QVERIFY(!negativePreroll.second);
    const auto zeroTarget = zeroGate.admit(
        offsetTimeline.schedule(
            makeFrame(44, 2, 4), offsetStream));
    QVERIFY(zeroTarget.first);
    QVERIFY(!zeroTarget.second);
    QCOMPARE(
        zeroTarget.first->frame->identity().frameId,
        2U);
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

    const VideoFrameSelection fixedDue =
        scheduler.selectForPresentation(
            queue,
            9,
            {
                .positionMicroseconds = 500'000,
                .advancing = false,
            },
            true);
    QVERIFY(fixedDue.frame);
    QCOMPARE(
        fixedDue.frame->frame->identity().frameId,
        3U);
    QCOMPARE(fixedDue.droppedFrames, 1U);
    QVERIFY(!fixedDue.reachedEnd);
    QCOMPARE(queue.size(9), 1U);

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
    QCOMPARE(due.droppedFrames, 0U);
    QVERIFY(!due.reachedEnd);
    QCOMPARE(queue.size(9), 0U);

    const VideoFrameSelection heldTail =
        scheduler.selectForPresentation(
            queue,
            9,
            {
                .positionMicroseconds = 1'000'000,
                .advancing = true,
            },
            true,
            1'500'000);
    QVERIFY(!heldTail.frame);
    QVERIFY(!heldTail.reachedEnd);

    const VideoFrameSelection ended =
        scheduler.selectForPresentation(
            queue,
            9,
            {
                .positionMicroseconds = 1'500'000,
                .advancing = false,
                .terminal = true,
            },
            true,
            1'500'000);
    QVERIFY(!ended.frame);
    QVERIFY(ended.reachedEnd);
    QVERIFY(ended.mediaEndMicroseconds);
    QCOMPARE(
        *ended.mediaEndMicroseconds,
        1'500'000);

    queue.reset(10);
    VideoFrameTimeline longFrameTimeline(
        stream.timelineOrigin);
    VideoFrameScheduler declaredEndScheduler;
    QVERIFY(queue.push(
        10,
        longFrameTimeline.schedule(
            makeFrame(10, 1, 25, 4),
            stream),
        {}));
    QVERIFY(declaredEndScheduler.selectFirst(
        queue, 10).frame);
    const VideoFrameSelection declaredEnd =
        declaredEndScheduler.selectForPresentation(
            queue,
            10,
            {
                .positionMicroseconds = 500'000,
                .advancing = true,
            },
            true,
            500'000);
    QVERIFY(declaredEnd.reachedEnd);
    QCOMPARE(
        declaredEnd.mediaEndMicroseconds,
        std::optional<std::int64_t>(500'000));
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
