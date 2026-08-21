#include <cmath>
#include <cstdint>
#include <cstring>

#include <QtTest>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/display.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "media/DecodedVideoFrame.h"

class DecodedVideoFrameTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void retainsSoftwareStorageAndSnapshotsSemantics();
    void describesHardwareFromItsSoftwarePlanes();
    void rejectsInvalidIdentityAndCrop();
    void classifiesDynamicRange_data();
    void classifiesDynamicRange();
};

void DecodedVideoFrameTest::retainsSoftwareStorageAndSnapshotsSemantics() {
    AVFrame* source = av_frame_alloc();
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
    AVFrameSideData* displayMatrix =
        av_frame_new_side_data(source, AV_FRAME_DATA_DISPLAYMATRIX, 9 * sizeof(std::int32_t));
    QVERIFY(displayMatrix);
    av_display_rotation_set(reinterpret_cast<std::int32_t*>(displayMatrix->data), 90.0);
    QVERIFY(av_frame_get_buffer(source, 0) >= 0);
    QVERIFY(av_frame_make_writable(source) >= 0);
    source->data[0][0] = 0x5a;

    QString error;
    std::shared_ptr<DecodedVideoFrame const> const frame = DecodedVideoFrame::clone(*source,
                                                                                    {
                                                                                        .playbackGeneration = 3,
                                                                                        .decoderRevision = 4,
                                                                                        .frameId = 5,
                                                                                    },
                                                                                    {1, 25}, std::nullopt, true,
                                                                                    &error);
    QVERIFY2(frame, qPrintable(error));
    QVERIFY(error.isEmpty());

    av_frame_free(&source);
    QVERIFY(!source);

    QCOMPARE(frame->identity().playbackGeneration, 3U);
    QCOMPARE(frame->identity().decoderRevision, 4U);
    QCOMPARE(frame->identity().frameId, 5U);
    QCOMPARE(frame->timing().pts, std::optional<std::int64_t>(8));
    QCOMPARE(frame->timing().duration, std::optional<std::int64_t>(2));
    QCOMPARE(frame->timing().timeBase.numerator, 1);
    QCOMPARE(frame->timing().timeBase.denominator, 25);
    QCOMPARE(frame->geometry().codedSize, QSize(4, 3));
    QCOMPARE(frame->geometry().visibleSize, QSize(3, 3));
    QCOMPARE(frame->geometry().crop, QMargins(1, 0, 0, 0));
    QVERIFY(frame->geometry().displayMatrixPresent);
    QVERIFY(std::abs(std::abs(frame->geometry().rotationDegrees) - 90.0) <= 0.001);
    QVERIFY(frame->geometry().sampleAspectRatioKnown);
    QCOMPARE(frame->geometry().sampleAspectRatio.numerator, 4);
    QCOMPARE(frame->geometry().sampleAspectRatio.denominator, 3);
    QCOMPARE(frame->storage().kind, VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(frame->storage().softwareFormat, QStringLiteral("rgb24"));
    QVERIFY(!frame->storage().graphicsDeviceGeneration);
    QCOMPARE(frame->signal().componentDepth, 8);
    QCOMPARE(frame->signal().colorPrimaries, QStringLiteral("bt709"));
    QCOMPARE(frame->signal().transferFunction, QStringLiteral("iec61966-2-1"));
    QCOMPARE(frame->signal().matrixCoefficients, QStringLiteral("gbr"));
    QCOMPARE(frame->signal().colorRange, QStringLiteral("pc"));
    QCOMPARE(frame->dynamicRange(), VideoDynamicRange::Sdr);
    QCOMPARE(frame->dolbyVisionBaseIsHdr10Compatible(), std::optional<bool>(true));
    QCOMPARE(frame->ffmpegFrame().data[0][0], 0x5a);
    QVERIFY(av_frame_get_side_data(&frame->ffmpegFrame(), AV_FRAME_DATA_DISPLAYMATRIX));
}

void DecodedVideoFrameTest::describesHardwareFromItsSoftwarePlanes() {
    AVFrame* source = av_frame_alloc();
    QVERIFY(source);
    source->format = AV_PIX_FMT_D3D11;
    source->width = 4;
    source->height = 2;
    source->buf[0] = av_buffer_alloc(1);
    QVERIFY(source->buf[0]);
    source->data[0] = source->buf[0]->data;
    source->hw_frames_ctx = av_buffer_allocz(sizeof(AVHWFramesContext));
    QVERIFY(source->hw_frames_ctx);
    auto* framesContext = reinterpret_cast<AVHWFramesContext*>(source->hw_frames_ctx->data);
    framesContext->format = AV_PIX_FMT_D3D11;
    framesContext->sw_format = AV_PIX_FMT_P010;
    framesContext->width = source->width;
    framesContext->height = source->height;

    QString error;
    std::shared_ptr<DecodedVideoFrame const> const frame = DecodedVideoFrame::clone(*source,
                                                                                    {
                                                                                        .playbackGeneration = 1,
                                                                                        .decoderRevision = 2,
                                                                                        .frameId = 3,
                                                                                    },
                                                                                    {1, 24}, 9, std::nullopt, &error);
    QVERIFY2(frame, qPrintable(error));
    av_frame_free(&source);

    QCOMPARE(frame->storage().kind, VideoFrameStorageKind::D3D11Surface);
    QCOMPARE(frame->storage().hardwareFormat, QStringLiteral("d3d11"));
    QCOMPARE(frame->storage().softwareFormat, QStringLiteral("p010le"));
    QCOMPARE(frame->signal().pixelFormat, QStringLiteral("p010le"));
    QCOMPARE(frame->signal().componentDepth, 10);
    QVERIFY(frame->storage().isCompatibleWithGraphicsDevice(9));
    QVERIFY(!frame->storage().isCompatibleWithGraphicsDevice(10));

    AVFrame* missingContext = av_frame_alloc();
    QVERIFY(missingContext);
    missingContext->format = AV_PIX_FMT_D3D11;
    missingContext->width = 4;
    missingContext->height = 2;
    QVERIFY(!DecodedVideoFrame::clone(*missingContext,
                                      {
                                          .playbackGeneration = 1,
                                          .decoderRevision = 2,
                                          .frameId = 4,
                                      },
                                      {1, 24}, 9, std::nullopt, &error));
    QVERIFY(error.contains(QStringLiteral("software-plane")));
    av_frame_free(&missingContext);
}

void DecodedVideoFrameTest::rejectsInvalidIdentityAndCrop() {
    AVFrame* source = av_frame_alloc();
    QVERIFY(source);
    source->format = AV_PIX_FMT_RGB24;
    source->width = 2;
    source->height = 2;
    source->crop_left = 2;
    QVERIFY(av_frame_get_buffer(source, 0) >= 0);

    QString error;
    QVERIFY(!DecodedVideoFrame::clone(*source,
                                      {
                                          .playbackGeneration = 1,
                                          .decoderRevision = 1,
                                          .frameId = 0,
                                      },
                                      {1, 25}, std::nullopt, std::nullopt, &error));
    QVERIFY(error.contains(QStringLiteral("identity")));

    QVERIFY(!DecodedVideoFrame::clone(*source,
                                      {
                                          .playbackGeneration = 1,
                                          .decoderRevision = 1,
                                          .frameId = 1,
                                      },
                                      {1, 25}, std::nullopt, std::nullopt, &error));
    QVERIFY(error.contains(QStringLiteral("geometry")));
    av_frame_free(&source);
}

void DecodedVideoFrameTest::classifiesDynamicRange_data() {
    QTest::addColumn<int>("primaries");
    QTest::addColumn<int>("transfer");
    QTest::addColumn<int>("sideData");
    QTest::addColumn<int>("expected");

    constexpr int mastering = 1 << 0;
    constexpr int hdr10Plus = 1 << 1;
    constexpr int dolbyVision = 1 << 2;
    constexpr int emptyMastering = 1 << 3;
    constexpr int truncatedHdr10Plus = 1 << 4;
    constexpr int rawDolbyVision = 1 << 5;
    constexpr int contentLight = 1 << 6;
    constexpr int emptyContentLight = 1 << 7;
    constexpr int truncatedDolbyVision = 1 << 8;
    constexpr int emptyDolbyVision = 1 << 9;
    constexpr int parsedHdr10Plus = 1 << 10;
    QTest::newRow("unknown") << static_cast<int>(AVCOL_PRI_UNSPECIFIED) << static_cast<int>(AVCOL_TRC_UNSPECIFIED) << 0
                             << static_cast<int>(VideoDynamicRange::Unknown);
    QTest::newRow("sdr") << static_cast<int>(AVCOL_PRI_BT709) << static_cast<int>(AVCOL_TRC_BT709) << 0
                         << static_cast<int>(VideoDynamicRange::Sdr);
    QTest::newRow("generic-pq") << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << 0
                                << static_cast<int>(VideoDynamicRange::Pq);
    QTest::newRow("hdr10") << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << mastering
                           << static_cast<int>(VideoDynamicRange::Hdr10);
    QTest::newRow("hdr10-content-light") << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084)
                                         << contentLight << static_cast<int>(VideoDynamicRange::Hdr10);
    QTest::newRow("empty-mastering-is-generic-pq")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << emptyMastering
        << static_cast<int>(VideoDynamicRange::Pq);
    QTest::newRow("empty-content-light-is-generic-pq")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << emptyContentLight
        << static_cast<int>(VideoDynamicRange::Pq);
    QTest::newRow("hdr10-plus") << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084)
                                << hdr10Plus << static_cast<int>(VideoDynamicRange::Hdr10Plus);
    QTest::newRow("decoder-parsed-hdr10-plus")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << parsedHdr10Plus
        << static_cast<int>(VideoDynamicRange::Hdr10Plus);
    QTest::newRow("truncated-hdr10-plus-is-generic-pq")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << truncatedHdr10Plus
        << static_cast<int>(VideoDynamicRange::Pq);
    QTest::newRow("hlg") << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_ARIB_STD_B67) << 0
                         << static_cast<int>(VideoDynamicRange::Hlg);
    QTest::newRow("dolby-vision-precedence")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << (hdr10Plus | dolbyVision)
        << static_cast<int>(VideoDynamicRange::DolbyVision);
    QTest::newRow("raw-dolby-vision-is-generic-pq")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << rawDolbyVision
        << static_cast<int>(VideoDynamicRange::Pq);
    QTest::newRow("truncated-dolby-vision-is-generic-pq")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << truncatedDolbyVision
        << static_cast<int>(VideoDynamicRange::Pq);
    QTest::newRow("empty-dolby-vision-is-generic-pq")
        << static_cast<int>(AVCOL_PRI_BT2020) << static_cast<int>(AVCOL_TRC_SMPTE2084) << emptyDolbyVision
        << static_cast<int>(VideoDynamicRange::Pq);
}

void DecodedVideoFrameTest::classifiesDynamicRange() {
    QFETCH(int, primaries);
    QFETCH(int, transfer);
    QFETCH(int, sideData);
    QFETCH(int, expected);

    AVFrame* source = av_frame_alloc();
    QVERIFY(source);
    source->format = AV_PIX_FMT_RGB24;
    source->width = 2;
    source->height = 2;
    source->color_primaries = static_cast<AVColorPrimaries>(primaries);
    source->color_trc = static_cast<AVColorTransferCharacteristic>(transfer);
    source->colorspace = AVCOL_SPC_RGB;
    source->color_range = AVCOL_RANGE_JPEG;
    source->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    QVERIFY(av_frame_get_buffer(source, 0) >= 0);

    if (sideData & (1 << 0)) {
        AVFrameSideData* const mastering = av_frame_new_side_data(source, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA,
                                                                  sizeof(AVMasteringDisplayMetadata));
        QVERIFY(mastering);
        std::memset(mastering->data, 0, mastering->size);
        auto* const metadata = reinterpret_cast<AVMasteringDisplayMetadata*>(mastering->data);
        metadata->has_luminance = 1;
        metadata->min_luminance = {1, 1'000};
        metadata->max_luminance = {1'000, 1};
    }
    if (sideData & ((1 << 1) | (1 << 10))) {
        AVFrameSideData* const hdr10Plus =
            av_frame_new_side_data(source, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, sizeof(AVDynamicHDRPlus));
        QVERIFY(hdr10Plus);
        std::memset(hdr10Plus->data, 0, hdr10Plus->size);
        auto* const metadata = reinterpret_cast<AVDynamicHDRPlus*>(hdr10Plus->data);
        metadata->itu_t_t35_country_code = sideData & (1 << 1) ? 0xb5 : 0;
        metadata->application_version = 1;
        metadata->num_windows = 1;
    }
    if (sideData & (1 << 2)) {
        std::size_t metadataSize = 0;
        AVDOVIMetadata* const metadata = av_dovi_metadata_alloc(&metadataSize);
        QVERIFY(metadata);
        AVFrameSideData* const dolbyVision = av_frame_new_side_data(source, AV_FRAME_DATA_DOVI_METADATA, metadataSize);
        QVERIFY(dolbyVision);
        std::memcpy(dolbyVision->data, metadata, metadataSize);
        av_free(metadata);
    }
    if (sideData & (1 << 3)) {
        AVFrameSideData* const mastering = av_frame_new_side_data(source, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA,
                                                                  sizeof(AVMasteringDisplayMetadata));
        QVERIFY(mastering);
        std::memset(mastering->data, 0, mastering->size);
    }
    if (sideData & (1 << 4)) {
        QVERIFY(av_frame_new_side_data(source, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, 1));
    }
    if (sideData & (1 << 5)) {
        QVERIFY(av_frame_new_side_data(source, AV_FRAME_DATA_DOVI_RPU_BUFFER, 1));
    }
    if (sideData & (1 << 6)) {
        AVFrameSideData* const contentLight =
            av_frame_new_side_data(source, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL, sizeof(AVContentLightMetadata));
        QVERIFY(contentLight);
        std::memset(contentLight->data, 0, contentLight->size);
        auto* const metadata = reinterpret_cast<AVContentLightMetadata*>(contentLight->data);
        metadata->MaxCLL = 1'000;
        metadata->MaxFALL = 400;
    }
    if (sideData & (1 << 7)) {
        AVFrameSideData* const contentLight =
            av_frame_new_side_data(source, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL, sizeof(AVContentLightMetadata));
        QVERIFY(contentLight);
        std::memset(contentLight->data, 0, contentLight->size);
    }
    if (sideData & (1 << 8)) {
        QVERIFY(av_frame_new_side_data(source, AV_FRAME_DATA_DOVI_METADATA, 1));
    }
    if (sideData & (1 << 9)) {
        AVFrameSideData* const dolbyVision =
            av_frame_new_side_data(source, AV_FRAME_DATA_DOVI_METADATA, sizeof(AVDOVIMetadata));
        QVERIFY(dolbyVision);
        std::memset(dolbyVision->data, 0, dolbyVision->size);
    }

    QString error;
    std::shared_ptr<DecodedVideoFrame const> const frame = DecodedVideoFrame::clone(*source,
                                                                                    {
                                                                                        .playbackGeneration = 1,
                                                                                        .decoderRevision = 1,
                                                                                        .frameId = 1,
                                                                                    },
                                                                                    {1, 24}, std::nullopt, std::nullopt,
                                                                                    &error);
    QVERIFY2(frame, qPrintable(error));
    QCOMPARE(static_cast<int>(frame->dynamicRange()), expected);
    av_frame_free(&source);
}

QTEST_APPLESS_MAIN(DecodedVideoFrameTest)
#include "tst_DecodedVideoFrame.moc"
