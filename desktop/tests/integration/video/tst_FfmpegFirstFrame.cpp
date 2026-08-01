#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QPoint>
#include <QRegularExpression>
#include <QtCore/qfloat16.h>
#include <QtTest>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
}

#include <rhi/qrhi.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "graphics/GraphicsBackendFactory.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "media/DecodedVideoFrame.h"
#include "media/FfmpegFirstFrameDecoder.h"
#include "media/FfmpegHardwareDevice.h"
#include "media/ffmpeg/FfmpegVideoDecodeFallback.h"
#include "media/ffmpeg/FfmpegVideoPacketDecoder.h"
#include "playback/VideoFrameQueue.h"
#include "presentation/HdrCompositor.h"
#include "video/DecodedVideoSource.h"
#include "video/LibplaceboDecodedVideoProducer.h"

namespace {
struct FloatPixel {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
};

FloatPixel pixel(
        const QRhiReadbackResult &readback,
        int x,
        int y) {
    Q_ASSERT(readback.format == QRhiTexture::RGBA16F);
    Q_ASSERT(x >= 0 && x < readback.pixelSize.width());
    Q_ASSERT(y >= 0 && y < readback.pixelSize.height());
    Q_ASSERT(readback.data.size()
             >= readback.pixelSize.width()
                    * readback.pixelSize.height()
                    * 4 * qsizetype(sizeof(qfloat16)));
    const qsizetype offset =
        (static_cast<qsizetype>(y)
         * readback.pixelSize.width()
         + x) * 4 * sizeof(qfloat16);
    std::array<qfloat16, 4> values;
    std::memcpy(
        values.data(),
        readback.data.constData() + offset,
        sizeof(values));
    return {
        static_cast<float>(values[0]),
        static_cast<float>(values[1]),
        static_cast<float>(values[2]),
        static_cast<float>(values[3]),
    };
}

QByteArray expectedFixtureHash(const QString &manifestPath) {
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly))
        return {};
    const QString contents =
        QString::fromUtf8(manifest.readAll());
    const QRegularExpressionMatch match =
        QRegularExpression(
            QStringLiteral(
                "^sha256\\s*=\\s*\"([0-9a-f]{64})\"\\s*$"),
            QRegularExpression::MultilineOption)
            .match(contents);
    return match.hasMatch()
        ? match.captured(1).toLatin1()
        : QByteArray();
}

QByteArray fixtureHash(const QString &fixturePath) {
    QFile fixture(fixturePath);
    if (!fixture.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(
               fixture.readAll(),
               QCryptographicHash::Sha256)
        .toHex();
}

void compareNear(float actual, float expected, float tolerance) {
    QVERIFY2(
        std::abs(actual - expected) <= tolerance,
        qPrintable(QStringLiteral(
            "Expected %1 ± %2, got %3")
            .arg(expected)
            .arg(tolerance)
            .arg(actual)));
}

RenderedVideoSurfaceState surfaceState(
        GraphicsDeviceDomain &graphics,
        std::uint64_t contentRevision,
        float referenceWhiteNits = 203.0f,
        QSize pixelSize = {4, 4}) {
    return {
        .description = {
            .pixelSize = pixelSize,
            .pixelFormat =
                RenderedVideoPixelFormat::Rgba16Float,
            .colorSpace =
                RenderedVideoColorSpace::LinearSrgb,
            .luminance =
                RenderedVideoLuminance::
                    DisplayTargetedSdrWhiteRelative,
            .alphaMode =
                RenderedVideoAlphaMode::Opaque,
            .referenceWhiteNits = referenceWhiteNits,
            .targetMinimumLuminanceKnown = true,
            .targetMinimumLuminanceNits = 0.0f,
            .targetPeakHeadroom = 1.0f,
        },
        .graphicsDeviceGeneration = graphics.generation(),
        .contentRevision = contentRevision,
    };
}

struct DecodedFrameCapture {
    QString error;
    QRhiReadbackResult readback;
    VideoFrameImportDiagnostics input;
    RenderedVideoProducerDiagnostics producer;

    bool isSuccess() const {
        return error.isEmpty()
            && input.isValid()
            && producer.isValid()
            && !readback.data.isEmpty();
    }
};

DecodedFrameCapture captureDecodedFrame(
        GraphicsDeviceDomain &graphics,
        std::shared_ptr<const DecodedVideoFrame> frame) {
    DecodedFrameCapture result;
    GraphicsDeviceExecutionScope execution =
        graphics.acquireExecutionScope();
    QRhi &rhi = graphics.rhi();
    DecodedVideoSource source(
        std::move(frame), VideoTargetReadback::Enabled);
    LibplaceboDecodedVideoProducer producer(
        graphics, source, VideoTargetReadback::Enabled);
    const RenderedVideoSurfaceState state =
        surfaceState(
            graphics,
            source.contentRevision(),
            203.0f,
            source.currentFrame()->geometry().visibleSize);
    if (producer.ensureSurface(state)
            != VideoOperationResult::Ready) {
        result.error =
            producer.diagnostics().target.fallbackReason;
        return result;
    }

    QRhiCommandBuffer *commandBuffer = nullptr;
    if (rhi.beginOffscreenFrame(&commandBuffer)
            != QRhi::FrameOpSuccess
            || !commandBuffer) {
        result.error =
            QStringLiteral("Could not begin offscreen frame");
        return result;
    }

    const auto abortFrame = [&] {
        rhi.endOffscreenFrame(QRhi::SkipPresent);
        producer.submissionAborted();
        producer.discardPendingRender();
    };
    if (producer.render(*commandBuffer, state)
            != VideoOperationResult::Ready
            || producer.prepareForComposition(*commandBuffer)
                != VideoOperationResult::Ready) {
        result.error =
            producer.diagnostics().target.fallbackReason;
        abortFrame();
        result.input =
            producer.frameImportDiagnostics();
        result.producer = producer.diagnostics();
        return result;
    }

    bool readbackCompleted = false;
    result.readback.completed = [&readbackCompleted] {
        readbackCompleted = true;
    };
    QRhiResourceUpdateBatch *updates =
        rhi.nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(
            &producer.textureForComposition()),
        &result.readback);
    commandBuffer->resourceUpdate(updates);

    const QRhi::FrameOpResult frameResult =
        rhi.endOffscreenFrame();
    if (frameResult == QRhi::FrameOpSuccess) {
        producer.submissionAccepted();
        producer.commitPendingRender();
    } else {
        producer.submissionAborted();
        producer.discardPendingRender();
        result.error =
            QStringLiteral("Could not finish offscreen frame");
        return result;
    }
    if (!readbackCompleted) {
        result.error =
            QStringLiteral("Decoded-frame readback did not complete");
        return result;
    }
    result.input = producer.frameImportDiagnostics();
    result.producer = producer.diagnostics();
    return result;
}
}

class FfmpegFirstFrameTest final : public QObject {
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
    void retainsStreamHdr10PlusAtTheDecodeBoundary();
    void realDemuxDecodeImportAndComposition();
    void compressedYuvMetadataAndRendering();
    void continuousDecodeDrainsEveryFrame();
    void seekDecodesInterFramePreroll();
    void longTimelineSeekUses64BitTarget();
    void hardwareDecodeFailureRetriesSoftware();
    void continuousD3d11DecodeRetainsBoundedFrames();
    void d3d11HardwareDecodeDirectImport();
};

void FfmpegFirstFrameTest::
retainsStreamHdr10PlusAtTheDecodeBoundary() {
    const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_RAWVIDEO);
    QVERIFY(decoder);

    const auto freeParameters = [](AVCodecParameters *parameters) {
        avcodec_parameters_free(&parameters);
    };
    std::unique_ptr<AVCodecParameters, decltype(freeParameters)> parameters(
        avcodec_parameters_alloc(), freeParameters);
    QVERIFY(parameters);
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->codec_id = AV_CODEC_ID_RAWVIDEO;
    parameters->format = AV_PIX_FMT_RGB24;
    parameters->width = 2;
    parameters->height = 2;
    parameters->color_primaries = AVCOL_PRI_BT2020;
    parameters->color_trc = AVCOL_TRC_SMPTE2084;
    parameters->color_space = AVCOL_SPC_RGB;
    parameters->color_range = AVCOL_RANGE_JPEG;

    AVPacketSideData *streamMetadata = av_packet_side_data_new(
        &parameters->coded_side_data,
        &parameters->nb_coded_side_data,
        AV_PKT_DATA_DYNAMIC_HDR10_PLUS,
        sizeof(AVDynamicHDRPlus),
        0);
    QVERIFY(streamMetadata);
    std::memset(
        streamMetadata->data,
        0,
        streamMetadata->size);
    auto *hdr10Plus = reinterpret_cast<AVDynamicHDRPlus *>(
        streamMetadata->data);
    hdr10Plus->itu_t_t35_country_code = 0xb5;
    hdr10Plus->application_version = 0;
    hdr10Plus->num_windows = 1;
    hdr10Plus->params[0].maxscl[0] = {1, 1};
    hdr10Plus->params[0].maxscl[1] = {4, 5};
    hdr10Plus->params[0].maxscl[2] = {3, 5};
    hdr10Plus->params[0].average_maxrgb = {1, 2};

    std::array<FfmpegAvPacketPtr, 2> packets;
    for (std::size_t index = 0; index < packets.size(); ++index) {
        AVPacket *rawPacket = av_packet_alloc();
        QVERIFY(rawPacket);
        QVERIFY(av_new_packet(rawPacket, 2 * 2 * 3) >= 0);
        std::memset(rawPacket->data, 0x80, rawPacket->size);
        rawPacket->pts = static_cast<std::int64_t>(index);
        rawPacket->duration = 1;
        packets[index].reset(rawPacket);
    }

    auto *packetMetadata = reinterpret_cast<AVDynamicHDRPlus *>(
        av_packet_new_side_data(
            packets[0].get(),
            AV_PKT_DATA_DYNAMIC_HDR10_PLUS,
            sizeof(AVDynamicHDRPlus)));
    QVERIFY(packetMetadata);
    std::memset(packetMetadata, 0, sizeof(*packetMetadata));
    packetMetadata->itu_t_t35_country_code = 0xb5;
    packetMetadata->application_version = 0;
    packetMetadata->num_windows = 1;
    packetMetadata->params[0].maxscl[0] = {1, 1};
    packetMetadata->params[0].maxscl[1] = {4, 5};
    packetMetadata->params[0].maxscl[2] = {3, 5};
    packetMetadata->params[0].average_maxrgb = {3, 4};

    std::size_t nextPacket = 0;
    std::vector<std::shared_ptr<const DecodedVideoFrame>> decodedFrames;
    bool hardwareSelected = false;

    const FfmpegVideoDecodeResult result = decodeFfmpegVideoPackets(
        {
            .path = QStringLiteral("memory:hdr10plus"),
            .firstFrameIdentity = {
                .playbackGeneration = 31,
                .decoderRevision = 1,
                .frameId = 1,
            },
        },
        *decoder,
        *parameters,
        {1, 24},
        {1, 1},
        {
            .containerFormat = QStringLiteral("rawvideo-test"),
            .decoderName = QString::fromLatin1(decoder->name),
            .decodePath = QStringLiteral("Software"),
            .videoStreamIndex = 0,
        },
        [&packets, &nextPacket](std::stop_token) {
            FfmpegVideoPacketRead read;
            if (nextPacket < packets.size()) {
                read.packet = std::move(packets[nextPacket++]);
            } else {
                read.terminal =
                    FfmpegVideoPacketTerminal::EndOfStream;
            }
            return read;
        },
        [&decodedFrames](
                std::shared_ptr<const DecodedVideoFrame> frame,
                const FfmpegVideoStreamDiagnostics &) {
            decodedFrames.push_back(std::move(frame));
            return true;
        },
        {},
        &hardwareSelected);

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!hardwareSelected);
    QCOMPARE(decodedFrames.size(), std::size_t(2));

    const auto retainedHdr10Plus = [&decodedFrames](std::size_t index) {
        const AVFrameSideData *retained = av_frame_get_side_data(
            &decodedFrames[index]->ffmpegFrame(),
            AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
        if (!retained
                || retained->size < sizeof(AVDynamicHDRPlus)) {
            return static_cast<const AVDynamicHDRPlus *>(nullptr);
        }
        return reinterpret_cast<const AVDynamicHDRPlus *>(
            retained->data);
    };

    const AVDynamicHDRPlus *first = retainedHdr10Plus(0);
    QVERIFY(first);
    QCOMPARE(first->application_version, 0);
    QCOMPARE(first->num_windows, 1);
    QCOMPARE(first->params[0].average_maxrgb.num, 3);
    QCOMPARE(first->params[0].average_maxrgb.den, 4);

    const AVDynamicHDRPlus *second = retainedHdr10Plus(1);
    QVERIFY(second);
    QCOMPARE(second->application_version, 0);
    QCOMPARE(second->num_windows, 1);
    QCOMPARE(second->params[0].average_maxrgb.num, 1);
    QCOMPARE(second->params[0].average_maxrgb.den, 2);
}

void FfmpegFirstFrameTest::
realDemuxDecodeImportAndComposition() {
#ifndef Q_OS_WIN
    QSKIP("The current libplacebo integration is Windows D3D11");
#else
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-rgb-first-frame.ppm");
    const QString manifest = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-rgb-first-frame.toml");
    const QByteArray declaredHash =
        expectedFixtureHash(manifest);
    QVERIFY2(
        !declaredHash.isEmpty(),
        "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    const FfmpegFirstFrameResult decoded =
        decodeFirstVideoFrame(
            fixture,
            {
                .playbackGeneration = 7,
                .decoderRevision = 11,
                .frameId = 1,
            });
    QVERIFY2(decoded.isSuccess(), qPrintable(decoded.error));
    const FfmpegFirstFrameResult secondDecode =
        decodeFirstVideoFrame(
            fixture,
            {
                .playbackGeneration = 7,
                .decoderRevision = 11,
                .frameId = 2,
            });
    QVERIFY2(
        secondDecode.isSuccess(),
        qPrintable(secondDecode.error));
    QVERIFY(
        decoded.frame->identity()
        != secondDecode.frame->identity());
    const bool expectedImageDemuxer =
        decoded.diagnostics.containerFormat.contains(
            QStringLiteral("image"), Qt::CaseInsensitive)
        || decoded.diagnostics.containerFormat.contains(
            QStringLiteral("ppm"), Qt::CaseInsensitive);
    QVERIFY2(
        expectedImageDemuxer,
        qPrintable(QStringLiteral(
            "Unexpected FFmpeg image demuxer: %1")
            .arg(decoded.diagnostics.containerFormat)));
    QCOMPARE(
        decoded.diagnostics.decoderName,
        QStringLiteral("ppm"));
    QCOMPARE(
        decoded.frame->identity().playbackGeneration,
        7U);
    QCOMPARE(
        decoded.frame->identity().decoderRevision,
        11U);
    QCOMPARE(
        decoded.frame->geometry().codedSize,
        QSize(4, 4));
    QCOMPARE(
        decoded.frame->storage().kind,
        VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(
        decoded.frame->storage().softwareFormat,
        QStringLiteral("rgb24"));

    std::unique_ptr<GraphicsDeviceDomain> graphics =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create D3D11 graphics domain");
    QRhi &rhi = graphics->rhi();
    DecodedVideoSource source(
        decoded.frame, VideoTargetReadback::Enabled);
    source.setFrame(secondDecode.frame);
    QCOMPARE(source.contentRevision(), 2U);
    LibplaceboDecodedVideoProducer producer(
        *graphics, source, VideoTargetReadback::Enabled);
    RenderedVideoSurfaceState state =
        surfaceState(*graphics, source.contentRevision());
    QCOMPARE(
        producer.ensureSurface(state),
        VideoOperationResult::Ready);

    std::unique_ptr<QRhiTexture> uiTexture(rhi.newTexture(
        QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(uiTexture->create());
    std::unique_ptr<QRhiTexture> compositionTexture(
        rhi.newTexture(
            QRhiTexture::RGBA16F,
            {4, 4},
            1,
            QRhiTexture::RenderTarget
                | QRhiTexture::UsedAsTransferSource));
    QVERIFY(compositionTexture->create());
    const QRhiTextureRenderTargetDescription compositionDescription(
        QRhiColorAttachment(compositionTexture.get()));
    std::unique_ptr<QRhiTextureRenderTarget> compositionTarget(
        rhi.newTextureRenderTarget(compositionDescription));
    std::unique_ptr<QRhiRenderPassDescriptor> compositionPass(
        compositionTarget->newCompatibleRenderPassDescriptor());
    compositionTarget->setRenderPassDescriptor(
        compositionPass.get());
    QVERIFY(compositionTarget->create());

    HdrCompositor compositor(rhi);
    QCOMPARE(
        compositor.initialize(
            *compositionPass,
            &producer.textureForComposition(),
            *uiTexture),
        HdrCompositor::ResourceResult::Ready);

    const auto capture = [&](
            const RenderedVideoSurfaceState &requested,
            QRhiReadbackResult &surfaceReadback,
            QRhiReadbackResult &compositionReadback) {
        QRhiCommandBuffer *commandBuffer = nullptr;
        if (rhi.beginOffscreenFrame(&commandBuffer)
                != QRhi::FrameOpSuccess
                || !commandBuffer) {
            return false;
        }

        const QByteArray transparentUi(4, '\0');
        QRhiResourceUpdateBatch *updates =
            rhi.nextResourceUpdateBatch();
        updates->uploadTexture(
            uiTexture.get(),
            QRhiTextureUploadDescription(
                QRhiTextureUploadEntry(
                    0,
                    0,
                    QRhiTextureSubresourceUploadDescription(
                        transparentUi))));
        commandBuffer->resourceUpdate(updates);

        if (producer.render(*commandBuffer, requested)
                != VideoOperationResult::Ready
                || producer.prepareForComposition(*commandBuffer)
                    != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            producer.submissionAborted();
            producer.discardPendingRender();
            return false;
        }

        HdrCompositorParameters parameters;
        parameters.viewportSize = {4.0f, 4.0f};
        parameters.videoOrigin = {0.0f, 0.0f};
        parameters.videoSize = {4.0f, 4.0f};
        parameters.sdrScale = 1.0f;
        parameters.ndcYUp =
            rhi.isYUpInNDC() ? 1.0f : 0.0f;
        parameters.linearOutput = 1.0f;
        compositor.render(
            *commandBuffer,
            *compositionTarget,
            {4, 4},
            parameters);

        bool surfaceCompleted = false;
        bool compositionCompleted = false;
        surfaceReadback.completed = [&surfaceCompleted] {
            surfaceCompleted = true;
        };
        compositionReadback.completed =
            [&compositionCompleted] {
                compositionCompleted = true;
            };
        updates = rhi.nextResourceUpdateBatch();
        updates->readBackTexture(
            QRhiReadbackDescription(
                &producer.textureForComposition()),
            &surfaceReadback);
        updates->readBackTexture(
            QRhiReadbackDescription(
                compositionTexture.get()),
            &compositionReadback);
        commandBuffer->resourceUpdate(updates);

        const QRhi::FrameOpResult frameResult =
            rhi.endOffscreenFrame();
        if (frameResult == QRhi::FrameOpSuccess) {
            producer.submissionAccepted();
            producer.commitPendingRender();
        } else {
            producer.submissionAborted();
            producer.discardPendingRender();
        }
        return frameResult == QRhi::FrameOpSuccess
            && surfaceCompleted
            && compositionCompleted;
    };

    QRhiReadbackResult surfaceReadback;
    QRhiReadbackResult compositionReadback;
    QVERIFY(capture(
        state, surfaceReadback, compositionReadback));
    QCOMPARE(surfaceReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(surfaceReadback.pixelSize, QSize(4, 4));
    QCOMPARE(producer.inputImportCount(), 1U);

    const FloatPixel red = pixel(surfaceReadback, 0, 0);
    QVERIFY(red.red > 0.95f);
    QVERIFY(red.green < 0.02f);
    QVERIFY(red.blue < 0.02f);
    compareNear(red.alpha, 1.0f, 0.002f);
    const FloatPixel green = pixel(surfaceReadback, 1, 0);
    QVERIFY(green.red < 0.02f);
    QVERIFY(green.green > 0.95f);
    QVERIFY(green.blue < 0.02f);
    const FloatPixel gray = pixel(surfaceReadback, 1, 1);
    QVERIFY(gray.red > 0.15f && gray.red < 0.30f);
    compareNear(gray.red, gray.green, 0.01f);
    compareNear(gray.green, gray.blue, 0.01f);

    const FloatPixel composed =
        pixel(compositionReadback, 0, 0);
    compareNear(composed.red, red.red, 0.01f);
    compareNear(composed.green, red.green, 0.01f);
    compareNear(composed.blue, red.blue, 0.01f);

    const VideoFrameImportDiagnostics &input =
        producer.frameImportDiagnostics();
    QVERIFY(input.isValid());
    QCOMPARE(
        input.path,
        VideoFrameImportPath::SoftwareUpload);
    QCOMPARE(input.knownCpuDownloadsPerFrame, 0U);
    QCOMPARE(input.knownCpuUploadsPerFrame, 1U);
    QCOMPARE(input.knownGpuCopiesPerFrame, 0U);
    const RenderedVideoProducerDiagnostics diagnostics =
        producer.diagnostics();
    QVERIFY(diagnostics.isValid());
    QCOMPARE(
        diagnostics.target.outputPath,
        VideoOutputPath::DirectRenderTarget);
    QCOMPARE(
        diagnostics.target.knownOutputGpuCopiesPerRender,
        0U);
    QCOMPARE(
        diagnostics.target.knownOutputCpuTransfersPerRender,
        0U);

    RenderedVideoSurfaceState changedTarget = state;
    changedTarget.description.referenceWhiteNits = 100.0f;
    QCOMPARE(
        producer.ensureSurface(changedTarget),
        VideoOperationResult::Ready);
    QVERIFY(producer.needsRender(changedTarget));
    QRhiReadbackResult changedSurface;
    QRhiReadbackResult changedComposition;
    QVERIFY(capture(
        changedTarget,
        changedSurface,
        changedComposition));
    QCOMPARE(
        producer.inputImportCount(),
        1U);
    const FloatPixel changedRed =
        pixel(changedSurface, 0, 0);
    compareNear(changedRed.red, red.red, 0.02f);
    compareNear(changedRed.green, red.green, 0.02f);
    compareNear(changedRed.blue, red.blue, 0.02f);
#endif
}

void FfmpegFirstFrameTest::
compressedYuvMetadataAndRendering() {
#ifndef Q_OS_WIN
    QSKIP("The current libplacebo integration is Windows D3D11");
#else
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1.mkv");
    const QString manifest = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1.toml");
    const QByteArray declaredHash =
        expectedFixtureHash(manifest);
    QVERIFY2(
        !declaredHash.isEmpty(),
        "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    std::unique_ptr<GraphicsDeviceDomain> graphics =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create D3D11 graphics domain");
    const FfmpegFirstFrameResult decoded =
        decodeFirstVideoFrame(
            fixture,
            {
                .playbackGeneration = 13,
                .decoderRevision = 1,
                .frameId = 1,
            },
            graphics->videoDecodeCapability());
    QVERIFY2(decoded.isSuccess(), qPrintable(decoded.error));
    QVERIFY(decoded.diagnostics.containerFormat.contains(
        QStringLiteral("matroska")));
    QCOMPARE(
        decoded.diagnostics.decoderName,
        QStringLiteral("ffv1"));
    QVERIFY(!decoded.diagnostics.hardwareAccelerated);
    QCOMPARE(
        decoded.diagnostics.decodePath,
        QStringLiteral("Software"));
    QVERIFY2(
        !decoded.diagnostics.hardwareFallbackReason.isEmpty(),
        "Software fallback must explain why hardware was unavailable");

    const DecodedVideoFrame &frame = *decoded.frame;
    QCOMPARE(frame.geometry().codedSize, QSize(96, 64));
    QCOMPARE(frame.geometry().visibleSize, QSize(96, 64));
    QVERIFY(frame.geometry().sampleAspectRatioKnown);
    QCOMPARE(
        frame.geometry().sampleAspectRatio.numerator, 32);
    QCOMPARE(
        frame.geometry().sampleAspectRatio.denominator, 27);
    QCOMPARE(
        frame.storage().kind,
        VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(
        frame.storage().softwareFormat,
        QStringLiteral("yuv420p"));
    QCOMPARE(frame.signal().componentDepth, 8);
    QCOMPARE(
        frame.signal().colorPrimaries,
        QStringLiteral("bt709"));
    QCOMPARE(
        frame.signal().transferFunction,
        QStringLiteral("bt709"));
    QCOMPARE(
        frame.signal().matrixCoefficients,
        QStringLiteral("bt709"));
    QCOMPARE(
        frame.signal().colorRange,
        QStringLiteral("tv"));
    QCOMPARE(
        frame.signal().chromaLocation,
        QStringLiteral("left"));
    QVERIFY(frame.timing().pts);
    QCOMPARE(*frame.timing().pts, 0);
    QVERIFY(frame.timing().duration);
    QCOMPARE(*frame.timing().duration, 250);
    QCOMPARE(frame.timing().timeBase.numerator, 1);
    QCOMPARE(frame.timing().timeBase.denominator, 1000);

    struct YuvSample {
        int x;
        int y;
        std::uint8_t luma;
        std::uint8_t chromaBlue;
        std::uint8_t chromaRed;
    };
    constexpr std::array samples{
        YuvSample{16, 16, 16, 128, 128},
        YuvSample{48, 16, 126, 128, 128},
        YuvSample{80, 16, 235, 128, 128},
        YuvSample{16, 48, 63, 102, 240},
        YuvSample{48, 48, 173, 42, 26},
        YuvSample{80, 48, 32, 240, 118},
    };
    const AVFrame &avFrame = frame.ffmpegFrame();
    for (const YuvSample &sample : samples) {
        QCOMPARE(
            avFrame.data[0][
                sample.y * avFrame.linesize[0] + sample.x],
            sample.luma);
        QCOMPARE(
            avFrame.data[1][
                (sample.y / 2) * avFrame.linesize[1]
                + sample.x / 2],
            sample.chromaBlue);
        QCOMPARE(
            avFrame.data[2][
                (sample.y / 2) * avFrame.linesize[2]
                + sample.x / 2],
            sample.chromaRed);
    }

    QRhi &rhi = graphics->rhi();
    DecodedVideoSource source(
        decoded.frame, VideoTargetReadback::Enabled);
    LibplaceboDecodedVideoProducer producer(
        *graphics, source, VideoTargetReadback::Enabled);
    const RenderedVideoSurfaceState state =
        surfaceState(
            *graphics,
            source.contentRevision(),
            203.0f,
            {96, 64});
    QCOMPARE(
        producer.ensureSurface(state),
        VideoOperationResult::Ready);

    QRhiCommandBuffer *commandBuffer = nullptr;
    QCOMPARE(
        rhi.beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QVERIFY(commandBuffer);
    QCOMPARE(
        producer.render(*commandBuffer, state),
        VideoOperationResult::Ready);
    QCOMPARE(
        producer.prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);

    QRhiReadbackResult readback;
    bool readbackCompleted = false;
    readback.completed = [&readbackCompleted] {
        readbackCompleted = true;
    };
    QRhiResourceUpdateBatch *updates =
        rhi.nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(
            &producer.textureForComposition()),
        &readback);
    commandBuffer->resourceUpdate(updates);
    const QRhi::FrameOpResult frameResult =
        rhi.endOffscreenFrame();
    if (frameResult == QRhi::FrameOpSuccess) {
        producer.submissionAccepted();
        producer.commitPendingRender();
    } else {
        producer.submissionAborted();
        producer.discardPendingRender();
    }
    QCOMPARE(frameResult, QRhi::FrameOpSuccess);
    QVERIFY(readbackCompleted);
    QCOMPARE(readback.format, QRhiTexture::RGBA16F);
    QCOMPARE(readback.pixelSize, QSize(96, 64));
    QVERIFY(
        readback.data.size()
        >= readback.pixelSize.width()
               * readback.pixelSize.height()
               * 4 * qsizetype(sizeof(qfloat16)));

    struct ExpectedRgb {
        int x;
        int y;
        float red;
        float green;
        float blue;
    };
    constexpr std::array expected{
        ExpectedRgb{16, 16, 0.0f, 0.0f, 0.0f},
        ExpectedRgb{48, 16, 0.19165f, 0.19165f, 0.19165f},
        ExpectedRgb{80, 16, 1.0f, 1.0f, 1.0f},
        ExpectedRgb{16, 48, 1.0f, 0.00051f, 0.0f},
        ExpectedRgb{48, 48, 0.0f, 1.0f, 0.00099f},
        ExpectedRgb{80, 48, 0.00061f, 0.00007f, 1.0f},
    };
    for (const ExpectedRgb &sample : expected) {
        const FloatPixel actual =
            pixel(readback, sample.x, sample.y);
        compareNear(actual.red, sample.red, 0.02f);
        compareNear(actual.green, sample.green, 0.02f);
        compareNear(actual.blue, sample.blue, 0.02f);
        compareNear(actual.alpha, 1.0f, 0.002f);
    }
    QCOMPARE(producer.inputImportCount(), 1U);
    QCOMPARE(
        producer.frameImportDiagnostics().path,
        VideoFrameImportPath::SoftwareUpload);
#endif
}

void FfmpegFirstFrameTest::
continuousDecodeDrainsEveryFrame() {
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1.mkv");
    std::vector<
        std::shared_ptr<const DecodedVideoFrame>> frames;
    const FfmpegVideoDecodeResult result =
        decodeVideoFrames(
            {
                .path = fixture,
                .firstFrameIdentity = {
                    .playbackGeneration = 17,
                    .decoderRevision = 3,
                    .frameId = 40,
                },
            },
            [&frames](
                    std::shared_ptr<
                        const DecodedVideoFrame> frame,
                    const FfmpegVideoStreamDiagnostics &) {
                frames.push_back(std::move(frame));
                return true;
            });

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(result.endOfStream);
    QVERIFY(!result.stopped);
    QCOMPARE(result.framesDecoded, 3U);
    QCOMPARE(frames.size(), 3U);
    QCOMPARE(frames[0]->identity().frameId, 40U);
    QCOMPARE(frames[1]->identity().frameId, 41U);
    QCOMPARE(frames[2]->identity().frameId, 42U);
    QCOMPARE(
        frames[0]->timing().ptsMicroseconds(),
        std::optional<std::int64_t>(0));
    QCOMPARE(
        frames[1]->timing().ptsMicroseconds(),
        std::optional<std::int64_t>(250'000));
    QCOMPARE(
        frames[2]->timing().ptsMicroseconds(),
        std::optional<std::int64_t>(500'000));
    QCOMPARE(
        result.diagnostics
            .nominalFrameDurationMicroseconds,
        std::optional<std::int64_t>(250'000));
}

void FfmpegFirstFrameTest::
seekDecodesInterFramePreroll() {
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-h264-seek.mkv");
    const QString manifest = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-h264-seek.toml");
    const QByteArray declaredHash =
        expectedFixtureHash(manifest);
    QVERIFY2(
        !declaredHash.isEmpty(),
        "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    const FfmpegFirstFrameResult initial =
        decodeFirstVideoFrame(
            fixture,
            {
                .playbackGeneration = 18,
                .decoderRevision = 1,
                .frameId = 1,
            });
    QVERIFY2(initial.isSuccess(), qPrintable(initial.error));
    QVERIFY(initial.diagnostics.seekable);
    QVERIFY(initial.diagnostics.timelineOrigin);

    std::vector<std::int64_t> decodedPts;
    std::optional<VideoSignalDescription> decodedSignal;
    const FfmpegVideoDecodeResult sought =
        decodeVideoFrames(
            {
                .path = fixture,
                .firstFrameIdentity = {
                    .playbackGeneration = 19,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
                .start = {
                    .targetPositionMicroseconds = 3'250'000,
                    .timelineOrigin =
                        initial.diagnostics.timelineOrigin,
                    .performDemuxSeek = true,
                },
            },
            [&decodedPts, &decodedSignal](
                    std::shared_ptr<
                        const DecodedVideoFrame> frame,
                    const FfmpegVideoStreamDiagnostics &) {
                const auto pts =
                    frame->timing().ptsMicroseconds();
                if (!pts)
                    return false;
                if (!decodedSignal)
                    decodedSignal = frame->signal();
                decodedPts.push_back(*pts);
                return *pts < 3'250'000;
            });

    QVERIFY2(sought.isSuccess(), qPrintable(sought.error));
    QVERIFY(sought.stopped);
    QCOMPARE(decodedPts.size(), 6U);
    QCOMPARE(decodedPts.front(), 2'000'000);
    QCOMPARE(decodedPts.back(), 3'250'000);
    QVERIFY(std::is_sorted(
        decodedPts.cbegin(), decodedPts.cend()));
    QVERIFY(decodedPts.front() < 3'250'000);
    QVERIFY(decodedSignal);
    QCOMPARE(
        decodedSignal->colorPrimaries,
        QStringLiteral("bt709"));
    QCOMPARE(
        decodedSignal->transferFunction,
        QStringLiteral("bt709"));
    QCOMPARE(
        decodedSignal->matrixCoefficients,
        QStringLiteral("bt709"));
}

void FfmpegFirstFrameTest::
longTimelineSeekUses64BitTarget() {
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-long-timeline.mkv");
    const QString manifest = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-long-timeline.toml");
    const QByteArray declaredHash =
        expectedFixtureHash(manifest);
    QVERIFY2(
        !declaredHash.isEmpty(),
        "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    const FfmpegFirstFrameResult initial =
        decodeFirstVideoFrame(
            fixture,
            {
                .playbackGeneration = 20,
                .decoderRevision = 1,
                .frameId = 1,
            });
    QVERIFY2(initial.isSuccess(), qPrintable(initial.error));
    QVERIFY(initial.diagnostics.seekable);
    QVERIFY(initial.diagnostics.timelineOrigin);
    QCOMPARE(
        initial.diagnostics.durationMicroseconds,
        std::optional<std::int64_t>(
            3'001'000'000LL));

    std::optional<std::int64_t> firstSoughtPts;
    const FfmpegVideoDecodeResult sought =
        decodeVideoFrames(
            {
                .path = fixture,
                .firstFrameIdentity = {
                    .playbackGeneration = 21,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
                .start = {
                    .targetPositionMicroseconds =
                        3'000'000'000LL,
                    .timelineOrigin =
                        initial.diagnostics.timelineOrigin,
                    .performDemuxSeek = true,
                },
            },
            [&firstSoughtPts](
                    std::shared_ptr<
                        const DecodedVideoFrame> frame,
                    const FfmpegVideoStreamDiagnostics &) {
                firstSoughtPts =
                    frame->timing().ptsMicroseconds();
                return false;
            });

    QVERIFY2(sought.isSuccess(), qPrintable(sought.error));
    QVERIFY(sought.stopped);
    QCOMPARE(sought.framesDecoded, 1U);
    QCOMPARE(
        firstSoughtPts,
        std::optional<std::int64_t>(
            3'000'000'000LL));
}

void FfmpegFirstFrameTest::
hardwareDecodeFailureRetriesSoftware() {
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1.mkv");
    const VideoFrameIdentity identity{
        .playbackGeneration = 19,
        .decoderRevision = 1,
        .frameId = 1,
    };
    int attempts = 0;
    bool receivedSoftwareFallback = false;
    const FfmpegVideoDecodeResult result =
        decodeVideoFramesWithFallback(
            {
                .device = {},
                .unavailableReason = {},
            },
            [&](const VideoHardwareDecodeCapability &capability,
                bool &hardwareSelected) {
                ++attempts;
                if (attempts == 1) {
                    hardwareSelected = true;
                    FfmpegVideoDecodeResult failure;
                    failure.error =
                        QStringLiteral(
                            "Injected post-selection hardware failure");
                    return failure;
                }
                hardwareSelected = false;
                receivedSoftwareFallback =
                    !capability.isAvailable()
                    && capability.unavailableReason.contains(
                        QStringLiteral(
                            "Injected post-selection hardware failure"));
                return decodeVideoFrames(
                    {
                        .path = fixture,
                        .firstFrameIdentity = identity,
                        .hardwareDecode = capability,
                        .extraHardwareFrames = 2,
                    },
                    [](
                            std::shared_ptr<
                                const DecodedVideoFrame>,
                            const FfmpegVideoStreamDiagnostics &) {
                        return false;
                    });
            });

    QCOMPARE(attempts, 2);
    QVERIFY(receivedSoftwareFallback);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!result.diagnostics.hardwareAccelerated);
    QCOMPARE(
        result.diagnostics.decodePath,
        QStringLiteral("Software"));
    QVERIFY(
        result.diagnostics.hardwareFallbackReason.contains(
            QStringLiteral(
                "Injected post-selection hardware failure")));
}

void FfmpegFirstFrameTest::
continuousD3d11DecodeRetainsBoundedFrames() {
#ifndef Q_OS_WIN
    QSKIP("D3D11VA decoding is Windows-specific");
#else
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-h264.mkv");
    std::unique_ptr<GraphicsDeviceDomain> graphics =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create D3D11 graphics domain");
    const VideoHardwareDecodeCapability capability =
        graphics->videoDecodeCapability();
    if (!capability.isAvailable()) {
        if (qEnvironmentVariableIntValue(
                "SUNROOM_REQUIRE_D3D11VA") != 0) {
            QFAIL(qPrintable(
                QStringLiteral(
                    "D3D11VA is required by this test run: %1")
                    .arg(capability.unavailableReason)));
        }
        QSKIP(qPrintable(capability.unavailableReason));
    }

    std::vector<
        std::shared_ptr<const DecodedVideoFrame>> frames;
    const FfmpegVideoDecodeResult result =
        decodeVideoFrames(
            {
                .path = fixture,
                .firstFrameIdentity = {
                    .playbackGeneration = 29,
                    .decoderRevision = 5,
                    .frameId = 70,
                },
                .hardwareDecode = capability,
                .extraHardwareFrames =
                    static_cast<int>(
                        VideoFrameQueue::capacity + 2),
            },
            [&frames](
                    std::shared_ptr<
                        const DecodedVideoFrame> frame,
                    const FfmpegVideoStreamDiagnostics &) {
                frames.push_back(std::move(frame));
                return true;
            });

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(result.endOfStream);
    QCOMPARE(result.framesDecoded, 3U);
    QCOMPARE(frames.size(), VideoFrameQueue::capacity);
    QVERIFY(result.diagnostics.hardwareAccelerated);
    QCOMPARE(
        result.diagnostics.decodePath,
        QStringLiteral("D3D11VA"));
    for (std::size_t index = 0;
            index < frames.size();
            ++index) {
        QVERIFY(frames[index]->storage().isHardware());
        QCOMPARE(
            frames[index]->identity().frameId,
            70U + index);
        QCOMPARE(
            frames[index]
                ->storage()
                .graphicsDeviceGeneration,
            std::optional<std::uint64_t>(
                graphics->generation()));
    }
#endif
}

void FfmpegFirstFrameTest::
d3d11HardwareDecodeDirectImport() {
#ifndef Q_OS_WIN
    QSKIP("D3D11VA direct import is Windows-specific");
#else
    const QString fixture = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-h264.mkv");
    const QString manifest = QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-h264.toml");
    const QByteArray declaredHash =
        expectedFixtureHash(manifest);
    QVERIFY2(
        !declaredHash.isEmpty(),
        "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    std::unique_ptr<GraphicsDeviceDomain> graphics =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create D3D11 graphics domain");
    const VideoHardwareDecodeCapability &hardwareDecode =
        graphics->videoDecodeCapability();
    if (!hardwareDecode.isAvailable()) {
        const QString reason = QStringLiteral(
            "D3D11VA unavailable: %1")
            .arg(hardwareDecode.unavailableReason);
        if (qEnvironmentVariableIntValue(
                "SUNROOM_REQUIRE_D3D11VA") != 0) {
            QFAIL(qPrintable(reason));
        }
        QSKIP(qPrintable(reason));
    }

    const FfmpegFirstFrameResult hardware =
        decodeFirstVideoFrame(
            fixture,
            {
                .playbackGeneration = 21,
                .decoderRevision = 1,
                .frameId = 1,
            },
            hardwareDecode);
    QVERIFY2(
        hardware.isSuccess(),
        qPrintable(hardware.error));
    QVERIFY(hardware.diagnostics.hardwareAccelerated);
    QCOMPARE(
        hardware.diagnostics.decodePath,
        QStringLiteral("D3D11VA"));
    QVERIFY(
        hardware.diagnostics.hardwareFallbackReason.isEmpty());
    QVERIFY(
        hardware.diagnostics.containerFormat.contains(
            QStringLiteral("matroska")));
    QCOMPARE(
        hardware.diagnostics.decoderName,
        QStringLiteral("h264"));
    QCOMPARE(
        hardware.frame->storage().kind,
        VideoFrameStorageKind::D3D11Surface);
    QCOMPARE(
        hardware.frame->storage().hardwareFormat,
        QStringLiteral("d3d11"));
    QCOMPARE(
        hardware.frame->storage().softwareFormat,
        QStringLiteral("nv12"));
    QCOMPARE(
        hardware.frame->storage().graphicsDeviceGeneration,
        std::optional<std::uint64_t>(graphics->generation()));
    QCOMPARE(
        hardware.frame->geometry().visibleSize,
        QSize(640, 360));
    QVERIFY(
        hardware.frame->geometry().sampleAspectRatioKnown);
    QCOMPARE(
        hardware.frame->geometry().sampleAspectRatio.numerator,
        1);
    QCOMPARE(
        hardware.frame->geometry().sampleAspectRatio.denominator,
        1);
    QVERIFY(hardware.frame->timing().pts);
    QCOMPARE(*hardware.frame->timing().pts, 0);
    QVERIFY(hardware.frame->timing().duration);
    QCOMPARE(*hardware.frame->timing().duration, 250);
    QCOMPARE(
        hardware.frame->timing().timeBase.numerator,
        1);
    QCOMPARE(
        hardware.frame->timing().timeBase.denominator,
        1000);
    QCOMPARE(
        hardware.frame->timing().timeBase.denominator
            / *hardware.frame->timing().duration,
        4);
    QCOMPARE(hardware.frame->signal().componentDepth, 8);
    QCOMPARE(
        hardware.frame->signal().colorPrimaries,
        QStringLiteral("bt709"));
    QCOMPARE(
        hardware.frame->signal().transferFunction,
        QStringLiteral("bt709"));
    QCOMPARE(
        hardware.frame->signal().matrixCoefficients,
        QStringLiteral("bt709"));
    QCOMPARE(
        hardware.frame->signal().colorRange,
        QStringLiteral("tv"));
    QCOMPARE(
        hardware.frame->signal().chromaLocation,
        QStringLiteral("left"));

    const FfmpegFirstFrameResult software =
        decodeFirstVideoFrame(
            fixture,
            {
                .playbackGeneration = 22,
                .decoderRevision = 1,
                .frameId = 1,
            });
    QVERIFY2(
        software.isSuccess(),
        qPrintable(software.error));
    QVERIFY(!software.diagnostics.hardwareAccelerated);
    QCOMPARE(
        software.diagnostics.decodePath,
        QStringLiteral("Software"));
    QCOMPARE(
        software.frame->storage().kind,
        VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(
        software.frame->storage().softwareFormat,
        QStringLiteral("yuv420p"));

    DecodedFrameCapture hardwareCapture =
        captureDecodedFrame(
            *graphics, hardware.frame);
    QVERIFY2(
        hardwareCapture.isSuccess(),
        qPrintable(hardwareCapture.error));

    const auto freeFrame = [](AVFrame *frame) {
        av_frame_free(&frame);
    };
    std::unique_ptr<AVFrame, decltype(freeFrame)> malformedSource(
        av_frame_clone(&hardware.frame->ffmpegFrame()),
        freeFrame);
    QVERIFY(malformedSource);
    av_frame_remove_side_data(
        malformedSource.get(), AV_FRAME_DATA_DOVI_METADATA);
    AVFrameSideData *truncatedDovi = av_frame_new_side_data(
        malformedSource.get(), AV_FRAME_DATA_DOVI_METADATA, 1);
    QVERIFY(truncatedDovi);
    truncatedDovi->data[0] = 0;

    QString malformedError;
    const auto malformedFrame = DecodedVideoFrame::clone(
        *malformedSource,
        {
            .playbackGeneration = 23,
            .decoderRevision = 1,
            .frameId = 1,
        },
        hardware.frame->timing().timeBase,
        graphics->generation(),
        &malformedError);
    QVERIFY2(malformedFrame, qPrintable(malformedError));
    const DecodedFrameCapture malformedCapture =
        captureDecodedFrame(*graphics, malformedFrame);
    QVERIFY(!malformedCapture.isSuccess());
    QCOMPARE(
        malformedCapture.input.failure,
        VideoFrameImportFailure::General);
    QCOMPARE(
        malformedCapture.producer.failureKind,
        VideoFailureKind::General);
    QVERIFY(malformedCapture.error.contains(
        QStringLiteral("Dolby Vision metadata is truncated")));

    DecodedFrameCapture softwareCapture =
        captureDecodedFrame(
            *graphics, software.frame);
    QVERIFY2(
        softwareCapture.isSuccess(),
        qPrintable(softwareCapture.error));
    QCOMPARE(
        hardwareCapture.readback.format,
        QRhiTexture::RGBA16F);
    QCOMPARE(
        hardwareCapture.readback.pixelSize,
        QSize(640, 360));
    QCOMPARE(
        softwareCapture.readback.pixelSize,
        hardwareCapture.readback.pixelSize);

    QCOMPARE(
        hardwareCapture.input.path,
        VideoFrameImportPath::DirectHardwareSurface);
    QCOMPARE(
        hardwareCapture.input.knownCpuDownloadsPerFrame,
        0U);
    QCOMPARE(
        hardwareCapture.input.knownCpuUploadsPerFrame,
        0U);
    QCOMPARE(
        hardwareCapture.input.knownGpuCopiesPerFrame,
        0U);
    QVERIFY(
        hardwareCapture.input.nativeResource.contains(
            QStringLiteral("NV12")));
    QVERIFY(
        hardwareCapture.producer.inputPath.contains(
            QStringLiteral("direct libplacebo import")));
    QCOMPARE(
        hardwareCapture.producer
            .knownInputCpuTransfersPerInputFrame,
        0U);
    QCOMPARE(
        hardwareCapture.producer
            .knownInputGpuCopiesPerInputFrame,
        0U);
    QCOMPARE(
        hardwareCapture.producer.target.outputPath,
        VideoOutputPath::DirectRenderTarget);
    QCOMPARE(
        hardwareCapture.producer.target
            .knownOutputGpuCopiesPerRender,
        0U);
    QCOMPARE(
        hardwareCapture.producer.target
            .knownOutputCpuTransfersPerRender,
        0U);
    QVERIFY(
        !hardwareCapture.producer.target
            .synchronizationMode.isEmpty());

    std::unique_ptr<GraphicsDeviceDomain>
        incompatibleGraphics =
            GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(
        incompatibleGraphics,
        "Could not create second D3D11 graphics domain");
    const DecodedFrameCapture incompatibleCapture =
        captureDecodedFrame(
            *incompatibleGraphics, hardware.frame);
    QVERIFY(!incompatibleCapture.isSuccess());
    QCOMPARE(
        incompatibleCapture.input.path,
        VideoFrameImportPath::Unavailable);
    QCOMPARE(
        incompatibleCapture.producer.failureKind,
        VideoFailureKind::
            HardwareFrameImportUnavailable);
    QVERIFY(
        incompatibleCapture.error.contains(
            QStringLiteral(
                "stale graphics-device generation")));

    constexpr std::array samplePoints{
        QPoint{100, 90},
        QPoint{320, 90},
        QPoint{540, 90},
        QPoint{100, 270},
        QPoint{320, 270},
        QPoint{540, 270},
    };
    for (const QPoint &sample : samplePoints) {
        const FloatPixel hardwarePixel =
            pixel(
                hardwareCapture.readback,
                sample.x(),
                sample.y());
        const FloatPixel softwarePixel =
            pixel(
                softwareCapture.readback,
                sample.x(),
                sample.y());
        compareNear(
            hardwarePixel.red, softwarePixel.red, 0.03f);
        compareNear(
            hardwarePixel.green, softwarePixel.green, 0.03f);
        compareNear(
            hardwarePixel.blue, softwarePixel.blue, 0.03f);
        compareNear(hardwarePixel.alpha, 1.0f, 0.002f);
    }
#endif
}

// This target links Qt Gui for headless QRhi. QTEST_MAIN would create a
// QGuiApplication, while the offscreen test only needs QCoreApplication.
QTEST_GUILESS_MAIN(FfmpegFirstFrameTest)
#include "tst_FfmpegFirstFrame.moc"
