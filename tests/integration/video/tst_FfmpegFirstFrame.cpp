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
#include <libavutil/dovi_meta.h>
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/mastering_display_metadata.h>
#include <libplacebo/colorspace.h>
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
#include "video/VideoTargetInterop.h"

namespace {
struct FloatPixel {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
};

enum class HdrFixtureKind {
    StaticPq,
    Hlg,
    Hdr10Plus,
    DolbyVision,
};

FloatPixel pixel(QRhiReadbackResult const& readback, int x, int y) {
    Q_ASSERT(readback.format == QRhiTexture::RGBA16F);
    Q_ASSERT(x >= 0 && x < readback.pixelSize.width());
    Q_ASSERT(y >= 0 && y < readback.pixelSize.height());
    Q_ASSERT(readback.data.size() >=
             readback.pixelSize.width() * readback.pixelSize.height() * 4 * qsizetype(sizeof(qfloat16)));
    qsizetype const offset = (static_cast<qsizetype>(y) * readback.pixelSize.width() + x) * 4 * sizeof(qfloat16);
    std::array<qfloat16, 4> values;
    std::memcpy(values.data(), readback.data.constData() + offset, sizeof(values));
    return {
        static_cast<float>(values[0]),
        static_cast<float>(values[1]),
        static_cast<float>(values[2]),
        static_cast<float>(values[3]),
    };
}

QByteArray expectedFixtureHash(QString const& manifestPath) {
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) {
        return {};
    }
    QString const contents = QString::fromUtf8(manifest.readAll());
    QRegularExpressionMatch const match = QRegularExpression(QStringLiteral("^sha256\\s*=\\s*\"([0-9a-f]{64})\"\\s*$"),
                                                             QRegularExpression::MultilineOption)
                                              .match(contents);
    return match.hasMatch() ? match.captured(1).toLatin1() : QByteArray();
}

QByteArray fixtureHash(QString const& fixturePath) {
    QFile fixture(fixturePath);
    if (!fixture.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256).toHex();
}

void compareNear(float actual, float expected, float tolerance) {
    QVERIFY2(std::abs(actual - expected) <= tolerance,
             qPrintable(QStringLiteral("Expected %1 ± %2, got %3").arg(expected).arg(tolerance).arg(actual)));
}

void compareBorder(QRhiReadbackResult const& actual, QRhiReadbackResult const& expected, float tolerance,
                   int borderWidth = 4) {
    QCOMPARE(actual.format, QRhiTexture::RGBA16F);
    QCOMPARE(expected.format, actual.format);
    QCOMPARE(actual.pixelSize, expected.pixelSize);
    int const width = actual.pixelSize.width();
    int const height = actual.pixelSize.height();
    QVERIFY(width >= borderWidth * 2);
    QVERIFY(height >= borderWidth * 2);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x >= borderWidth && x < width - borderWidth && y >= borderWidth && y < height - borderWidth) {
                continue;
            }
            FloatPixel const actualPixel = pixel(actual, x, y);
            FloatPixel const expectedPixel = pixel(expected, x, y);
            std::array const channels{actualPixel.red, actualPixel.green, actualPixel.blue};
            std::array const expectedChannels{expectedPixel.red, expectedPixel.green, expectedPixel.blue};
            for (std::size_t channel = 0; channel < channels.size(); ++channel) {
                QVERIFY2(std::abs(channels[channel] - expectedChannels[channel]) <= tolerance,
                         qPrintable(QStringLiteral("Border mismatch at (%1, %2), channel %3: expected %4 ± %5, got %6")
                                        .arg(x)
                                        .arg(y)
                                        .arg(static_cast<int>(channel))
                                        .arg(expectedChannels[channel])
                                        .arg(tolerance)
                                        .arg(channels[channel])));
            }
            QVERIFY2(
                std::abs(actualPixel.alpha - 1.0f) <= 0.002f,
                qPrintable(
                    QStringLiteral("Border alpha mismatch at (%1, %2): got %3").arg(x).arg(y).arg(actualPixel.alpha)));
        }
    }
}

RenderedVideoSurfaceState surfaceState(GraphicsDeviceDomain& graphics, std::uint64_t contentRevision,
                                       float referenceWhiteNits = 203.0f, QSize pixelSize = {4, 4},
                                       float targetPeakHeadroom = 1.0f,
                                       std::optional<ColorPrimaries> targetPrimaries = std::nullopt,
                                       float targetMinimumLuminanceNits = 0.0f) {
    RenderedVideoSurfaceState state{
        .description =
            {
                .pixelSize = pixelSize,
                .pixelFormat = RenderedVideoPixelFormat::Rgba16Float,
                .colorSpace = RenderedVideoColorSpace::LinearSrgb,
                .luminance = RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative,
                .alphaMode = RenderedVideoAlphaMode::Opaque,
                .referenceWhiteNits = referenceWhiteNits,
                .targetMinimumLuminanceKnown = true,
                .targetMinimumLuminanceNits = targetMinimumLuminanceNits,
                .targetPeakHeadroom = targetPeakHeadroom,
            },
        .graphicsDeviceGeneration = graphics.generation(),
        .contentRevision = contentRevision,
    };
    if (targetPrimaries) {
        state.description.targetPrimariesKnown = true;
        state.description.targetPrimaries = *targetPrimaries;
    }
    return state;
}

struct DecodedFrameCapture {
    QString error;
    QRhiReadbackResult readback;
    QRhiReadbackResult compositionReadback;
    VideoFrameImportDiagnostics input;
    RenderedVideoProducerDiagnostics producer;

    bool isSuccess() const {
        return error.isEmpty() && input.isValid() && producer.isValid() && !readback.data.isEmpty();
    }
};

DecodedFrameCapture captureDecodedFrame(GraphicsDeviceDomain& graphics, std::shared_ptr<DecodedVideoFrame const> frame,
                                        float referenceWhiteNits = 203.0f, float targetPeakHeadroom = 1.0f,
                                        std::optional<ColorPrimaries> targetPrimaries = std::nullopt,
                                        float targetMinimumLuminanceNits = 0.0f,
                                        std::optional<float> compositionSdrScale = std::nullopt,
                                        QSize targetPixelSize = {}) {
    DecodedFrameCapture result;
    GraphicsDeviceExecutionScope execution = graphics.acquireExecutionScope();
    QRhi& rhi = graphics.rhi();
    DecodedVideoSource source(std::move(frame), VideoTargetReadback::Enabled);
    LibplaceboDecodedVideoProducer producer(graphics, source, VideoTargetReadback::Enabled);
    if (!targetPixelSize.isValid()) {
        targetPixelSize = source.currentFrame()->geometry().visibleSize;
    }
    RenderedVideoSurfaceState const state =
        surfaceState(graphics, source.contentRevision(), referenceWhiteNits, targetPixelSize, targetPeakHeadroom,
                     targetPrimaries, targetMinimumLuminanceNits);
    if (producer.ensureSurface(state) != VideoOperationResult::Ready) {
        result.error = producer.diagnostics().target.fallbackReason;
        return result;
    }

    std::unique_ptr<QRhiTexture> uiTexture;
    std::unique_ptr<QRhiTexture> subtitleTexture;
    std::unique_ptr<QRhiTexture> compositionTexture;
    std::unique_ptr<QRhiTextureRenderTarget> compositionTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> compositionPass;
    std::unique_ptr<HdrCompositor> compositor;
    if (compositionSdrScale) {
        uiTexture.reset(rhi.newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
        subtitleTexture.reset(rhi.newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
        compositionTexture.reset(rhi.newTexture(QRhiTexture::RGBA16F, state.description.pixelSize, 1,
                                                QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!uiTexture->create() || !subtitleTexture->create() || !compositionTexture->create()) {
            result.error = QStringLiteral("Could not create composition capture textures");
            return result;
        }
        QRhiTextureRenderTargetDescription const description(QRhiColorAttachment(compositionTexture.get()));
        compositionTarget.reset(rhi.newTextureRenderTarget(description));
        compositionPass.reset(compositionTarget->newCompatibleRenderPassDescriptor());
        compositionTarget->setRenderPassDescriptor(compositionPass.get());
        if (!compositionTarget->create()) {
            result.error = QStringLiteral("Could not create composition capture target");
            return result;
        }
        compositor = std::make_unique<HdrCompositor>(rhi);
        if (compositor->initialize(*compositionPass, &producer.textureForComposition(), subtitleTexture.get(),
                                   *uiTexture) != HdrCompositor::ResourceResult::Ready) {
            result.error = QStringLiteral("Could not initialize composition capture");
            return result;
        }
    }

    QRhiCommandBuffer* commandBuffer = nullptr;
    if (rhi.beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess || !commandBuffer) {
        result.error = QStringLiteral("Could not begin offscreen frame");
        return result;
    }

    auto const abortFrame = [&] {
        rhi.endOffscreenFrame(QRhi::SkipPresent);
        producer.submissionAborted();
        producer.discardPendingRender();
    };
    if (producer.render(*commandBuffer, state) != VideoOperationResult::Ready ||
        producer.prepareForComposition(*commandBuffer) != VideoOperationResult::Ready) {
        result.error = producer.diagnostics().target.fallbackReason;
        abortFrame();
        result.input = producer.frameImportDiagnostics();
        result.producer = producer.diagnostics();
        return result;
    }

    if (compositionSdrScale) {
        QByteArray const transparentPixel(4, '\0');
        QRhiResourceUpdateBatch* uploads = rhi.nextResourceUpdateBatch();
        uploads->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                    0, 0, QRhiTextureSubresourceUploadDescription(transparentPixel))));
        uploads->uploadTexture(subtitleTexture.get(),
                               QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                   0, 0, QRhiTextureSubresourceUploadDescription(transparentPixel))));
        commandBuffer->resourceUpdate(uploads);

        HdrCompositorParameters parameters;
        parameters.viewportSize = {static_cast<float>(state.description.pixelSize.width()),
                                   static_cast<float>(state.description.pixelSize.height())};
        parameters.videoOrigin = {0.0f, 0.0f};
        parameters.videoSize = parameters.viewportSize;
        parameters.sdrScale = *compositionSdrScale;
        parameters.ndcYUp = rhi.isYUpInNDC() ? 1.0f : 0.0f;
        parameters.outputEncoding = 2.0f;
        compositor->render(*commandBuffer, *compositionTarget, state.description.pixelSize, parameters);
    }

    bool readbackCompleted = false;
    bool compositionReadbackCompleted = !compositionSdrScale.has_value();
    result.readback.completed = [&readbackCompleted] { readbackCompleted = true; };
    result.compositionReadback.completed = [&compositionReadbackCompleted] { compositionReadbackCompleted = true; };
    QRhiResourceUpdateBatch* updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(&producer.textureForComposition()), &result.readback);
    if (compositionSdrScale) {
        updates->readBackTexture(QRhiReadbackDescription(compositionTexture.get()), &result.compositionReadback);
    }
    commandBuffer->resourceUpdate(updates);

    QRhi::FrameOpResult const frameResult = rhi.endOffscreenFrame();
    if (frameResult == QRhi::FrameOpSuccess) {
        producer.submissionAccepted();
        producer.commitPendingRender();
    } else {
        producer.submissionAborted();
        producer.discardPendingRender();
        result.error = QStringLiteral("Could not finish offscreen frame");
        return result;
    }
    if (!readbackCompleted || !compositionReadbackCompleted) {
        result.error = QStringLiteral("Decoded-frame capture readback did not complete");
        return result;
    }
    result.input = producer.frameImportDiagnostics();
    result.producer = producer.diagnostics();
    return result;
}

DecodedFrameCapture captureDecodedFrameAtSize(GraphicsDeviceDomain& graphics,
                                              std::shared_ptr<DecodedVideoFrame const> frame, QSize targetPixelSize) {
    return captureDecodedFrame(graphics, std::move(frame), 203.0f, 1.0f, std::nullopt, 0.0f, std::nullopt,
                               targetPixelSize);
}

std::array<FloatPixel, 4> neutralPatchPixels(QRhiReadbackResult const& readback) {
    return {
        pixel(readback, 32, 72),
        pixel(readback, 96, 72),
        pixel(readback, 160, 72),
        pixel(readback, 224, 72),
    };
}

void verifyNeutralPatchProperties(std::array<FloatPixel, 4> const& patches, float targetPeakHeadroom,
                                  float neutralChannelTolerance = 0.025f) {
    float previous = -1.0f;
    for (FloatPixel const& patch : patches) {
        QVERIFY(std::isfinite(patch.red));
        QVERIFY(std::isfinite(patch.green));
        QVERIFY(std::isfinite(patch.blue));
        QVERIFY(std::isfinite(patch.alpha));
        compareNear(patch.red, patch.green, neutralChannelTolerance);
        compareNear(patch.green, patch.blue, neutralChannelTolerance);
        compareNear(patch.alpha, 1.0f, 0.002f);
        QVERIFY2(
            patch.red > previous,
            qPrintable(QStringLiteral("Neutral patches are not increasing: %1 after %2").arg(patch.red).arg(previous)));
        QVERIFY2(
            patch.red <= targetPeakHeadroom * 1.005f,
            qPrintable(QStringLiteral("Patch %1 exceeds target headroom %2").arg(patch.red).arg(targetPeakHeadroom)));
        previous = patch.red;
    }
}

} // namespace

class FfmpegFirstFrameTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void retainsStreamHdr10PlusAtTheDecodeBoundary();
    void hdrInputAcceptance_data();
    void hdrInputAcceptance();
    void nominalSdrTargetPreservesWcg();
    void dualFormatTargetChangeRemapsPausedFrame();
    void realDemuxDecodeImportAndComposition();
    void compressedYuvMetadataAndRendering();
    void continuousDecodeDrainsEveryFrame();
    void seekDecodesInterFramePreroll();
    void longTimelineSeekUses64BitTarget();
    void hardwareDecodeFailureRetriesSoftware();
    void videoToolboxHardwareDecodeDirectImport_data();
    void videoToolboxHardwareDecodeDirectImport();
    void continuousVideoToolboxDecodeAndImport();
    void metalTargetResizesBeforeFirstSubmission();
    void continuousD3d11DecodeRetainsBoundedFrames();
    void d3d11HardwareDecodeSafeImport();
    void d3d11P010HardwareDecodeSafeImport();
};

void FfmpegFirstFrameTest::retainsStreamHdr10PlusAtTheDecodeBoundary() {
    AVCodec const* decoder = avcodec_find_decoder(AV_CODEC_ID_RAWVIDEO);
    QVERIFY(decoder);

    auto const freeParameters = [](AVCodecParameters* parameters) { avcodec_parameters_free(&parameters); };
    std::unique_ptr<AVCodecParameters, decltype(freeParameters)> parameters(avcodec_parameters_alloc(), freeParameters);
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

    AVPacketSideData* streamMetadata =
        av_packet_side_data_new(&parameters->coded_side_data, &parameters->nb_coded_side_data,
                                AV_PKT_DATA_DYNAMIC_HDR10_PLUS, sizeof(AVDynamicHDRPlus), 0);
    QVERIFY(streamMetadata);
    std::memset(streamMetadata->data, 0, streamMetadata->size);
    auto* hdr10Plus = reinterpret_cast<AVDynamicHDRPlus*>(streamMetadata->data);
    hdr10Plus->itu_t_t35_country_code = 0xb5;
    hdr10Plus->application_version = 0;
    hdr10Plus->num_windows = 1;
    hdr10Plus->params[0].maxscl[0] = {1, 1};
    hdr10Plus->params[0].maxscl[1] = {4, 5};
    hdr10Plus->params[0].maxscl[2] = {3, 5};
    hdr10Plus->params[0].average_maxrgb = {1, 2};

    AVPacketSideData* dolbyVisionConfiguration =
        av_packet_side_data_new(&parameters->coded_side_data, &parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF,
                                sizeof(AVDOVIDecoderConfigurationRecord), 0);
    QVERIFY(dolbyVisionConfiguration);
    std::memset(dolbyVisionConfiguration->data, 0, dolbyVisionConfiguration->size);
    auto* dovi = reinterpret_cast<AVDOVIDecoderConfigurationRecord*>(dolbyVisionConfiguration->data);
    dovi->dv_version_major = 1;
    dovi->dv_profile = 8;
    dovi->rpu_present_flag = 1;
    dovi->bl_present_flag = 1;
    dovi->dv_bl_signal_compatibility_id = 1;

    std::array<FfmpegAvPacketPtr, 2> packets;
    for (std::size_t index = 0; index < packets.size(); ++index) {
        AVPacket* rawPacket = av_packet_alloc();
        QVERIFY(rawPacket);
        QVERIFY(av_new_packet(rawPacket, 2 * 2 * 3) >= 0);
        std::memset(rawPacket->data, 0x80, rawPacket->size);
        rawPacket->pts = static_cast<std::int64_t>(index);
        rawPacket->duration = 1;
        packets[index].reset(rawPacket);
    }

    auto* packetMetadata = reinterpret_cast<AVDynamicHDRPlus*>(
        av_packet_new_side_data(packets[0].get(), AV_PKT_DATA_DYNAMIC_HDR10_PLUS, sizeof(AVDynamicHDRPlus)));
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
    std::vector<std::shared_ptr<DecodedVideoFrame const>> decodedFrames;
    bool hardwareSelected = false;

    FfmpegVideoDecodeResult const result = decodeFfmpegVideoPackets(
        {
            .path = QStringLiteral("memory:hdr10plus"),
            .firstFrameIdentity =
                {
                    .playbackGeneration = 31,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
        },
        *decoder, *parameters, {1, 24}, {1, 1},
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
                read.terminal = FfmpegVideoPacketTerminal::EndOfStream;
            }
            return read;
        },
        [&decodedFrames](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const&) {
            decodedFrames.push_back(std::move(frame));
            return true;
        },
        {}, &hardwareSelected);

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!hardwareSelected);
    QCOMPARE(decodedFrames.size(), std::size_t(2));

    auto const retainedHdr10Plus = [&decodedFrames](std::size_t index) {
        AVFrameSideData const* retained =
            av_frame_get_side_data(&decodedFrames[index]->ffmpegFrame(), AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
        if (!retained || retained->size < sizeof(AVDynamicHDRPlus)) {
            return static_cast<AVDynamicHDRPlus const*>(nullptr);
        }
        return reinterpret_cast<AVDynamicHDRPlus const*>(retained->data);
    };

    AVDynamicHDRPlus const* first = retainedHdr10Plus(0);
    QVERIFY(first);
    QCOMPARE(decodedFrames[0]->dolbyVisionBaseIsHdr10Compatible(), std::optional<bool>(true));
    QCOMPARE(first->application_version, 0);
    QCOMPARE(first->num_windows, 1);
    QCOMPARE(first->params[0].average_maxrgb.num, 3);
    QCOMPARE(first->params[0].average_maxrgb.den, 4);

    AVDynamicHDRPlus const* second = retainedHdr10Plus(1);
    QVERIFY(second);
    QCOMPARE(decodedFrames[1]->dolbyVisionBaseIsHdr10Compatible(), std::optional<bool>(true));
    QCOMPARE(second->application_version, 0);
    QCOMPARE(second->num_windows, 1);
    QCOMPARE(second->params[0].average_maxrgb.num, 1);
    QCOMPARE(second->params[0].average_maxrgb.den, 2);

    dovi->dv_version_major = 2;
    FfmpegAvPacketPtr unsupportedVersionPacket(av_packet_alloc());
    QVERIFY(unsupportedVersionPacket);
    QVERIFY(av_new_packet(unsupportedVersionPacket.get(), 2 * 2 * 3) >= 0);
    std::memset(unsupportedVersionPacket->data, 0x80, unsupportedVersionPacket->size);
    unsupportedVersionPacket->pts = 0;
    unsupportedVersionPacket->duration = 1;
    bool packetAvailable = true;
    bool unsupportedVersionHardwareSelected = false;
    std::vector<std::shared_ptr<DecodedVideoFrame const>> unsupportedVersionFrames;
    FfmpegVideoDecodeResult const unsupportedVersionResult = decodeFfmpegVideoPackets(
        {
            .path = QStringLiteral("memory:unsupported-dovi-version"),
            .firstFrameIdentity =
                {
                    .playbackGeneration = 32,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
        },
        *decoder, *parameters, {1, 24}, {1, 1},
        {
            .containerFormat = QStringLiteral("rawvideo-test"),
            .decoderName = QString::fromLatin1(decoder->name),
            .decodePath = QStringLiteral("Software"),
            .videoStreamIndex = 0,
        },
        [&unsupportedVersionPacket, &packetAvailable](std::stop_token) {
            FfmpegVideoPacketRead read;
            if (packetAvailable) {
                packetAvailable = false;
                read.packet = std::move(unsupportedVersionPacket);
            } else {
                read.terminal = FfmpegVideoPacketTerminal::EndOfStream;
            }
            return read;
        },
        [&unsupportedVersionFrames](std::shared_ptr<DecodedVideoFrame const> frame,
                                    FfmpegVideoStreamDiagnostics const&) {
            unsupportedVersionFrames.push_back(std::move(frame));
            return true;
        },
        {}, &unsupportedVersionHardwareSelected);

    QVERIFY2(unsupportedVersionResult.isSuccess(), qPrintable(unsupportedVersionResult.error));
    QCOMPARE(unsupportedVersionFrames.size(), std::size_t(1));
    QCOMPARE(unsupportedVersionFrames.front()->dolbyVisionBaseIsHdr10Compatible(), std::optional<bool>());
}

void FfmpegFirstFrameTest::hdrInputAcceptance_data() {
    QTest::addColumn<QString>("fixtureStem");
    QTest::addColumn<QString>("expectedTransfer");
    QTest::addColumn<int>("formatKind");

    QTest::newRow("static-pq") << QStringLiteral("hdr10-pq-hevc") << QStringLiteral("smpte2084")
                               << static_cast<int>(HdrFixtureKind::StaticPq);
    QTest::newRow("hlg") << QStringLiteral("hlg-hevc") << QStringLiteral("arib-std-b67")
                         << static_cast<int>(HdrFixtureKind::Hlg);
    QTest::newRow("hdr10plus") << QStringLiteral("hdr10plus-hevc") << QStringLiteral("smpte2084")
                               << static_cast<int>(HdrFixtureKind::Hdr10Plus);
    QTest::newRow("dolby-vision-profile-8.1") << QStringLiteral("dovi-profile81-hevc") << QStringLiteral("smpte2084")
                                              << static_cast<int>(HdrFixtureKind::DolbyVision);
}

void FfmpegFirstFrameTest::hdrInputAcceptance() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QSKIP("This test requires a D3D11 or Metal graphics domain");
#else
    QFETCH(QString, fixtureStem);
    QFETCH(QString, expectedTransfer);
    QFETCH(int, formatKind);
    auto const kind = static_cast<HdrFixtureKind>(formatKind);

    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/%1.hevc").arg(fixtureStem);
    QString const manifest = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/%1.toml").arg(fixtureStem);
    QByteArray const declaredHash = expectedFixtureHash(manifest);
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    std::vector<std::shared_ptr<DecodedVideoFrame const>> frames;
    std::uint64_t const playbackGeneration = 40U + static_cast<std::uint64_t>(formatKind);
    FfmpegVideoDecodeResult const decoded = decodeVideoFrames(
        {
            .path = fixture,
            .firstFrameIdentity =
                {
                    .playbackGeneration = playbackGeneration,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
        },
        [&frames](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const&) {
            frames.push_back(std::move(frame));
            return true;
        });

    QVERIFY2(decoded.isSuccess(), qPrintable(decoded.error));
    QVERIFY(decoded.endOfStream);
    QVERIFY(!decoded.stopped);
    QCOMPARE(decoded.framesDecoded, 4U);
    QCOMPARE(frames.size(), std::size_t(4));
    QVERIFY(decoded.diagnostics.containerFormat.contains(QStringLiteral("hevc"), Qt::CaseInsensitive));
    QCOMPARE(decoded.diagnostics.decoderName, QStringLiteral("hevc"));
    QCOMPARE(decoded.diagnostics.decodePath, QStringLiteral("Software"));
    QVERIFY(!decoded.diagnostics.hardwareAccelerated);

    for (std::size_t index = 0; index < frames.size(); ++index) {
        DecodedVideoFrame const& frame = *frames[index];
        QCOMPARE(frame.identity().playbackGeneration, playbackGeneration);
        QCOMPARE(frame.identity().frameId, 1U + index);
        QCOMPARE(frame.geometry().visibleSize, QSize(256, 144));
        QCOMPARE(frame.storage().kind, VideoFrameStorageKind::SoftwarePlanes);
        QCOMPARE(frame.storage().softwareFormat, QStringLiteral("yuv420p10le"));
        QCOMPARE(frame.signal().componentDepth, 10);
        QCOMPARE(frame.signal().colorPrimaries, QStringLiteral("bt2020"));
        QCOMPARE(frame.signal().transferFunction, expectedTransfer);
        QCOMPARE(frame.signal().matrixCoefficients, QStringLiteral("bt2020nc"));
        QCOMPARE(frame.signal().colorRange, QStringLiteral("tv"));
        QCOMPARE(frame.signal().chromaLocation, QStringLiteral("left"));
        QCOMPARE(frame.timing().durationMicroseconds(), std::optional<std::int64_t>(250'000));
    }

    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the graphics domain");
    constexpr float referenceWhiteNits = 100.0f;
    constexpr float targetPeakHeadroom = 6.0f;
    DecodedFrameCapture const capture =
        captureDecodedFrame(*graphics, frames.front(), referenceWhiteNits, targetPeakHeadroom);
    QVERIFY2(capture.isSuccess(), qPrintable(capture.error));
    QCOMPARE(capture.readback.pixelSize, QSize(256, 144));
    QCOMPARE(capture.input.path, VideoFrameImportPath::SoftwareUpload);
    if (kind == HdrFixtureKind::Hdr10Plus) {
        QVERIFY(capture.producer.colorPolicy.contains(QStringLiteral("Spline tone map")));
        QVERIFY(capture.producer.colorPolicy.contains(QStringLiteral("HDR10+ scene maximum")));
        QVERIFY(capture.producer.colorPolicy.contains(
            QStringLiteral("source OOTF not applied on reference-white-adaptive HDR target")));
    } else if (kind == HdrFixtureKind::Hlg) {
        QCOMPARE(capture.producer.colorPolicy, QStringLiteral("Spline tone map · perceptual gamut map · "
                                                              "inverse mapping off · peak detection off · dither off"));
    } else {
        QVERIFY(capture.producer.colorPolicy.contains(QStringLiteral("Spline tone map")));
    }
    std::array<FloatPixel, 4> const patches = neutralPatchPixels(capture.readback);
    verifyNeutralPatchProperties(patches, targetPeakHeadroom, kind == HdrFixtureKind::DolbyVision ? 0.12f : 0.025f);

    DecodedFrameCapture const sdrCapture = captureDecodedFrame(*graphics, frames.front(), 203.0f, 1.0f);
    QVERIFY2(sdrCapture.isSuccess(), qPrintable(sdrCapture.error));
    std::array<FloatPixel, 4> const sdrPatches = neutralPatchPixels(sdrCapture.readback);
    verifyNeutralPatchProperties(sdrPatches, 1.0f, kind == HdrFixtureKind::DolbyVision ? 0.12f : 0.025f);

    auto const sideData = [](DecodedVideoFrame const& frame, AVFrameSideDataType type) {
        return av_frame_get_side_data(&frame.ffmpegFrame(), type);
    };

    if (kind == HdrFixtureKind::StaticPq) {
        QVERIFY(sdrCapture.producer.colorPolicy.contains(QStringLiteral("BT.2446A EETF")));
        QVERIFY(sdrCapture.producer.colorPolicy.contains(QStringLiteral("MaxCLL 1000 nits")));
        AVFrameSideData const* masteringSideData = sideData(*frames.front(), AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        QVERIFY(masteringSideData);
        QVERIFY(masteringSideData->size >= sizeof(AVMasteringDisplayMetadata));
        auto const* mastering = reinterpret_cast<AVMasteringDisplayMetadata const*>(masteringSideData->data);
        QVERIFY(mastering->has_luminance);
        compareNear(static_cast<float>(av_q2d(mastering->max_luminance)), 1000.0f, 0.01f);
        compareNear(static_cast<float>(av_q2d(mastering->min_luminance)), 0.005f, 0.0001f);

        AVFrameSideData const* lightSideData = sideData(*frames.front(), AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        QVERIFY(lightSideData);
        QVERIFY(lightSideData->size >= sizeof(AVContentLightMetadata));
        auto const* light = reinterpret_cast<AVContentLightMetadata const*>(lightSideData->data);
        QCOMPARE(light->MaxCLL, 1000U);
        QCOMPARE(light->MaxFALL, 400U);

        AVFrame* metadataLessSource = av_frame_clone(&frames.front()->ffmpegFrame());
        QVERIFY(metadataLessSource);
        av_frame_remove_side_data(metadataLessSource, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        av_frame_remove_side_data(metadataLessSource, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        QString metadataLessError;
        std::shared_ptr<DecodedVideoFrame const> const metadataLessFrame =
            DecodedVideoFrame::clone(*metadataLessSource,
                                     {
                                         .playbackGeneration = playbackGeneration + 100,
                                         .decoderRevision = 1,
                                         .frameId = 1,
                                     },
                                     frames.front()->timing().timeBase, std::nullopt, std::nullopt, &metadataLessError);
        av_frame_free(&metadataLessSource);
        QVERIFY2(metadataLessFrame, qPrintable(metadataLessError));

        DecodedFrameCapture const metadataLessCapture = captureDecodedFrame(*graphics, metadataLessFrame, 203.0f, 1.0f);
        QVERIFY2(metadataLessCapture.isSuccess(), qPrintable(metadataLessCapture.error));
        QVERIFY(metadataLessCapture.producer.colorPolicy.contains(QStringLiteral("Spline tone map")));
        QVERIFY(metadataLessCapture.producer.colorPolicy.contains(
            QStringLiteral("explicit PQ compatibility fallback 1000 nits")));
        std::array<FloatPixel, 4> const metadataLessPatches = neutralPatchPixels(metadataLessCapture.readback);
        verifyNeutralPatchProperties(metadataLessPatches, 1.0f);
        // The fixture's second neutral patch is 203 nits. Pinned spline maps
        // 1000 -> 100 nits to about 52.16 nits here; normalization therefore
        // stores about 0.522 in the white-relative surface.
        compareNear(metadataLessPatches[1].red, 0.522f, 0.02f);

        DecodedFrameCapture const metadataLessHdrCapture =
            captureDecodedFrame(*graphics, metadataLessFrame, referenceWhiteNits, targetPeakHeadroom);
        QVERIFY2(metadataLessHdrCapture.isSuccess(), qPrintable(metadataLessHdrCapture.error));
        QVERIFY(metadataLessHdrCapture.producer.colorPolicy.contains(QStringLiteral("Spline tone map")));
        QVERIFY(metadataLessHdrCapture.producer.colorPolicy.contains(
            QStringLiteral("explicit PQ compatibility fallback 1000 nits")));
        std::array<FloatPixel, 4> const metadataLessHdrPatches = neutralPatchPixels(metadataLessHdrCapture.readback);
        for (std::size_t index = 0; index < metadataLessPatches.size(); ++index) {
            compareNear(metadataLessHdrPatches[index].red, patches[index].red, 0.01f);
        }

        constexpr float firstReferenceWhiteNits = 160.0f;
        constexpr float secondReferenceWhiteNits = 240.0f;
#ifdef Q_OS_WIN
        constexpr float firstCompositionScale = firstReferenceWhiteNits / 80.0f;
        constexpr float secondCompositionScale = secondReferenceWhiteNits / 80.0f;
        constexpr float expectedCompositionRatio = secondReferenceWhiteNits / firstReferenceWhiteNits;
#else
        constexpr float firstCompositionScale = 1.0f;
        constexpr float secondCompositionScale = 1.0f;
        constexpr float expectedCompositionRatio = 1.0f;
#endif
        DecodedFrameCapture const firstWhiteCapture =
            captureDecodedFrame(*graphics, frames.front(), firstReferenceWhiteNits, targetPeakHeadroom, std::nullopt,
                                0.0f, firstCompositionScale);
        DecodedFrameCapture const secondWhiteCapture =
            captureDecodedFrame(*graphics, frames.front(), secondReferenceWhiteNits, targetPeakHeadroom, std::nullopt,
                                0.0f, secondCompositionScale);
        QVERIFY2(firstWhiteCapture.isSuccess(), qPrintable(firstWhiteCapture.error));
        QVERIFY2(secondWhiteCapture.isSuccess(), qPrintable(secondWhiteCapture.error));
        QVERIFY(!firstWhiteCapture.compositionReadback.data.isEmpty());
        QVERIFY(!secondWhiteCapture.compositionReadback.data.isEmpty());
        std::array<FloatPixel, 4> const firstWhiteSurface = neutralPatchPixels(firstWhiteCapture.readback);
        std::array<FloatPixel, 4> const secondWhiteSurface = neutralPatchPixels(secondWhiteCapture.readback);
        std::array<FloatPixel, 4> const firstWhiteComposition =
            neutralPatchPixels(firstWhiteCapture.compositionReadback);
        std::array<FloatPixel, 4> const secondWhiteComposition =
            neutralPatchPixels(secondWhiteCapture.compositionReadback);
        for (std::size_t index = 0; index < firstWhiteSurface.size(); ++index) {
            compareNear(secondWhiteSurface[index].red, firstWhiteSurface[index].red, 0.01f);
            compareNear(firstWhiteComposition[index].red, firstWhiteSurface[index].red * firstCompositionScale, 0.04f);
            compareNear(secondWhiteComposition[index].red, secondWhiteSurface[index].red * secondCompositionScale,
                        0.04f);
            compareNear(secondWhiteComposition[index].red, firstWhiteComposition[index].red * expectedCompositionRatio,
                        0.04f);
        }

    } else if (kind == HdrFixtureKind::Hlg) {
        QCOMPARE(sdrCapture.producer.colorPolicy,
                 QStringLiteral("Spline tone map · perceptual gamut map · "
                                "inverse mapping off · peak detection off · dither off"));
        for (auto const& frame : frames) {
            QVERIFY(!sideData(*frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS));
            QVERIFY(!sideData(*frame, AV_FRAME_DATA_DOVI_METADATA));
            QVERIFY(!sideData(*frame, AV_FRAME_DATA_DOVI_RPU_BUFFER));
        }

        constexpr float secondReferenceWhiteNits = 203.0f;
        constexpr float physicalPeakNits = 600.0f;
        constexpr float secondTargetHeadroom = physicalPeakNits / secondReferenceWhiteNits;
        DecodedFrameCapture const secondCapture =
            captureDecodedFrame(*graphics, frames.front(), secondReferenceWhiteNits, secondTargetHeadroom);
        QVERIFY2(secondCapture.isSuccess(), qPrintable(secondCapture.error));
        std::array<FloatPixel, 4> const secondPatches = neutralPatchPixels(secondCapture.readback);
        verifyNeutralPatchProperties(secondPatches, secondTargetHeadroom);

        float const firstMidtoneRatio = patches[1].red / patches.back().red;
        float const secondMidtoneRatio = secondPatches[1].red / secondPatches.back().red;
        QVERIFY2(std::abs(firstMidtoneRatio - secondMidtoneRatio) > 0.01f,
                 "HLG target change did not alter the captured OOTF response");
    } else if (kind == HdrFixtureKind::Hdr10Plus) {
        std::array<double, 4> averageMaxRgb{};
        for (std::size_t index = 0; index < frames.size(); ++index) {
            AVFrameSideData const* dynamic = sideData(*frames[index], AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
            QVERIFY(dynamic);
            QVERIFY(dynamic->size >= sizeof(AVDynamicHDRPlus));
            auto const* metadata = reinterpret_cast<AVDynamicHDRPlus const*>(dynamic->data);
            // FFmpeg's parsed T.35 helper starts after the country/provider
            // envelope, so decoder-produced frame metadata leaves this zero.
            QCOMPARE(metadata->itu_t_t35_country_code, std::uint8_t(0));
            QCOMPARE(metadata->application_version, 1);
            QCOMPARE(metadata->num_windows, 1);
            compareNear(static_cast<float>(av_q2d(metadata->targeted_system_display_maximum_luminance)), 600.0f, 0.01f);
            averageMaxRgb[index] = av_q2d(metadata->params[0].average_maxrgb);
        }
        compareNear(static_cast<float>(averageMaxRgb[0]), 0.10f, 0.0001f);
        compareNear(static_cast<float>(averageMaxRgb[1]), 0.10f, 0.0001f);
        compareNear(static_cast<float>(averageMaxRgb[2]), 0.05f, 0.0001f);
        compareNear(static_cast<float>(averageMaxRgb[3]), 0.05f, 0.0001f);
        QVERIFY(averageMaxRgb[1] > averageMaxRgb[2]);
        QVERIFY(capture.input.metadataPath.contains(QStringLiteral("HDR10+ scene-luminance subset available "
                                                                   "on mapped frame")));
        QVERIFY2(sdrCapture.producer.colorPolicy.contains(QStringLiteral("ST 2094-40 EETF")),
                 qPrintable(sdrCapture.producer.colorPolicy));
        QVERIFY(sdrCapture.producer.colorPolicy.contains(QStringLiteral("HDR10+ source OOTF")));
        // The pinned source OOTF maps the brightest fixture patch to about
        // 88.6 nominal nits. The fixed 203/100 conversion must therefore store
        // about 0.886, not leave it near 88.6/203.
        compareNear(sdrPatches.back().red, 0.886f, 0.025f);

        constexpr float secondReferenceWhiteNits = 200.0f;
        constexpr float secondTargetHeadroom = targetPeakHeadroom;
        DecodedFrameCapture const secondCapture =
            captureDecodedFrame(*graphics, frames.front(), secondReferenceWhiteNits, secondTargetHeadroom);
        QVERIFY2(secondCapture.isSuccess(), qPrintable(secondCapture.error));
        QVERIFY(secondCapture.producer.colorPolicy.contains(QStringLiteral("Spline tone map")));
        QVERIFY(secondCapture.producer.colorPolicy.contains(QStringLiteral("HDR10+ scene maximum")));
        std::array<FloatPixel, 4> const secondPatches = neutralPatchPixels(secondCapture.readback);
        verifyNeutralPatchProperties(secondPatches, secondTargetHeadroom);
        for (std::size_t index = 0; index < patches.size(); ++index) {
            compareNear(secondPatches[index].red, patches[index].red, 0.01f);
        }

        DecodedFrameCapture const positiveMinimumCapture =
            captureDecodedFrame(*graphics, frames.front(), referenceWhiteNits, targetPeakHeadroom, std::nullopt, 5.0f);
        QVERIFY2(positiveMinimumCapture.isSuccess(), qPrintable(positiveMinimumCapture.error));
        std::array<FloatPixel, 4> const positiveMinimumPatches = neutralPatchPixels(positiveMinimumCapture.readback);
        QVERIFY2(std::abs(patches.front().red - positiveMinimumPatches.front().red) > 0.001f,
                 "A positive physical target minimum did not change the darkest captured patch");
    } else {
        QVERIFY(sdrCapture.producer.colorPolicy.contains(QStringLiteral("BT.2446A EETF")));
        QVERIFY(sdrCapture.producer.colorPolicy.contains(QStringLiteral("Dolby Vision")));
        for (auto const& frame : frames) {
            QVERIFY(sideData(*frame, AV_FRAME_DATA_DOVI_RPU_BUFFER));
            QVERIFY(sideData(*frame, AV_FRAME_DATA_DOVI_METADATA));
        }
        QVERIFY(capture.input.metadataPath.contains(QStringLiteral("Dolby Vision reshape mapped by libplacebo")));
    }
#endif
}

void FfmpegFirstFrameTest::nominalSdrTargetPreservesWcg() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QSKIP("This test requires a D3D11 or Metal graphics domain");
#else
    auto const freeFrame = [](AVFrame* frame) { av_frame_free(&frame); };
    std::unique_ptr<AVFrame, decltype(freeFrame)> source(av_frame_alloc(), freeFrame);
    QVERIFY(source);
    source->format = AV_PIX_FMT_RGB24;
    source->width = 4;
    source->height = 4;
    source->color_primaries = AVCOL_PRI_SMPTE432;
    source->color_trc = AVCOL_TRC_SMPTE2084;
    source->colorspace = AVCOL_SPC_RGB;
    source->color_range = AVCOL_RANGE_JPEG;
    source->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    QVERIFY(av_frame_get_buffer(source.get(), 0) >= 0);
    QVERIFY(av_frame_make_writable(source.get()) >= 0);

    auto const pqCode = [](float nits) {
        return static_cast<std::uint8_t>(std::lround(255.0f * pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits)));
    };
    for (int y = 0; y < source->height; ++y) {
        std::uint8_t* const row = source->data[0] + y * source->linesize[0];
        for (int x = 0; x < source->width; ++x) {
            row[x * 3] = x == 0 ? 0 : pqCode(x == 1 ? 50.0f : 100.0f);
            row[x * 3 + 1] = 0;
            row[x * 3 + 2] = 0;
        }
    }

    AVFrameSideData* const lightSideData =
        av_frame_new_side_data(source.get(), AV_FRAME_DATA_CONTENT_LIGHT_LEVEL, sizeof(AVContentLightMetadata));
    QVERIFY(lightSideData);
    std::memset(lightSideData->data, 0, lightSideData->size);
    auto* const light = reinterpret_cast<AVContentLightMetadata*>(lightSideData->data);
    light->MaxCLL = 100;

    QString cloneError;
    std::shared_ptr<DecodedVideoFrame const> const frame =
        DecodedVideoFrame::clone(*source,
                                 {
                                     .playbackGeneration = 95,
                                     .decoderRevision = 1,
                                     .frameId = 1,
                                 },
                                 {1, 24}, std::nullopt, std::nullopt, &cloneError);
    QVERIFY2(frame, qPrintable(cloneError));

    ColorPrimaries const displayP3{
        .red = {0.680f, 0.320f},
        .green = {0.265f, 0.690f},
        .blue = {0.150f, 0.060f},
        .white = {0.3127f, 0.3290f},
    };
    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the graphics domain");
    DecodedFrameCapture const capture = captureDecodedFrame(*graphics, frame, PL_COLOR_SDR_WHITE, 1.0f, displayP3);
    QVERIFY2(capture.isSuccess(), qPrintable(capture.error));

    pl_raw_primaries const p3{
        .red = {displayP3.red.x, displayP3.red.y},
        .green = {displayP3.green.x, displayP3.green.y},
        .blue = {displayP3.blue.x, displayP3.blue.y},
        .white = {displayP3.white.x, displayP3.white.y},
    };
    pl_matrix3x3 const bt709ToP3 =
        pl_get_color_mapping_matrix(pl_raw_primaries_get(PL_COLOR_PRIM_BT_709), &p3, PL_INTENT_RELATIVE_COLORIMETRIC);
    auto const targetRgb = [&bt709ToP3](FloatPixel const& sample) {
        std::array values{sample.red, sample.green, sample.blue};
        pl_matrix3x3_apply(&bt709ToP3, values.data());
        return values;
    };

    std::array<float, 3> const black = targetRgb(pixel(capture.readback, 0, 2));
    std::array<float, 3> const shadow = targetRgb(pixel(capture.readback, 1, 2));
    std::array<float, 3> const white = targetRgb(pixel(capture.readback, 3, 2));
    compareNear(black[0], 0.0f, 0.002f);
    compareNear(black[1], 0.0f, 0.002f);
    compareNear(black[2], 0.0f, 0.002f);
    compareNear(shadow[0], 0.5f, 0.02f);
    compareNear(shadow[1], 0.0f, 0.02f);
    compareNear(shadow[2], 0.0f, 0.02f);
    compareNear(white[0], 1.0f, 0.03f);
    compareNear(white[1], 0.0f, 0.03f);
    compareNear(white[2], 0.0f, 0.03f);
    QVERIFY(pixel(capture.readback, 3, 2).red > 1.1f);
#endif
}

void FfmpegFirstFrameTest::dualFormatTargetChangeRemapsPausedFrame() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QSKIP("This test requires a D3D11 or Metal graphics domain");
#else
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/dovi-profile81-hevc.hevc");
    FfmpegFirstFrameResult const decoded = decodeFirstVideoFrame(fixture, {
                                                                              .playbackGeneration = 46,
                                                                              .decoderRevision = 1,
                                                                              .frameId = 1,
                                                                          });
    QVERIFY2(decoded.isSuccess(), qPrintable(decoded.error));

    AVFrame* dualSource = av_frame_clone(&decoded.frame->ffmpegFrame());
    QVERIFY(dualSource);
    AVFrameSideData* dynamic =
        av_frame_new_side_data(dualSource, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, sizeof(AVDynamicHDRPlus));
    QVERIFY(dynamic);
    std::memset(dynamic->data, 0, dynamic->size);
    auto* hdr10Plus = reinterpret_cast<AVDynamicHDRPlus*>(dynamic->data);
    hdr10Plus->itu_t_t35_country_code = 0xb5;
    hdr10Plus->application_version = 1;
    hdr10Plus->num_windows = 1;
    hdr10Plus->targeted_system_display_maximum_luminance = {600, 1};
    hdr10Plus->params[0].maxscl[0] = {32, 100};
    hdr10Plus->params[0].maxscl[1] = {30, 100};
    hdr10Plus->params[0].maxscl[2] = {28, 100};
    hdr10Plus->params[0].average_maxrgb = {1, 10};
    hdr10Plus->params[0].tone_mapping_flag = 1;
    hdr10Plus->params[0].knee_point_x = {1, 2};
    hdr10Plus->params[0].knee_point_y = {1, 2};
    hdr10Plus->params[0].num_bezier_curve_anchors = 2;
    hdr10Plus->params[0].bezier_curve_anchors[0] = {2, 3};
    hdr10Plus->params[0].bezier_curve_anchors[1] = {5, 6};

    QString frameError;
    std::shared_ptr<DecodedVideoFrame const> const dualFrame = DecodedVideoFrame::clone(
        *dualSource, decoded.frame->identity(), decoded.frame->timing().timeBase, std::nullopt, true, &frameError);
    av_frame_free(&dualSource);
    QVERIFY2(dualFrame, qPrintable(frameError));
    QCOMPARE(dualFrame->dynamicRange(), VideoDynamicRange::DolbyVision);

    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the graphics domain");
    GraphicsDeviceExecutionScope execution = graphics->acquireExecutionScope();
    DecodedVideoSource source(dualFrame, VideoTargetReadback::Disabled);
    LibplaceboDecodedVideoProducer producer(*graphics, source, VideoTargetReadback::Disabled);
    QRhi& rhi = graphics->rhi();

    auto const render = [&](float headroom) {
        RenderedVideoSurfaceState const state =
            surfaceState(*graphics, source.contentRevision(), 203.0f, dualFrame->geometry().visibleSize, headroom);
        if (producer.ensureSurface(state) != VideoOperationResult::Ready) {
            return false;
        }
        QRhiCommandBuffer* commandBuffer = nullptr;
        if (rhi.beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess || !commandBuffer) {
            return false;
        }
        if (producer.render(*commandBuffer, state) != VideoOperationResult::Ready ||
            producer.prepareForComposition(*commandBuffer) != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            producer.submissionAborted();
            producer.discardPendingRender();
            return false;
        }
        if (rhi.endOffscreenFrame() != QRhi::FrameOpSuccess) {
            producer.submissionAborted();
            producer.discardPendingRender();
            return false;
        }
        producer.submissionAccepted();
        producer.commitPendingRender();
        return true;
    };

    QVERIFY(render(1.0f));
    QCOMPARE(producer.inputImportCount(), 1U);
    QVERIFY(producer.frameImportDiagnostics().metadataPath.contains(QStringLiteral("base-layer representation")));
    QVERIFY(producer.diagnostics().colorPolicy.contains(QStringLiteral("ST 2094-40 EETF")));

    QVERIFY(render(6.0f));
    QCOMPARE(producer.inputImportCount(), 2U);
    QVERIFY(producer.frameImportDiagnostics().metadataPath.contains(QStringLiteral("Dolby Vision reshape mapped")));
    QVERIFY(producer.diagnostics().colorPolicy.contains(QStringLiteral("Spline tone map")));

    QVERIFY(render(1.0f));
    QCOMPARE(producer.inputImportCount(), 3U);
    QVERIFY(producer.frameImportDiagnostics().metadataPath.contains(QStringLiteral("base-layer representation")));
#endif
}

void FfmpegFirstFrameTest::realDemuxDecodeImportAndComposition() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QSKIP("This test requires a D3D11 or Metal graphics domain");
#else
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-rgb-first-frame.ppm");
    QString const manifest = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-rgb-first-frame.toml");
    QByteArray const declaredHash = expectedFixtureHash(manifest);
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    FfmpegFirstFrameResult const decoded = decodeFirstVideoFrame(fixture, {
                                                                              .playbackGeneration = 7,
                                                                              .decoderRevision = 11,
                                                                              .frameId = 1,
                                                                          });
    QVERIFY2(decoded.isSuccess(), qPrintable(decoded.error));
    FfmpegFirstFrameResult const secondDecode = decodeFirstVideoFrame(fixture, {
                                                                                   .playbackGeneration = 7,
                                                                                   .decoderRevision = 11,
                                                                                   .frameId = 2,
                                                                               });
    QVERIFY2(secondDecode.isSuccess(), qPrintable(secondDecode.error));
    QVERIFY(decoded.frame->identity() != secondDecode.frame->identity());
    bool const expectedImageDemuxer =
        decoded.diagnostics.containerFormat.contains(QStringLiteral("image"), Qt::CaseInsensitive) ||
        decoded.diagnostics.containerFormat.contains(QStringLiteral("ppm"), Qt::CaseInsensitive);
    QVERIFY2(
        expectedImageDemuxer,
        qPrintable(QStringLiteral("Unexpected FFmpeg image demuxer: %1").arg(decoded.diagnostics.containerFormat)));
    QCOMPARE(decoded.diagnostics.decoderName, QStringLiteral("ppm"));
    QCOMPARE(decoded.frame->identity().playbackGeneration, 7U);
    QCOMPARE(decoded.frame->identity().decoderRevision, 11U);
    QCOMPARE(decoded.frame->geometry().codedSize, QSize(4, 4));
    QCOMPARE(decoded.frame->storage().kind, VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(decoded.frame->storage().softwareFormat, QStringLiteral("rgb24"));

    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the graphics domain");
    QRhi& rhi = graphics->rhi();
    DecodedVideoSource source(decoded.frame, VideoTargetReadback::Enabled);
    source.setFrame(secondDecode.frame);
    QCOMPARE(source.contentRevision(), 2U);
    LibplaceboDecodedVideoProducer producer(*graphics, source, VideoTargetReadback::Enabled);
    RenderedVideoSurfaceState state = surfaceState(*graphics, source.contentRevision());
    QCOMPARE(producer.ensureSurface(state), VideoOperationResult::Ready);

    std::unique_ptr<QRhiTexture> uiTexture(rhi.newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(uiTexture->create());
    std::unique_ptr<QRhiTexture> subtitleTexture(rhi.newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(subtitleTexture->create());
    std::unique_ptr<QRhiTexture> compositionTexture(
        rhi.newTexture(QRhiTexture::RGBA16F, {4, 4}, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(compositionTexture->create());
    QRhiTextureRenderTargetDescription const compositionDescription(QRhiColorAttachment(compositionTexture.get()));
    std::unique_ptr<QRhiTextureRenderTarget> compositionTarget(rhi.newTextureRenderTarget(compositionDescription));
    std::unique_ptr<QRhiRenderPassDescriptor> compositionPass(compositionTarget->newCompatibleRenderPassDescriptor());
    compositionTarget->setRenderPassDescriptor(compositionPass.get());
    QVERIFY(compositionTarget->create());

    HdrCompositor compositor(rhi);
    QCOMPARE(
        compositor.initialize(*compositionPass, &producer.textureForComposition(), subtitleTexture.get(), *uiTexture),
        HdrCompositor::ResourceResult::Ready);

    auto const capture = [&](RenderedVideoSurfaceState const& requested, QRhiReadbackResult& surfaceReadback,
                             QRhiReadbackResult& compositionReadback) {
        QRhiCommandBuffer* commandBuffer = nullptr;
        if (rhi.beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess || !commandBuffer) {
            return false;
        }

        QByteArray const transparentUi(4, '\0');
        QRhiResourceUpdateBatch* updates = rhi.nextResourceUpdateBatch();
        updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                    0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
        updates->uploadTexture(subtitleTexture.get(),
                               QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                   0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
        commandBuffer->resourceUpdate(updates);

        if (producer.render(*commandBuffer, requested) != VideoOperationResult::Ready ||
            producer.prepareForComposition(*commandBuffer) != VideoOperationResult::Ready) {
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
        parameters.ndcYUp = rhi.isYUpInNDC() ? 1.0f : 0.0f;
        parameters.outputEncoding = 2.0f;
        compositor.render(*commandBuffer, *compositionTarget, {4, 4}, parameters);

        bool surfaceCompleted = false;
        bool compositionCompleted = false;
        surfaceReadback.completed = [&surfaceCompleted] { surfaceCompleted = true; };
        compositionReadback.completed = [&compositionCompleted] { compositionCompleted = true; };
        updates = rhi.nextResourceUpdateBatch();
        updates->readBackTexture(QRhiReadbackDescription(&producer.textureForComposition()), &surfaceReadback);
        updates->readBackTexture(QRhiReadbackDescription(compositionTexture.get()), &compositionReadback);
        commandBuffer->resourceUpdate(updates);

        QRhi::FrameOpResult const frameResult = rhi.endOffscreenFrame();
        if (frameResult == QRhi::FrameOpSuccess) {
            producer.submissionAccepted();
            producer.commitPendingRender();
        } else {
            producer.submissionAborted();
            producer.discardPendingRender();
        }
        return frameResult == QRhi::FrameOpSuccess && surfaceCompleted && compositionCompleted;
    };

    QRhiReadbackResult surfaceReadback;
    QRhiReadbackResult compositionReadback;
    QVERIFY(capture(state, surfaceReadback, compositionReadback));
    QCOMPARE(surfaceReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(surfaceReadback.pixelSize, QSize(4, 4));
    QCOMPARE(producer.inputImportCount(), 1U);

    FloatPixel const red = pixel(surfaceReadback, 0, 0);
    QVERIFY(red.red > 0.95f);
    QVERIFY(red.green < 0.02f);
    QVERIFY(red.blue < 0.02f);
    compareNear(red.alpha, 1.0f, 0.002f);
    FloatPixel const green = pixel(surfaceReadback, 1, 0);
    QVERIFY(green.red < 0.02f);
    QVERIFY(green.green > 0.95f);
    QVERIFY(green.blue < 0.02f);
    FloatPixel const gray = pixel(surfaceReadback, 1, 1);
    QVERIFY(gray.red > 0.15f && gray.red < 0.30f);
    compareNear(gray.red, gray.green, 0.01f);
    compareNear(gray.green, gray.blue, 0.01f);

    FloatPixel const composed = pixel(compositionReadback, 0, 0);
    compareNear(composed.red, red.red, 0.01f);
    compareNear(composed.green, red.green, 0.01f);
    compareNear(composed.blue, red.blue, 0.01f);

    VideoFrameImportDiagnostics const& input = producer.frameImportDiagnostics();
    QVERIFY(input.isValid());
    QCOMPARE(input.path, VideoFrameImportPath::SoftwareUpload);
    QCOMPARE(input.knownCpuDownloadsPerFrame, 0U);
    QCOMPARE(input.knownCpuUploadsPerFrame, 1U);
    QCOMPARE(input.knownGpuCopiesPerFrame, 0U);
    RenderedVideoProducerDiagnostics const diagnostics = producer.diagnostics();
    QVERIFY(diagnostics.isValid());
    QCOMPARE(diagnostics.target.outputPath, VideoOutputPath::DirectRenderTarget);
    QCOMPARE(diagnostics.target.knownOutputGpuCopiesPerRender, 0U);
    QCOMPARE(diagnostics.target.knownOutputCpuTransfersPerRender, 0U);

    RenderedVideoSurfaceState changedTarget = state;
    changedTarget.description.referenceWhiteNits = 100.0f;
    QCOMPARE(producer.ensureSurface(changedTarget), VideoOperationResult::Ready);
    QVERIFY(producer.needsRender(changedTarget));
    QRhiReadbackResult changedSurface;
    QRhiReadbackResult changedComposition;
    QVERIFY(capture(changedTarget, changedSurface, changedComposition));
    QCOMPARE(producer.inputImportCount(), 1U);
    FloatPixel const changedRed = pixel(changedSurface, 0, 0);
    compareNear(changedRed.red, red.red, 0.02f);
    compareNear(changedRed.green, red.green, 0.02f);
    compareNear(changedRed.blue, red.blue, 0.02f);
#endif
}

void FfmpegFirstFrameTest::compressedYuvMetadataAndRendering() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QSKIP("This test requires a D3D11 or Metal graphics domain");
#else
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1.mkv");
    QString const manifest = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1.toml");
    QByteArray const declaredHash = expectedFixtureHash(manifest);
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the graphics domain");
    FfmpegFirstFrameResult const decoded = decodeFirstVideoFrame(fixture,
                                                                 {
                                                                     .playbackGeneration = 13,
                                                                     .decoderRevision = 1,
                                                                     .frameId = 1,
                                                                 },
                                                                 graphics->videoDecodeCapability());
    QVERIFY2(decoded.isSuccess(), qPrintable(decoded.error));
    QVERIFY(decoded.diagnostics.containerFormat.contains(QStringLiteral("matroska")));
    QCOMPARE(decoded.diagnostics.decoderName, QStringLiteral("ffv1"));
    QVERIFY(!decoded.diagnostics.hardwareAccelerated);
    QCOMPARE(decoded.diagnostics.decodePath, QStringLiteral("Software"));
    QVERIFY2(!decoded.diagnostics.hardwareFallbackReason.isEmpty(),
             "Software fallback must explain why hardware was unavailable");

    DecodedVideoFrame const& frame = *decoded.frame;
    QCOMPARE(frame.geometry().codedSize, QSize(96, 64));
    QCOMPARE(frame.geometry().visibleSize, QSize(96, 64));
    QVERIFY(frame.geometry().sampleAspectRatioKnown);
    QCOMPARE(frame.geometry().sampleAspectRatio.numerator, 32);
    QCOMPARE(frame.geometry().sampleAspectRatio.denominator, 27);
    QCOMPARE(frame.storage().kind, VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(frame.storage().softwareFormat, QStringLiteral("yuv420p"));
    QCOMPARE(frame.signal().componentDepth, 8);
    QCOMPARE(frame.signal().colorPrimaries, QStringLiteral("bt709"));
    QCOMPARE(frame.signal().transferFunction, QStringLiteral("bt709"));
    QCOMPARE(frame.signal().matrixCoefficients, QStringLiteral("bt709"));
    QCOMPARE(frame.signal().colorRange, QStringLiteral("tv"));
    QCOMPARE(frame.signal().chromaLocation, QStringLiteral("left"));
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
        YuvSample{16, 16, 16, 128, 128}, YuvSample{48, 16, 126, 128, 128}, YuvSample{80, 16, 235, 128, 128},
        YuvSample{16, 48, 63, 102, 240}, YuvSample{48, 48, 173, 42, 26},   YuvSample{80, 48, 32, 240, 118},
    };
    AVFrame const& avFrame = frame.ffmpegFrame();
    for (YuvSample const& sample : samples) {
        QCOMPARE(avFrame.data[0][sample.y * avFrame.linesize[0] + sample.x], sample.luma);
        QCOMPARE(avFrame.data[1][(sample.y / 2) * avFrame.linesize[1] + sample.x / 2], sample.chromaBlue);
        QCOMPARE(avFrame.data[2][(sample.y / 2) * avFrame.linesize[2] + sample.x / 2], sample.chromaRed);
    }

    QRhi& rhi = graphics->rhi();
    DecodedVideoSource source(decoded.frame, VideoTargetReadback::Enabled);
    LibplaceboDecodedVideoProducer producer(*graphics, source, VideoTargetReadback::Enabled);
    RenderedVideoSurfaceState const state = surfaceState(*graphics, source.contentRevision(), 203.0f, {96, 64});
    QCOMPARE(producer.ensureSurface(state), VideoOperationResult::Ready);

    QRhiCommandBuffer* commandBuffer = nullptr;
    QCOMPARE(rhi.beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QVERIFY(commandBuffer);
    QCOMPARE(producer.render(*commandBuffer, state), VideoOperationResult::Ready);
    QCOMPARE(producer.prepareForComposition(*commandBuffer), VideoOperationResult::Ready);

    QRhiReadbackResult readback;
    bool readbackCompleted = false;
    readback.completed = [&readbackCompleted] { readbackCompleted = true; };
    QRhiResourceUpdateBatch* updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(&producer.textureForComposition()), &readback);
    commandBuffer->resourceUpdate(updates);
    QRhi::FrameOpResult const frameResult = rhi.endOffscreenFrame();
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
    QVERIFY(readback.data.size() >=
            readback.pixelSize.width() * readback.pixelSize.height() * 4 * qsizetype(sizeof(qfloat16)));

    struct ExpectedRgb {
        int x;
        int y;
        float red;
        float green;
        float blue;
    };
    constexpr std::array expected{
        ExpectedRgb{16, 16, 0.0f, 0.0f, 0.0f},     ExpectedRgb{48, 16, 0.19165f, 0.19165f, 0.19165f},
        ExpectedRgb{80, 16, 1.0f, 1.0f, 1.0f},     ExpectedRgb{16, 48, 1.0f, 0.00051f, 0.0f},
        ExpectedRgb{48, 48, 0.0f, 1.0f, 0.00099f}, ExpectedRgb{80, 48, 0.00061f, 0.00007f, 1.0f},
    };
    for (ExpectedRgb const& sample : expected) {
        FloatPixel const actual = pixel(readback, sample.x, sample.y);
        compareNear(actual.red, sample.red, 0.02f);
        compareNear(actual.green, sample.green, 0.02f);
        compareNear(actual.blue, sample.blue, 0.02f);
        compareNear(actual.alpha, 1.0f, 0.002f);
    }
    QCOMPARE(producer.inputImportCount(), 1U);
    QCOMPARE(producer.frameImportDiagnostics().path, VideoFrameImportPath::SoftwareUpload);
#endif
}

void FfmpegFirstFrameTest::continuousDecodeDrainsEveryFrame() {
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1.mkv");
    std::vector<std::shared_ptr<DecodedVideoFrame const>> frames;
    FfmpegVideoDecodeResult const result = decodeVideoFrames(
        {
            .path = fixture,
            .firstFrameIdentity =
                {
                    .playbackGeneration = 17,
                    .decoderRevision = 3,
                    .frameId = 40,
                },
        },
        [&frames](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const&) {
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
    QCOMPARE(frames[0]->timing().ptsMicroseconds(), std::optional<std::int64_t>(0));
    QCOMPARE(frames[1]->timing().ptsMicroseconds(), std::optional<std::int64_t>(250'000));
    QCOMPARE(frames[2]->timing().ptsMicroseconds(), std::optional<std::int64_t>(500'000));
    QCOMPARE(result.diagnostics.nominalFrameDurationMicroseconds, std::optional<std::int64_t>(250'000));
}

void FfmpegFirstFrameTest::seekDecodesInterFramePreroll() {
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264-seek.mkv");
    QString const manifest = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264-seek.toml");
    QByteArray const declaredHash = expectedFixtureHash(manifest);
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    FfmpegFirstFrameResult const initial = decodeFirstVideoFrame(fixture, {
                                                                              .playbackGeneration = 18,
                                                                              .decoderRevision = 1,
                                                                              .frameId = 1,
                                                                          });
    QVERIFY2(initial.isSuccess(), qPrintable(initial.error));
    QVERIFY(initial.diagnostics.seekable);
    QVERIFY(initial.diagnostics.timelineOrigin);

    std::vector<std::int64_t> decodedPts;
    std::optional<VideoSignalDescription> decodedSignal;
    FfmpegVideoDecodeResult const sought = decodeVideoFrames(
        {
            .path = fixture,
            .firstFrameIdentity =
                {
                    .playbackGeneration = 19,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
            .start =
                {
                    .targetPositionMicroseconds = 3'250'000,
                    .timelineOrigin = initial.diagnostics.timelineOrigin,
                    .performDemuxSeek = true,
                },
        },
        [&decodedPts, &decodedSignal](std::shared_ptr<DecodedVideoFrame const> frame,
                                      FfmpegVideoStreamDiagnostics const&) {
            auto const pts = frame->timing().ptsMicroseconds();
            if (!pts) {
                return false;
            }
            if (!decodedSignal) {
                decodedSignal = frame->signal();
            }
            decodedPts.push_back(*pts);
            return *pts < 3'250'000;
        });

    QVERIFY2(sought.isSuccess(), qPrintable(sought.error));
    QVERIFY(sought.stopped);
    QCOMPARE(decodedPts.size(), 6U);
    QCOMPARE(decodedPts.front(), 2'000'000);
    QCOMPARE(decodedPts.back(), 3'250'000);
    QVERIFY(std::is_sorted(decodedPts.cbegin(), decodedPts.cend()));
    QVERIFY(decodedPts.front() < 3'250'000);
    QVERIFY(decodedSignal);
    QCOMPARE(decodedSignal->colorPrimaries, QStringLiteral("bt709"));
    QCOMPARE(decodedSignal->transferFunction, QStringLiteral("bt709"));
    QCOMPARE(decodedSignal->matrixCoefficients, QStringLiteral("bt709"));
}

void FfmpegFirstFrameTest::longTimelineSeekUses64BitTarget() {
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-long-timeline.mkv");
    QString const manifest = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-long-timeline.toml");
    QByteArray const declaredHash = expectedFixtureHash(manifest);
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    FfmpegFirstFrameResult const initial = decodeFirstVideoFrame(fixture, {
                                                                              .playbackGeneration = 20,
                                                                              .decoderRevision = 1,
                                                                              .frameId = 1,
                                                                          });
    QVERIFY2(initial.isSuccess(), qPrintable(initial.error));
    QVERIFY(initial.diagnostics.seekable);
    QVERIFY(initial.diagnostics.timelineOrigin);
    QCOMPARE(initial.diagnostics.durationMicroseconds, std::optional<std::int64_t>(3'001'000'000LL));

    std::optional<std::int64_t> firstSoughtPts;
    FfmpegVideoDecodeResult const sought = decodeVideoFrames(
        {
            .path = fixture,
            .firstFrameIdentity =
                {
                    .playbackGeneration = 21,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
            .start =
                {
                    .targetPositionMicroseconds = 3'000'000'000LL,
                    .timelineOrigin = initial.diagnostics.timelineOrigin,
                    .performDemuxSeek = true,
                },
        },
        [&firstSoughtPts](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const&) {
            firstSoughtPts = frame->timing().ptsMicroseconds();
            return false;
        });

    QVERIFY2(sought.isSuccess(), qPrintable(sought.error));
    QVERIFY(sought.stopped);
    QCOMPARE(sought.framesDecoded, 1U);
    QCOMPARE(firstSoughtPts, std::optional<std::int64_t>(3'000'000'000LL));
}

void FfmpegFirstFrameTest::hardwareDecodeFailureRetriesSoftware() {
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1.mkv");
    VideoFrameIdentity const identity{
        .playbackGeneration = 19,
        .decoderRevision = 1,
        .frameId = 1,
    };
    int attempts = 0;
    bool receivedSoftwareFallback = false;
    FfmpegVideoDecodeResult const result = decodeVideoFramesWithFallback(
        {
            .device = {},
            .unavailableReason = {},
        },
        [&](VideoHardwareDecodeCapability const& capability, bool& hardwareSelected) {
            ++attempts;
            if (attempts == 1) {
                hardwareSelected = true;
                FfmpegVideoDecodeResult failure;
                failure.error = QStringLiteral("Injected post-selection hardware failure");
                return failure;
            }
            hardwareSelected = false;
            receivedSoftwareFallback =
                !capability.isAvailable() &&
                capability.unavailableReason.contains(QStringLiteral("Injected post-selection hardware failure"));
            return decodeVideoFrames(
                {
                    .path = fixture,
                    .firstFrameIdentity = identity,
                    .hardwareDecode = capability,
                    .extraHardwareFrames = 2,
                },
                [](std::shared_ptr<DecodedVideoFrame const>, FfmpegVideoStreamDiagnostics const&) { return false; });
        });

    QCOMPARE(attempts, 2);
    QVERIFY(receivedSoftwareFallback);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(!result.diagnostics.hardwareAccelerated);
    QCOMPARE(result.diagnostics.decodePath, QStringLiteral("Software"));
    QVERIFY(
        result.diagnostics.hardwareFallbackReason.contains(QStringLiteral("Injected post-selection hardware failure")));
}

void FfmpegFirstFrameTest::videoToolboxHardwareDecodeDirectImport_data() {
    QTest::addColumn<QString>("fixtureName");
    QTest::addColumn<QString>("softwareFormat");
    QTest::addColumn<int>("componentDepth");
    QTest::addColumn<float>("comparisonTolerance");

    QTest::newRow("nv12-h264") << QStringLiteral("sdr-bt709-h264.mkv") << QStringLiteral("nv12") << 8 << 0.035f;
    QTest::newRow("p010-hevc") << QStringLiteral("hdr10-pq-hevc.hevc") << QStringLiteral("p010le") << 10 << 0.04f;
}

void FfmpegFirstFrameTest::videoToolboxHardwareDecodeDirectImport() {
#ifndef Q_OS_MACOS
    QSKIP("VideoToolbox direct import is macOS-specific");
#else
    QFETCH(QString, fixtureName);
    QFETCH(QString, softwareFormat);
    QFETCH(int, componentDepth);
    QFETCH(float, comparisonTolerance);

    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/%1").arg(fixtureName);
    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the Metal graphics domain");
    QCOMPARE(graphics->backend(), GraphicsBackend::Metal);

    VideoHardwareDecodeCapability const& hardwareDecode = graphics->videoDecodeCapability();
    if (!hardwareDecode.isAvailable()) {
        QString const reason = QStringLiteral("VideoToolbox unavailable: %1").arg(hardwareDecode.unavailableReason);
        if (qEnvironmentVariableIntValue("SUNPLAYER_REQUIRE_VIDEOTOOLBOX") != 0) {
            QFAIL(qPrintable(reason));
        }
        QSKIP(qPrintable(reason));
    }

    FfmpegFirstFrameResult const hardware = decodeFirstVideoFrame(fixture,
                                                                  {
                                                                      .playbackGeneration = 51,
                                                                      .decoderRevision = 1,
                                                                      .frameId = 1,
                                                                  },
                                                                  hardwareDecode);
    QVERIFY2(hardware.isSuccess(), qPrintable(hardware.error));
    QVERIFY(hardware.diagnostics.hardwareAccelerated);
    QCOMPARE(hardware.diagnostics.decodePath, QStringLiteral("VideoToolbox"));
    QVERIFY(hardware.diagnostics.hardwareFallbackReason.isEmpty());
    QCOMPARE(hardware.frame->storage().kind, VideoFrameStorageKind::VideoToolboxSurface);
    QCOMPARE(hardware.frame->storage().hardwareFormat, QStringLiteral("videotoolbox_vld"));
    QCOMPARE(hardware.frame->storage().softwareFormat, softwareFormat);
    QCOMPARE(hardware.frame->storage().graphicsDeviceGeneration, std::optional<std::uint64_t>(graphics->generation()));
    QCOMPARE(hardware.frame->signal().componentDepth, componentDepth);

    FfmpegFirstFrameResult const software = decodeFirstVideoFrame(fixture, {
                                                                               .playbackGeneration = 52,
                                                                               .decoderRevision = 1,
                                                                               .frameId = 1,
                                                                           });
    QVERIFY2(software.isSuccess(), qPrintable(software.error));
    QVERIFY(!software.diagnostics.hardwareAccelerated);

    DecodedFrameCapture const hardwareCapture = captureDecodedFrame(*graphics, hardware.frame);
    QVERIFY2(hardwareCapture.isSuccess(), qPrintable(hardwareCapture.error));
    DecodedFrameCapture const softwareCapture = captureDecodedFrame(*graphics, software.frame);
    QVERIFY2(softwareCapture.isSuccess(), qPrintable(softwareCapture.error));

    QCOMPARE(hardwareCapture.input.path, VideoFrameImportPath::DirectHardwareSurface);
    QCOMPARE(hardwareCapture.input.knownCpuDownloadsPerFrame, 0U);
    QCOMPARE(hardwareCapture.input.knownCpuUploadsPerFrame, 0U);
    QCOMPARE(hardwareCapture.input.knownGpuCopiesPerFrame, 0U);
    QVERIFY(hardwareCapture.input.nativeResource.contains(softwareFormat.toUpper()));
    QVERIFY(hardwareCapture.input.synchronizationMode.contains(QStringLiteral("CVPixelBuffer")));
    QCOMPARE(hardwareCapture.producer.target.outputPath, VideoOutputPath::DirectRenderTarget);
    QCOMPARE(hardwareCapture.producer.target.knownOutputGpuCopiesPerRender, 0U);
    QCOMPARE(hardwareCapture.producer.target.knownOutputCpuTransfersPerRender, 0U);
    QCOMPARE(hardwareCapture.readback.pixelSize, softwareCapture.readback.pixelSize);

    int const width = hardwareCapture.readback.pixelSize.width();
    int const height = hardwareCapture.readback.pixelSize.height();
    std::array const samplePoints{
        QPoint{width / 6, height / 4},     QPoint{width / 2, height / 4},     QPoint{width * 5 / 6, height / 4},
        QPoint{width / 6, height * 3 / 4}, QPoint{width / 2, height * 3 / 4}, QPoint{width * 5 / 6, height * 3 / 4},
    };
    for (QPoint const& sample : samplePoints) {
        FloatPixel const hardwarePixel = pixel(hardwareCapture.readback, sample.x(), sample.y());
        FloatPixel const softwarePixel = pixel(softwareCapture.readback, sample.x(), sample.y());
        compareNear(hardwarePixel.red, softwarePixel.red, comparisonTolerance);
        compareNear(hardwarePixel.green, softwarePixel.green, comparisonTolerance);
        compareNear(hardwarePixel.blue, softwarePixel.blue, comparisonTolerance);
        compareNear(hardwarePixel.alpha, 1.0f, 0.002f);
    }
#endif
}

void FfmpegFirstFrameTest::metalTargetResizesBeforeFirstSubmission() {
#ifndef Q_OS_MACOS
    QSKIP("The deferred Metal handoff is macOS-specific");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the Metal graphics domain");
    GraphicsDeviceExecutionScope execution = graphics->acquireExecutionScope();
    std::unique_ptr<VideoTargetInterop> target = graphics->createVideoTarget({
        .producerApi = VideoProducerApi::Libplacebo,
        .readback = VideoTargetReadback::Disabled,
    });
    QVERIFY(target);

    RenderedVideoSurfaceDescription const initial = surfaceState(*graphics, 1, 203.0f, {8, 8}).description;
    RenderedVideoSurfaceDescription const resized = surfaceState(*graphics, 2, 203.0f, {12, 10}).description;
    QCOMPARE(target->ensureTarget(initial), VideoTargetUpdate::Created);
    QCOMPARE(target->ensureTarget(resized), VideoTargetUpdate::Resized);

    QRhi& rhi = graphics->rhi();
    QRhiCommandBuffer* commandBuffer = nullptr;
    QCOMPARE(rhi.beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QVERIFY(commandBuffer);
    QCOMPARE(target->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    QRhi::FrameOpResult const result = rhi.endOffscreenFrame();
    if (result == QRhi::FrameOpSuccess) {
        target->submissionAccepted();
    } else {
        target->submissionAborted();
    }
    QCOMPARE(result, QRhi::FrameOpSuccess);
#endif
}

void FfmpegFirstFrameTest::continuousVideoToolboxDecodeAndImport() {
#ifndef Q_OS_MACOS
    QSKIP("VideoToolbox decoding is macOS-specific");
#else
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264.mkv");
    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create the Metal graphics domain");
    VideoHardwareDecodeCapability const& hardwareDecode = graphics->videoDecodeCapability();
    QVERIFY2(hardwareDecode.isAvailable(), qPrintable(hardwareDecode.unavailableReason));

    std::vector<std::shared_ptr<DecodedVideoFrame const>> frames;
    FfmpegVideoDecodeResult const decoded = decodeVideoFrames(
        {
            .path = fixture,
            .firstFrameIdentity =
                {
                    .playbackGeneration = 61,
                    .decoderRevision = 1,
                    .frameId = 1,
                },
            .hardwareDecode = hardwareDecode,
            .extraHardwareFrames = static_cast<int>(VideoFrameQueue::capacity + 2),
        },
        [&frames](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const&) {
            frames.push_back(std::move(frame));
            return true;
        });
    QVERIFY2(decoded.isSuccess(), qPrintable(decoded.error));
    QVERIFY(decoded.endOfStream);
    QVERIFY(decoded.diagnostics.hardwareAccelerated);
    QCOMPARE(decoded.diagnostics.decodePath, QStringLiteral("VideoToolbox"));
    QCOMPARE(frames.size(), std::size_t(3));

    GraphicsDeviceExecutionScope execution = graphics->acquireExecutionScope();
    DecodedVideoSource source(std::move(frames[0]), VideoTargetReadback::Disabled);
    LibplaceboDecodedVideoProducer producer(*graphics, source, VideoTargetReadback::Disabled);
    QRhi& rhi = graphics->rhi();

    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (index > 0) {
            source.setFrame(std::move(frames[index]));
        }
        RenderedVideoSurfaceState const state =
            surfaceState(*graphics, source.contentRevision(), 203.0f, source.currentFrame()->geometry().visibleSize);
        QCOMPARE(producer.ensureSurface(state), VideoOperationResult::Ready);

        QRhiCommandBuffer* commandBuffer = nullptr;
        QCOMPARE(rhi.beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
        QVERIFY(commandBuffer);
        QCOMPARE(producer.render(*commandBuffer, state), VideoOperationResult::Ready);
        QCOMPARE(producer.prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
        QCOMPARE(rhi.endOffscreenFrame(), QRhi::FrameOpSuccess);
        producer.submissionAccepted();
        producer.commitPendingRender();
    }

    QCOMPARE(producer.inputImportCount(), 3U);
    QCOMPARE(producer.frameImportDiagnostics().path, VideoFrameImportPath::DirectHardwareSurface);
    QCOMPARE(producer.frameImportDiagnostics().knownGpuCopiesPerFrame, 0U);
#endif
}

void FfmpegFirstFrameTest::continuousD3d11DecodeRetainsBoundedFrames() {
#ifndef Q_OS_WIN
    QSKIP("D3D11VA decoding is Windows-specific");
#else
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264.mkv");
    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create D3D11 graphics domain");
    VideoHardwareDecodeCapability const capability = graphics->videoDecodeCapability();
    if (!capability.isAvailable()) {
        if (qEnvironmentVariableIntValue("SUNPLAYER_REQUIRE_D3D11VA") != 0) {
            QFAIL(qPrintable(
                QStringLiteral("D3D11VA is required by this test run: %1").arg(capability.unavailableReason)));
        }
        QSKIP(qPrintable(capability.unavailableReason));
    }

    std::vector<std::shared_ptr<DecodedVideoFrame const>> frames;
    FfmpegVideoDecodeResult const result = decodeVideoFrames(
        {
            .path = fixture,
            .firstFrameIdentity =
                {
                    .playbackGeneration = 29,
                    .decoderRevision = 5,
                    .frameId = 70,
                },
            .hardwareDecode = capability,
            .extraHardwareFrames = static_cast<int>(VideoFrameQueue::capacity + 2),
        },
        [&frames](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const&) {
            frames.push_back(std::move(frame));
            return true;
        });

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(result.endOfStream);
    QCOMPARE(result.framesDecoded, 3U);
    QCOMPARE(frames.size(), VideoFrameQueue::capacity);
    QVERIFY(result.diagnostics.hardwareAccelerated);
    QCOMPARE(result.diagnostics.decodePath, QStringLiteral("D3D11VA"));
    for (std::size_t index = 0; index < frames.size(); ++index) {
        QVERIFY(frames[index]->storage().isHardware());
        QCOMPARE(frames[index]->identity().frameId, 70U + index);
        QCOMPARE(frames[index]->storage().graphicsDeviceGeneration,
                 std::optional<std::uint64_t>(graphics->generation()));
    }
#endif
}

void FfmpegFirstFrameTest::d3d11HardwareDecodeSafeImport() {
#ifndef Q_OS_WIN
    QSKIP("D3D11VA import is Windows-specific");
#else
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264.mkv");
    QString const manifest = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264.toml");
    QByteArray const declaredHash = expectedFixtureHash(manifest);
    QVERIFY2(!declaredHash.isEmpty(), "Fixture manifest has no valid SHA-256");
    QCOMPARE(fixtureHash(fixture), declaredHash);

    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create D3D11 graphics domain");
    VideoHardwareDecodeCapability const& hardwareDecode = graphics->videoDecodeCapability();
    if (!hardwareDecode.isAvailable()) {
        QString const reason = QStringLiteral("D3D11VA unavailable: %1").arg(hardwareDecode.unavailableReason);
        if (qEnvironmentVariableIntValue("SUNPLAYER_REQUIRE_D3D11VA") != 0) {
            QFAIL(qPrintable(reason));
        }
        QSKIP(qPrintable(reason));
    }

    FfmpegFirstFrameResult const hardware = decodeFirstVideoFrame(fixture,
                                                                  {
                                                                      .playbackGeneration = 21,
                                                                      .decoderRevision = 1,
                                                                      .frameId = 1,
                                                                  },
                                                                  hardwareDecode);
    QVERIFY2(hardware.isSuccess(), qPrintable(hardware.error));
    QVERIFY(hardware.diagnostics.hardwareAccelerated);
    QCOMPARE(hardware.diagnostics.decodePath, QStringLiteral("D3D11VA"));
    QVERIFY(hardware.diagnostics.hardwareFallbackReason.isEmpty());
    QVERIFY(hardware.diagnostics.containerFormat.contains(QStringLiteral("matroska")));
    QCOMPARE(hardware.diagnostics.decoderName, QStringLiteral("h264"));
    QCOMPARE(hardware.frame->storage().kind, VideoFrameStorageKind::D3D11Surface);
    QCOMPARE(hardware.frame->storage().hardwareFormat, QStringLiteral("d3d11"));
    QCOMPARE(hardware.frame->storage().softwareFormat, QStringLiteral("nv12"));
    QCOMPARE(hardware.frame->storage().graphicsDeviceGeneration, std::optional<std::uint64_t>(graphics->generation()));
    QCOMPARE(hardware.frame->geometry().visibleSize, QSize(640, 360));
    QVERIFY(hardware.frame->geometry().sampleAspectRatioKnown);
    QCOMPARE(hardware.frame->geometry().sampleAspectRatio.numerator, 1);
    QCOMPARE(hardware.frame->geometry().sampleAspectRatio.denominator, 1);
    QVERIFY(hardware.frame->timing().pts);
    QCOMPARE(*hardware.frame->timing().pts, 0);
    QVERIFY(hardware.frame->timing().duration);
    QCOMPARE(*hardware.frame->timing().duration, 250);
    QCOMPARE(hardware.frame->timing().timeBase.numerator, 1);
    QCOMPARE(hardware.frame->timing().timeBase.denominator, 1000);
    QCOMPARE(hardware.frame->timing().timeBase.denominator / *hardware.frame->timing().duration, 4);
    QCOMPARE(hardware.frame->signal().componentDepth, 8);
    QCOMPARE(hardware.frame->signal().colorPrimaries, QStringLiteral("bt709"));
    QCOMPARE(hardware.frame->signal().transferFunction, QStringLiteral("bt709"));
    QCOMPARE(hardware.frame->signal().matrixCoefficients, QStringLiteral("bt709"));
    QCOMPARE(hardware.frame->signal().colorRange, QStringLiteral("tv"));
    QCOMPARE(hardware.frame->signal().chromaLocation, QStringLiteral("left"));

    FfmpegFirstFrameResult const software = decodeFirstVideoFrame(fixture, {
                                                                               .playbackGeneration = 22,
                                                                               .decoderRevision = 1,
                                                                               .frameId = 1,
                                                                           });
    QVERIFY2(software.isSuccess(), qPrintable(software.error));
    QVERIFY(!software.diagnostics.hardwareAccelerated);
    QCOMPARE(software.diagnostics.decodePath, QStringLiteral("Software"));
    QCOMPARE(software.frame->storage().kind, VideoFrameStorageKind::SoftwarePlanes);
    QCOMPARE(software.frame->storage().softwareFormat, QStringLiteral("yuv420p"));

    DecodedFrameCapture hardwareCapture = captureDecodedFrameAtSize(*graphics, hardware.frame, {1280, 720});
    QVERIFY2(hardwareCapture.isSuccess(), qPrintable(hardwareCapture.error));

    auto const freeFrame = [](AVFrame* frame) { av_frame_free(&frame); };
    std::unique_ptr<AVFrame, decltype(freeFrame)> malformedSource(av_frame_clone(&hardware.frame->ffmpegFrame()),
                                                                  freeFrame);
    QVERIFY(malformedSource);
    av_frame_remove_side_data(malformedSource.get(), AV_FRAME_DATA_DOVI_METADATA);
    AVFrameSideData* truncatedDovi = av_frame_new_side_data(malformedSource.get(), AV_FRAME_DATA_DOVI_METADATA, 1);
    QVERIFY(truncatedDovi);
    truncatedDovi->data[0] = 0;

    QString malformedError;
    auto const malformedFrame = DecodedVideoFrame::clone(*malformedSource,
                                                         {
                                                             .playbackGeneration = 23,
                                                             .decoderRevision = 1,
                                                             .frameId = 1,
                                                         },
                                                         hardware.frame->timing().timeBase, graphics->generation(),
                                                         std::nullopt, &malformedError);
    QVERIFY2(malformedFrame, qPrintable(malformedError));
    DecodedFrameCapture const malformedCapture = captureDecodedFrame(*graphics, malformedFrame);
    QVERIFY(!malformedCapture.isSuccess());
    QCOMPARE(malformedCapture.input.failure, VideoFrameImportFailure::General);
    QCOMPARE(malformedCapture.producer.failureKind, VideoFailureKind::General);
    QVERIFY(malformedCapture.error.contains(QStringLiteral("Dolby Vision metadata is truncated")));

    DecodedFrameCapture softwareCapture = captureDecodedFrameAtSize(*graphics, software.frame, {1280, 720});
    QVERIFY2(softwareCapture.isSuccess(), qPrintable(softwareCapture.error));
    QCOMPARE(hardwareCapture.readback.format, QRhiTexture::RGBA16F);
    QCOMPARE(hardwareCapture.readback.pixelSize, QSize(1280, 720));
    QCOMPARE(softwareCapture.readback.pixelSize, hardwareCapture.readback.pixelSize);

    QCOMPARE(hardwareCapture.input.path, VideoFrameImportPath::SameDeviceGpuCopy);
    QCOMPARE(hardwareCapture.input.knownCpuDownloadsPerFrame, 0U);
    QCOMPARE(hardwareCapture.input.knownCpuUploadsPerFrame, 0U);
    QCOMPARE(hardwareCapture.input.knownGpuCopiesPerFrame, 1U);
    QVERIFY(hardwareCapture.input.fallbackReason.contains(QStringLiteral("padding")));
    QVERIFY(hardwareCapture.input.nativeResource.contains(QStringLiteral("NV12")));
    QVERIFY(hardwareCapture.producer.inputPath.contains(QStringLiteral("same-device GPU copy")));
    QCOMPARE(hardwareCapture.producer.knownInputCpuTransfersPerInputFrame, 0U);
    QCOMPARE(hardwareCapture.producer.knownInputGpuCopiesPerInputFrame, 1U);
    QCOMPARE(hardwareCapture.producer.target.outputPath, VideoOutputPath::DirectRenderTarget);
    QCOMPARE(hardwareCapture.producer.target.knownOutputGpuCopiesPerRender, 0U);
    QCOMPARE(hardwareCapture.producer.target.knownOutputCpuTransfersPerRender, 0U);
    QVERIFY(!hardwareCapture.producer.target.synchronizationMode.isEmpty());

    std::unique_ptr<GraphicsDeviceDomain> incompatibleGraphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(incompatibleGraphics, "Could not create second D3D11 graphics domain");
    DecodedFrameCapture const incompatibleCapture = captureDecodedFrame(*incompatibleGraphics, hardware.frame);
    QVERIFY(!incompatibleCapture.isSuccess());
    QCOMPARE(incompatibleCapture.input.path, VideoFrameImportPath::Unavailable);
    QCOMPARE(incompatibleCapture.producer.failureKind, VideoFailureKind::HardwareFrameImportUnavailable);
    QVERIFY(incompatibleCapture.error.contains(QStringLiteral("stale graphics-device generation")));

    constexpr std::array samplePoints{
        QPoint{100, 90}, QPoint{320, 90}, QPoint{540, 90}, QPoint{100, 270}, QPoint{320, 270}, QPoint{540, 270},
    };
    for (QPoint const& sample : samplePoints) {
        FloatPixel const hardwarePixel = pixel(hardwareCapture.readback, sample.x(), sample.y());
        FloatPixel const softwarePixel = pixel(softwareCapture.readback, sample.x(), sample.y());
        compareNear(hardwarePixel.red, softwarePixel.red, 0.03f);
        compareNear(hardwarePixel.green, softwarePixel.green, 0.03f);
        compareNear(hardwarePixel.blue, softwarePixel.blue, 0.03f);
        compareNear(hardwarePixel.alpha, 1.0f, 0.002f);
    }
    compareBorder(hardwareCapture.readback, softwareCapture.readback, 0.035f);

    std::unique_ptr<AVFrame, decltype(freeFrame)> croppedHardwareSource(av_frame_clone(&hardware.frame->ffmpegFrame()),
                                                                        freeFrame);
    std::unique_ptr<AVFrame, decltype(freeFrame)> croppedSoftwareSource(av_frame_clone(&software.frame->ffmpegFrame()),
                                                                        freeFrame);
    QVERIFY(croppedHardwareSource);
    QVERIFY(croppedSoftwareSource);
    for (AVFrame* cropped : {croppedHardwareSource.get(), croppedSoftwareSource.get()}) {
        cropped->crop_left = 2;
        cropped->crop_top = 2;
        cropped->crop_right = 2;
        cropped->crop_bottom = 2;
    }

    QString croppedError;
    std::shared_ptr<DecodedVideoFrame const> const croppedHardwareFrame = DecodedVideoFrame::clone(
        *croppedHardwareSource,
        {
            .playbackGeneration = 26,
            .decoderRevision = 1,
            .frameId = 1,
        },
        hardware.frame->timing().timeBase, graphics->generation(), std::nullopt, &croppedError);
    QVERIFY2(croppedHardwareFrame, qPrintable(croppedError));
    std::shared_ptr<DecodedVideoFrame const> const croppedSoftwareFrame =
        DecodedVideoFrame::clone(*croppedSoftwareSource,
                                 {
                                     .playbackGeneration = 27,
                                     .decoderRevision = 1,
                                     .frameId = 1,
                                 },
                                 software.frame->timing().timeBase, std::nullopt, std::nullopt, &croppedError);
    QVERIFY2(croppedSoftwareFrame, qPrintable(croppedError));
    QCOMPARE(croppedHardwareFrame->geometry().visibleSize, QSize(636, 356));

    DecodedFrameCapture const croppedHardwareCapture =
        captureDecodedFrameAtSize(*graphics, croppedHardwareFrame, {1272, 712});
    QVERIFY2(croppedHardwareCapture.isSuccess(), qPrintable(croppedHardwareCapture.error));
    DecodedFrameCapture const croppedSoftwareCapture =
        captureDecodedFrameAtSize(*graphics, croppedSoftwareFrame, {1272, 712});
    QVERIFY2(croppedSoftwareCapture.isSuccess(), qPrintable(croppedSoftwareCapture.error));
    QCOMPARE(croppedHardwareCapture.input.path, VideoFrameImportPath::SameDeviceGpuCopy);
    QCOMPARE(croppedHardwareCapture.input.knownGpuCopiesPerFrame, 1U);
    QVERIFY(croppedHardwareCapture.input.nativeResource.contains(QStringLiteral("crop 2,2")));
    QVERIFY(croppedHardwareCapture.input.nativeResource.contains(QStringLiteral("636x356")));
    compareBorder(croppedHardwareCapture.readback, croppedSoftwareCapture.readback, 0.035f);
    constexpr std::array cropSensitivePoints{QPoint{424, 200}, QPoint{850, 200}, QPoint{200, 358}};
    for (QPoint const& sample : cropSensitivePoints) {
        FloatPixel const hardwarePixel = pixel(croppedHardwareCapture.readback, sample.x(), sample.y());
        FloatPixel const softwarePixel = pixel(croppedSoftwareCapture.readback, sample.x(), sample.y());
        compareNear(hardwarePixel.red, softwarePixel.red, 0.035f);
        compareNear(hardwarePixel.green, softwarePixel.green, 0.035f);
        compareNear(hardwarePixel.blue, softwarePixel.blue, 0.035f);
    }

    croppedHardwareSource->crop_right = 1;
    std::shared_ptr<DecodedVideoFrame const> const oddWidthHardwareFrame = DecodedVideoFrame::clone(
        *croppedHardwareSource,
        {
            .playbackGeneration = 28,
            .decoderRevision = 1,
            .frameId = 1,
        },
        hardware.frame->timing().timeBase, graphics->generation(), std::nullopt, &croppedError);
    QVERIFY2(oddWidthHardwareFrame, qPrintable(croppedError));
    QCOMPARE(oddWidthHardwareFrame->geometry().visibleSize, QSize(637, 356));
    DecodedFrameCapture const oddWidthCapture = captureDecodedFrame(*graphics, oddWidthHardwareFrame);
    QVERIFY(!oddWidthCapture.isSuccess());
    QCOMPARE(oddWidthCapture.input.failure, VideoFrameImportFailure::NativeHardwareImportUnavailable);
    QVERIFY(oddWidthCapture.error.contains(QStringLiteral("visible rectangle is not chroma-aligned")));
#endif
}

void FfmpegFirstFrameTest::d3d11P010HardwareDecodeSafeImport() {
#ifndef Q_OS_WIN
    QSKIP("D3D11VA P010 import is Windows-specific");
#else
    QString const fixture = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/hdr10-pq-hevc.hevc");
    QString const manifest = QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/hdr10-pq-hevc.toml");
    QCOMPARE(fixtureHash(fixture), expectedFixtureHash(manifest));

    std::unique_ptr<GraphicsDeviceDomain> graphics = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphics, "Could not create D3D11 graphics domain");
    VideoHardwareDecodeCapability const& hardwareDecode = graphics->videoDecodeCapability();
    if (!hardwareDecode.isAvailable()) {
        QString const reason = QStringLiteral("D3D11VA unavailable: %1").arg(hardwareDecode.unavailableReason);
        if (qEnvironmentVariableIntValue("SUNPLAYER_REQUIRE_D3D11VA") != 0) {
            QFAIL(qPrintable(reason));
        }
        QSKIP(qPrintable(reason));
    }

    FfmpegFirstFrameResult const hardware = decodeFirstVideoFrame(fixture,
                                                                  {
                                                                      .playbackGeneration = 24,
                                                                      .decoderRevision = 1,
                                                                      .frameId = 1,
                                                                  },
                                                                  hardwareDecode);
    QVERIFY2(hardware.isSuccess(), qPrintable(hardware.error));
    QVERIFY(hardware.diagnostics.hardwareAccelerated);
    QCOMPARE(hardware.diagnostics.decodePath, QStringLiteral("D3D11VA"));
    QCOMPARE(hardware.frame->storage().kind, VideoFrameStorageKind::D3D11Surface);
    QCOMPARE(hardware.frame->storage().softwareFormat, QStringLiteral("p010le"));
    QCOMPARE(hardware.frame->signal().componentDepth, 10);

    FfmpegFirstFrameResult const software = decodeFirstVideoFrame(fixture, {
                                                                               .playbackGeneration = 25,
                                                                               .decoderRevision = 1,
                                                                               .frameId = 1,
                                                                           });
    QVERIFY2(software.isSuccess(), qPrintable(software.error));
    QVERIFY(!software.diagnostics.hardwareAccelerated);

    DecodedFrameCapture const hardwareCapture = captureDecodedFrameAtSize(*graphics, hardware.frame, {512, 288});
    QVERIFY2(hardwareCapture.isSuccess(), qPrintable(hardwareCapture.error));
    DecodedFrameCapture const softwareCapture = captureDecodedFrameAtSize(*graphics, software.frame, {512, 288});
    QVERIFY2(softwareCapture.isSuccess(), qPrintable(softwareCapture.error));

    QCOMPARE(hardwareCapture.input.path, VideoFrameImportPath::SameDeviceGpuCopy);
    QCOMPARE(hardwareCapture.input.knownCpuDownloadsPerFrame, 0U);
    QCOMPARE(hardwareCapture.input.knownCpuUploadsPerFrame, 0U);
    QCOMPARE(hardwareCapture.input.knownGpuCopiesPerFrame, 1U);
    QVERIFY(hardwareCapture.input.fallbackReason.contains(QStringLiteral("padding")));
    QVERIFY(hardwareCapture.input.nativeResource.contains(QStringLiteral("P010")));
    QVERIFY(hardwareCapture.input.nativeResource.contains(QStringLiteral("256x144")));
    QCOMPARE(hardwareCapture.producer.knownInputCpuTransfersPerInputFrame, 0U);
    QCOMPARE(hardwareCapture.producer.knownInputGpuCopiesPerInputFrame, 1U);
    compareBorder(hardwareCapture.readback, softwareCapture.readback, 0.04f);
#endif
}

// Metal device creation needs the macOS GUI application lifecycle. The
// Windows offscreen D3D11 path remains independent of a window system.
#ifdef Q_OS_MACOS
QTEST_MAIN(FfmpegFirstFrameTest)
#else
QTEST_GUILESS_MAIN(FfmpegFirstFrameTest)
#endif
#include "tst_FfmpegFirstFrame.moc"
