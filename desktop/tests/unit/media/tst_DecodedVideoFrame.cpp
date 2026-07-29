#include <cmath>
#include <cstdint>
#include <cstring>

#include <QtTest>

extern "C" {
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavutil/buffer.h>
#include <libavutil/display.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "media/DecodedVideoFrame.h"
#include "media/ffmpeg/FfmpegFrameMetadata.h"

class DecodedVideoFrameTest final : public QObject {
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
    void retainsSoftwareStorageAndSnapshotsSemantics();
    void retainsSideDataAndFrameMetadataWins();
    void describesHardwareFromItsSoftwarePlanes();
    void rejectsInvalidIdentityAndCrop();
};

void DecodedVideoFrameTest::
retainsSoftwareStorageAndSnapshotsSemantics() {
    AVFrame *source = av_frame_alloc();
    QVERIFY(source);
    source->format = AV_PIX_FMT_RGB24;
    source->width = 4;
    source->height = 3;
    source->crop_left = 1;
    source->sample_aspect_ratio = {4, 3};
    source->pts = 7;
    source->best_effort_timestamp = 8;
    source->duration = 2;
    source->color_primaries = AVCOL_PRI_BT709;
    source->color_trc = AVCOL_TRC_IEC61966_2_1;
    source->colorspace = AVCOL_SPC_RGB;
    source->color_range = AVCOL_RANGE_JPEG;
    source->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    AVFrameSideData *displayMatrix =
        av_frame_new_side_data(
            source,
            AV_FRAME_DATA_DISPLAYMATRIX,
            9 * sizeof(std::int32_t));
    QVERIFY(displayMatrix);
    av_display_rotation_set(
        reinterpret_cast<std::int32_t *>(
            displayMatrix->data),
        90.0);
    QVERIFY(av_frame_get_buffer(source, 0) >= 0);
    QVERIFY(av_frame_make_writable(source) >= 0);
    source->data[0][0] = 0x5a;

    QString error;
    const std::shared_ptr<const DecodedVideoFrame> frame =
        DecodedVideoFrame::clone(
            *source,
            {
                .playbackGeneration = 3,
                .decoderRevision = 4,
                .frameId = 5,
            },
            {1, 25},
            std::nullopt,
            &error);
    QVERIFY2(frame, qPrintable(error));
    QVERIFY(error.isEmpty());

    av_frame_free(&source);
    QVERIFY(!source);

    QCOMPARE(frame->identity().playbackGeneration, 3U);
    QCOMPARE(frame->identity().decoderRevision, 4U);
    QCOMPARE(frame->identity().frameId, 5U);
    QCOMPARE(frame->timing().pts, std::optional<std::int64_t>(8));
    QCOMPARE(
        frame->timing().duration,
        std::optional<std::int64_t>(2));
    QCOMPARE(frame->timing().timeBase.numerator, 1);
    QCOMPARE(frame->timing().timeBase.denominator, 25);
    QCOMPARE(frame->geometry().codedSize, QSize(4, 3));
    QCOMPARE(frame->geometry().visibleSize, QSize(3, 3));
    QCOMPARE(frame->geometry().crop, QMargins(1, 0, 0, 0));
    QVERIFY(frame->geometry().displayMatrixPresent);
    QVERIFY(
        std::abs(
            std::abs(frame->geometry().rotationDegrees)
            - 90.0)
        <= 0.001);
    QVERIFY(frame->geometry().sampleAspectRatioKnown);
    QCOMPARE(frame->geometry().sampleAspectRatio.numerator, 4);
    QCOMPARE(frame->geometry().sampleAspectRatio.denominator, 3);
    QCOMPARE(
        frame->storage().kind,
        VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(
        frame->storage().softwareFormat,
        QStringLiteral("rgb24"));
    QVERIFY(!frame->storage().graphicsDeviceGeneration);
    QCOMPARE(frame->signal().componentDepth, 8);
    QCOMPARE(
        frame->signal().colorPrimaries,
        QStringLiteral("bt709"));
    QCOMPARE(
        frame->signal().transferFunction,
        QStringLiteral("iec61966-2-1"));
    QCOMPARE(
        frame->signal().matrixCoefficients,
        QStringLiteral("gbr"));
    QCOMPARE(
        frame->signal().colorRange,
        QStringLiteral("pc"));
    QCOMPARE(frame->ffmpegFrame().data[0][0], 0x5a);
    QVERIFY(av_frame_get_side_data(
        &frame->ffmpegFrame(),
        AV_FRAME_DATA_DISPLAYMATRIX));
}

void DecodedVideoFrameTest::
retainsSideDataAndFrameMetadataWins() {
    AVCodecParameters *parameters =
        avcodec_parameters_alloc();
    QVERIFY(parameters);
    parameters->color_primaries = AVCOL_PRI_BT2020;
    parameters->color_trc = AVCOL_TRC_SMPTE2084;
    parameters->color_space = AVCOL_SPC_BT2020_NCL;
    parameters->color_range = AVCOL_RANGE_MPEG;
    parameters->chroma_location = AVCHROMA_LOC_LEFT;

    AVPacketSideData *streamMatrix =
        av_packet_side_data_new(
            &parameters->coded_side_data,
            &parameters->nb_coded_side_data,
            AV_PKT_DATA_DISPLAYMATRIX,
            9 * sizeof(std::int32_t),
            0);
    QVERIFY(streamMatrix);
    std::memset(streamMatrix->data, 0x11, streamMatrix->size);

    AVFrame *frame = av_frame_alloc();
    QVERIFY(frame);
    frame->color_primaries = AVCOL_PRI_BT709;
    AVFrameSideData *frameMatrix =
        av_frame_new_side_data(
            frame,
            AV_FRAME_DATA_DISPLAYMATRIX,
            streamMatrix->size);
    QVERIFY(frameMatrix);
    std::memset(frameMatrix->data, 0x22, frameMatrix->size);

    QVERIFY(mergeStreamVideoMetadata(*frame, *parameters));
    QCOMPARE(frame->color_primaries, AVCOL_PRI_BT709);
    QCOMPARE(frame->color_trc, AVCOL_TRC_SMPTE2084);
    QCOMPARE(frame->colorspace, AVCOL_SPC_BT2020_NCL);
    QCOMPARE(frame->color_range, AVCOL_RANGE_MPEG);
    QCOMPARE(frame->chroma_location, AVCHROMA_LOC_LEFT);
    const AVFrameSideData *retainedMatrix =
        av_frame_get_side_data(
            frame, AV_FRAME_DATA_DISPLAYMATRIX);
    QVERIFY(retainedMatrix);
    QCOMPARE(retainedMatrix->data[0], 0x22);

    av_frame_free(&frame);
    avcodec_parameters_free(&parameters);
}

void DecodedVideoFrameTest::
describesHardwareFromItsSoftwarePlanes() {
    AVFrame *source = av_frame_alloc();
    QVERIFY(source);
    source->format = AV_PIX_FMT_D3D11;
    source->width = 4;
    source->height = 2;
    source->buf[0] = av_buffer_alloc(1);
    QVERIFY(source->buf[0]);
    source->data[0] = source->buf[0]->data;
    source->hw_frames_ctx =
        av_buffer_allocz(sizeof(AVHWFramesContext));
    QVERIFY(source->hw_frames_ctx);
    auto *framesContext =
        reinterpret_cast<AVHWFramesContext *>(
            source->hw_frames_ctx->data);
    framesContext->format = AV_PIX_FMT_D3D11;
    framesContext->sw_format = AV_PIX_FMT_P010;
    framesContext->width = source->width;
    framesContext->height = source->height;

    QString error;
    const std::shared_ptr<const DecodedVideoFrame> frame =
        DecodedVideoFrame::clone(
            *source,
            {
                .playbackGeneration = 1,
                .decoderRevision = 2,
                .frameId = 3,
            },
            {1, 24},
            9,
            &error);
    QVERIFY2(frame, qPrintable(error));
    av_frame_free(&source);

    QCOMPARE(
        frame->storage().kind,
        VideoFrameStorageKind::D3D11Surface);
    QCOMPARE(
        frame->storage().hardwareFormat,
        QStringLiteral("d3d11"));
    QCOMPARE(
        frame->storage().softwareFormat,
        QStringLiteral("p010le"));
    QCOMPARE(frame->signal().pixelFormat, QStringLiteral("p010le"));
    QCOMPARE(frame->signal().componentDepth, 10);
    QVERIFY(
        frame->storage().isCompatibleWithGraphicsDevice(9));
    QVERIFY(
        !frame->storage().isCompatibleWithGraphicsDevice(10));

    AVFrame *missingContext = av_frame_alloc();
    QVERIFY(missingContext);
    missingContext->format = AV_PIX_FMT_D3D11;
    missingContext->width = 4;
    missingContext->height = 2;
    QVERIFY(!DecodedVideoFrame::clone(
        *missingContext,
        {
            .playbackGeneration = 1,
            .decoderRevision = 2,
            .frameId = 4,
        },
        {1, 24},
        9,
        &error));
    QVERIFY(error.contains(QStringLiteral("software-plane")));
    av_frame_free(&missingContext);
}

void DecodedVideoFrameTest::
rejectsInvalidIdentityAndCrop() {
    AVFrame *source = av_frame_alloc();
    QVERIFY(source);
    source->format = AV_PIX_FMT_RGB24;
    source->width = 2;
    source->height = 2;
    source->crop_left = 2;
    QVERIFY(av_frame_get_buffer(source, 0) >= 0);

    QString error;
    QVERIFY(!DecodedVideoFrame::clone(
        *source,
        {
            .playbackGeneration = 1,
            .decoderRevision = 1,
            .frameId = 0,
        },
        {1, 25},
        std::nullopt,
        &error));
    QVERIFY(error.contains(QStringLiteral("identity")));

    QVERIFY(!DecodedVideoFrame::clone(
        *source,
        {
            .playbackGeneration = 1,
            .decoderRevision = 1,
            .frameId = 1,
        },
        {1, 25},
        std::nullopt,
        &error));
    QVERIFY(error.contains(QStringLiteral("geometry")));
    av_frame_free(&source);
}

QTEST_APPLESS_MAIN(DecodedVideoFrameTest)
#include "tst_DecodedVideoFrame.moc"
