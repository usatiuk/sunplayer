#include <array>
#include <cmath>
#include <memory>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QtCore/qfloat16.h>
#include <QtTest>
#include <rhi/qrhi.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "graphics/GraphicsBackendFactory.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "media/DecodedVideoFrame.h"
#include "media/FfmpegFirstFrameDecoder.h"
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
    const auto *values =
        reinterpret_cast<const qfloat16 *>(
            readback.data.constData());
    const qsizetype index =
        (static_cast<qsizetype>(y)
         * readback.pixelSize.width()
         + x) * 4;
    return {
        static_cast<float>(values[index]),
        static_cast<float>(values[index + 1]),
        static_cast<float>(values[index + 2]),
        static_cast<float>(values[index + 3]),
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
        float referenceWhiteNits = 203.0f) {
    return {
        .description = {
            .pixelSize = {4, 4},
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
        .displayTargetRevision = 1,
        .contentRevision = contentRevision,
    };
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
    void realDemuxDecodeImportAndComposition();
};

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
    ++changedTarget.displayTargetRevision;
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

// This target links Qt Gui for headless QRhi. QTEST_MAIN would create a
// QGuiApplication, while the offscreen test only needs QCoreApplication.
QTEST_GUILESS_MAIN(FfmpegFirstFrameTest)
#include "tst_FfmpegFirstFrame.moc"
