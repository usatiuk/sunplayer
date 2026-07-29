#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#include <QtTest>
#include <QCoreApplication>
#include <QtCore/qfloat16.h>
#include <rhi/qrhi.h>

#include "graphics/GraphicsBackendFactory.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "presentation/HdrCompositor.h"
#include "video/DiagnosticVideoSource.h"
#include "video/RenderedVideoProducer.h"
#include "video/RenderedVideoSurface.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr QSize videoSize{16, 12};
constexpr QSize outputSize{24, 20};
constexpr int videoOriginX = 4;
constexpr int videoOriginY = 4;
constexpr float sourcePeak = 4.0f;

struct FloatPixel {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

struct BytePixel {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

RenderedVideoSurfaceState surfaceState() {
    RenderedVideoSurfaceState state;
    state.description.pixelSize = videoSize;
    state.description.pixelFormat =
        RenderedVideoPixelFormat::Rgba16Float;
    state.description.colorSpace =
        RenderedVideoColorSpace::LinearSrgb;
    state.description.luminance =
        RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative;
    state.description.alphaMode = RenderedVideoAlphaMode::Opaque;
    state.description.referenceWhiteNits = 80.0f;
    state.description.targetPeakHeadroom = sourcePeak;
    state.graphicsDeviceGeneration = 1;
    state.displayTargetRevision = 1;
    state.contentRevision = 1;
    return state;
}

int storageY(const QRhi &rhi, int height, int canonicalY) {
    return rhi.isYUpInFramebuffer()
        ? height - 1 - canonicalY
        : canonicalY;
}

FloatPixel readFloatPixel(const QRhiReadbackResult &result,
                          const QRhi &rhi,
                          int x,
                          int y) {
    Q_ASSERT(result.format == QRhiTexture::RGBA16F);
    Q_ASSERT(result.pixelSize.width() > x && x >= 0);
    Q_ASSERT(result.pixelSize.height() > y && y >= 0);
    Q_ASSERT(result.data.size()
             >= result.pixelSize.width() * result.pixelSize.height() * 8);

    const int row = storageY(rhi, result.pixelSize.height(), y);
    const qsizetype offset =
        (row * result.pixelSize.width() + x) * 4 * sizeof(qfloat16);
    std::array<qfloat16, 4> channels;
    std::memcpy(channels.data(), result.data.constData() + offset,
                sizeof(channels));
    return {
        static_cast<float>(channels[0]),
        static_cast<float>(channels[1]),
        static_cast<float>(channels[2]),
        static_cast<float>(channels[3]),
    };
}

BytePixel readBytePixel(const QRhiReadbackResult &result,
                        const QRhi &rhi,
                        int x,
                        int y) {
    Q_ASSERT(result.format == QRhiTexture::RGBA8);
    Q_ASSERT(result.pixelSize.width() > x && x >= 0);
    Q_ASSERT(result.pixelSize.height() > y && y >= 0);
    Q_ASSERT(result.data.size()
             >= result.pixelSize.width() * result.pixelSize.height() * 4);

    const int row = storageY(rhi, result.pixelSize.height(), y);
    const qsizetype offset =
        (row * result.pixelSize.width() + x) * 4;
    const auto *pixel = reinterpret_cast<const uchar *>(
        result.data.constData() + offset);
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

float smoothStep(float value) {
    return value * value * (3.0f - 2.0f * value);
}

float expectedRamp(int x, int width = videoSize.width()) {
    const float u =
        (static_cast<float>(x) + 0.5f) / width;
    return 0.02f + (sourcePeak - 0.02f) * smoothStep(u);
}

float expectedStep(int x) {
    const float u =
        (static_cast<float>(x) + 0.5f) / videoSize.width();
    return std::floor(u * 8.0f) / 7.0f * sourcePeak;
}

float srgbToLinear(float value) {
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value) {
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

int encodedByte(float linear) {
    const float encoded = linearToSrgb(std::clamp(linear, 0.0f, 1.0f));
    return static_cast<int>(std::lround(encoded * 255.0f));
}

void compareNear(float actual, float expected, float tolerance) {
    QVERIFY2(std::abs(actual - expected) <= tolerance,
             qPrintable(QStringLiteral("actual %1, expected %2 ± %3")
                 .arg(actual, 0, 'g', 8)
                 .arg(expected, 0, 'g', 8)
                 .arg(tolerance, 0, 'g', 8)));
}

void compareByteNear(int actual, int expected, int tolerance = 2) {
    QVERIFY2(std::abs(actual - expected) <= tolerance,
             qPrintable(QStringLiteral("actual %1, expected %2 ± %3")
                 .arg(actual)
                 .arg(expected)
                 .arg(tolerance)));
}
}

class QrhiCompositorTest final : public QObject {
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
    void realD3d11ProducerAndCompositionReadback();
};

void QrhiCompositorTest::realD3d11ProducerAndCompositionReadback() {
#ifndef Q_OS_WIN
    QSKIP("The current supported QRhi integration test is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphicsDevice, "Could not create the D3D11 graphics domain");
    QRhi *const rhi = &graphicsDevice->rhi();
    QVERIFY(graphicsDevice->generation() != 0);
    QCOMPARE(graphicsDevice->backend(), GraphicsBackend::D3D11);
    QVERIFY(graphicsDevice->diagnostics().isValid());
    const bool floatCaptureSupported = rhi->isTextureFormatSupported(
        QRhiTexture::RGBA16F,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    qInfo().noquote()
        << "QRhi backend:" << rhi->backendName()
        << "| device:" << rhi->driverInfo().deviceName
        << "| requested RGBA16F capture format:"
        << (floatCaptureSupported ? "accepted" : "unavailable");
    QVERIFY(floatCaptureSupported);

    DiagnosticVideoSource source(VideoTargetReadback::Enabled);
    source.setSourcePeakHeadroom(sourcePeak);
    source.setToneMappingEnabled(false);
    source.setAnimatePattern(false);
    std::unique_ptr<RenderedVideoProducer> producer =
        source.createProducer(*graphicsDevice);
    QVERIFY(producer);
    const RenderedVideoProducerDiagnostics unprovisionedDiagnostics =
        producer->diagnostics();
    QVERIFY(unprovisionedDiagnostics.isValid());
    QCOMPARE(
        unprovisionedDiagnostics.target.outputPath,
        VideoOutputPath::Unavailable);
    QVERIFY(!unprovisionedDiagnostics.target.fallbackReason.isEmpty());
    const RenderedVideoSurfaceState requestedState = surfaceState();
    QCOMPARE(
        producer->ensureSurface(requestedState),
        VideoOperationResult::Ready);
    QVERIFY(producer->needsRender(requestedState));
    const RenderedVideoProducerDiagnostics producerDiagnostics =
        producer->diagnostics();
    QVERIFY(producerDiagnostics.isValid());
    QCOMPARE(
        producerDiagnostics.target.outputPath,
        VideoOutputPath::DirectRenderTarget);
    QCOMPARE(producerDiagnostics.target.knownGpuCopiesPerRender, 0U);
    QCOMPARE(producerDiagnostics.target.knownCpuTransfersPerRender, 0U);
    QVERIFY(producerDiagnostics.target.fallbackReason.isEmpty());

    std::unique_ptr<QRhiTexture> uiTexture(rhi->newTexture(
        QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(uiTexture->create());

    std::unique_ptr<QRhiTexture> outputTexture(rhi->newTexture(
        QRhiTexture::RGBA8,
        outputSize,
        1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(outputTexture->create());

    const QRhiTextureRenderTargetDescription outputDescription(
        QRhiColorAttachment(outputTexture.get()));
    std::unique_ptr<QRhiRenderPassDescriptor> outputRenderPass;
    std::unique_ptr<QRhiTextureRenderTarget> outputTarget(
        rhi->newTextureRenderTarget(outputDescription));
    outputRenderPass.reset(
        outputTarget->newCompatibleRenderPassDescriptor());
    outputTarget->setRenderPassDescriptor(outputRenderPass.get());
    QVERIFY(outputTarget->create());

    std::unique_ptr<QRhiTexture> linearOutputTexture(rhi->newTexture(
        QRhiTexture::RGBA16F,
        outputSize,
        1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(linearOutputTexture->create());

    const QRhiTextureRenderTargetDescription linearOutputDescription(
        QRhiColorAttachment(linearOutputTexture.get()));
    std::unique_ptr<QRhiRenderPassDescriptor> linearOutputRenderPass;
    std::unique_ptr<QRhiTextureRenderTarget> linearOutputTarget(
        rhi->newTextureRenderTarget(linearOutputDescription));
    linearOutputRenderPass.reset(
        linearOutputTarget->newCompatibleRenderPassDescriptor());
    linearOutputTarget->setRenderPassDescriptor(
        linearOutputRenderPass.get());
    QVERIFY(linearOutputTarget->create());

    HdrCompositor compositor(*rhi);
    QCOMPARE(
        compositor.initialize(
            *outputRenderPass,
            &producer->textureForComposition(),
            *uiTexture),
        HdrCompositor::ResourceResult::Ready);
    HdrCompositor linearOutputCompositor(*rhi);
    QCOMPARE(
        linearOutputCompositor.initialize(
            *linearOutputRenderPass,
            &producer->textureForComposition(),
            *uiTexture),
        HdrCompositor::ResourceResult::Ready);

    HdrCompositorParameters compositorParameters;
    compositorParameters.viewportSize = {
        static_cast<float>(outputSize.width()),
        static_cast<float>(outputSize.height()),
    };
    compositorParameters.videoOrigin = {
        static_cast<float>(videoOriginX),
        static_cast<float>(videoOriginY),
    };
    compositorParameters.videoSize = {
        static_cast<float>(videoSize.width()),
        static_cast<float>(videoSize.height()),
    };
    compositorParameters.sdrScale = 1.0f;
    compositorParameters.ndcYUp = rhi->isYUpInNDC() ? 1.0f : 0.0f;
    compositorParameters.linearOutput = 0.0f;

    QRhiCommandBuffer *commandBuffer = nullptr;
    QCOMPARE(
        rhi->beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QVERIFY(commandBuffer);

    const QByteArray transparentUi(4, '\0');
    QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(
        uiTexture.get(),
        QRhiTextureUploadDescription(QRhiTextureUploadEntry(
            0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    commandBuffer->resourceUpdate(updates);

    QCOMPARE(
        producer->render(*commandBuffer, requestedState),
        VideoOperationResult::Ready);
    QVERIFY(producer->needsRender(requestedState));
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);
    compositor.render(
        *commandBuffer,
        *outputTarget,
        outputSize,
        compositorParameters);

    bool videoReadbackCompleted = false;
    bool outputReadbackCompleted = false;
    QRhiReadbackResult videoReadback;
    QRhiReadbackResult outputReadback;
    videoReadback.completed = [&videoReadbackCompleted] {
        videoReadbackCompleted = true;
    };
    outputReadback.completed = [&outputReadbackCompleted] {
        outputReadbackCompleted = true;
    };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(
            &producer->textureForComposition()),
        &videoReadback);
    updates->readBackTexture(
        QRhiReadbackDescription(outputTexture.get()), &outputReadback);
    commandBuffer->resourceUpdate(updates);

    const QRhi::FrameOpResult firstFrameResult =
        rhi->endOffscreenFrame();
    if (firstFrameResult == QRhi::FrameOpSuccess) {
        producer->submissionAccepted();
        producer->commitPendingRender();
    } else {
        producer->submissionAborted();
        producer->discardPendingRender();
    }
    QCOMPARE(firstFrameResult, QRhi::FrameOpSuccess);
    QVERIFY(!producer->needsRender(requestedState));
    QVERIFY(videoReadbackCompleted);
    QVERIFY(outputReadbackCompleted);
    QCOMPARE(videoReadback.pixelSize, videoSize);
    QCOMPARE(videoReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(
        videoReadback.data.size(),
        qsizetype(videoSize.width() * videoSize.height() * 8));
    QCOMPARE(outputReadback.pixelSize, outputSize);
    QCOMPARE(outputReadback.format, QRhiTexture::RGBA8);
    QCOMPARE(
        outputReadback.data.size(),
        qsizetype(outputSize.width() * outputSize.height() * 4));
    qInfo("RGBA16F producer capture completed with validated byte count");

    constexpr int sampleX = 2;
    const FloatPixel top = readFloatPixel(
        videoReadback, *rhi, sampleX, 1);
    const FloatPixel middle = readFloatPixel(
        videoReadback, *rhi, sampleX, 5);
    const FloatPixel bottom = readFloatPixel(
        videoReadback, *rhi, sampleX, 10);
    compareNear(top.r, expectedRamp(sampleX), 0.002f);
    compareNear(top.g, top.r, 0.001f);
    compareNear(top.b, top.r, 0.001f);
    compareNear(top.a, 1.0f, 0.001f);
    QVERIFY(std::abs(middle.r - middle.g) > 0.01f
            || std::abs(middle.g - middle.b) > 0.01f);
    compareNear(bottom.r, expectedStep(sampleX), 0.002f);
    compareNear(bottom.g, bottom.r, 0.001f);
    compareNear(bottom.b, bottom.r, 0.001f);
    compareNear(bottom.a, 1.0f, 0.001f);
    QVERIFY(std::abs(top.r - bottom.r) > 0.1f);

    constexpr int extendedSampleX = 12;
    const float expectedExtended = expectedRamp(extendedSampleX);
    QVERIFY(expectedExtended > 1.0f);
    const FloatPixel extendedProducerPixel = readFloatPixel(
        videoReadback, *rhi, extendedSampleX, 1);
    QVERIFY(extendedProducerPixel.r > 1.0f);
    compareNear(
        extendedProducerPixel.r, expectedExtended, 0.004f);
    compareNear(
        extendedProducerPixel.g, expectedExtended, 0.004f);
    compareNear(
        extendedProducerPixel.b, expectedExtended, 0.004f);

    const BytePixel background =
        readBytePixel(outputReadback, *rhi, 1, 1);
    compareByteNear(background.r, 17);
    compareByteNear(background.g, 19);
    compareByteNear(background.b, 24);
    QCOMPARE(background.a, 255);

    const BytePixel composedVideo = readBytePixel(
        outputReadback,
        *rhi,
        videoOriginX + sampleX,
        videoOriginY + 1);
    const int expectedVideoByte = encodedByte(expectedRamp(sampleX));
    compareByteNear(composedVideo.r, expectedVideoByte);
    compareByteNear(composedVideo.g, expectedVideoByte);
    compareByteNear(composedVideo.b, expectedVideoByte);
    QCOMPARE(composedVideo.a, 255);

    // A second frame changes only presentation and UI state. It must reuse the
    // submitted video surface while exercising extended-linear output,
    // non-unity SDR scaling, and premultiplied encoded UI.
    QCOMPARE(
        rhi->beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QByteArray translucentRedUi(4, '\0');
    translucentRedUi[0] = static_cast<char>(64);
    translucentRedUi[3] = static_cast<char>(128);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(
        uiTexture.get(),
        QRhiTextureUploadDescription(QRhiTextureUploadEntry(
            0, 0,
            QRhiTextureSubresourceUploadDescription(translucentRedUi))));
    commandBuffer->resourceUpdate(updates);
    QVERIFY(!producer->needsRender(requestedState));
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);
    HdrCompositorParameters linearParameters = compositorParameters;
    constexpr float linearSdrScale = 1.5f;
    linearParameters.sdrScale = linearSdrScale;
    linearParameters.linearOutput = 1.0f;
    linearOutputCompositor.render(
        *commandBuffer,
        *linearOutputTarget,
        outputSize,
        linearParameters);

    bool linearReadbackCompleted = false;
    QRhiReadbackResult linearReadback;
    linearReadback.completed = [&linearReadbackCompleted] {
        linearReadbackCompleted = true;
    };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(linearOutputTexture.get()),
        &linearReadback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    QVERIFY(linearReadbackCompleted);
    QCOMPARE(linearReadback.pixelSize, outputSize);
    QCOMPARE(linearReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(
        linearReadback.data.size(),
        qsizetype(outputSize.width() * outputSize.height() * 8));

    const float alpha = 128.0f / 255.0f;
    const float encodedStraightRed =
        (64.0f / 255.0f) / alpha;
    const std::array<float, 3> backgroundEncoded{
        17.0f / 255.0f,
        19.0f / 255.0f,
        24.0f / 255.0f,
    };
    const auto expectedLinearBlend =
        [alpha](float baseLinear, float encodedUi, float scale) {
            return (
                srgbToLinear(encodedUi) * alpha
                + baseLinear * (1.0f - alpha))
                * scale;
        };

    const FloatPixel blendedBackground =
        readFloatPixel(linearReadback, *rhi, 1, 1);
    compareNear(
        blendedBackground.r,
        expectedLinearBlend(
            srgbToLinear(backgroundEncoded[0]),
            encodedStraightRed,
            linearSdrScale),
        0.002f);
    compareNear(
        blendedBackground.g,
        expectedLinearBlend(
            srgbToLinear(backgroundEncoded[1]),
            0.0f,
            linearSdrScale),
        0.002f);
    compareNear(
        blendedBackground.b,
        expectedLinearBlend(
            srgbToLinear(backgroundEncoded[2]),
            0.0f,
            linearSdrScale),
        0.002f);
    compareNear(blendedBackground.a, 1.0f, 0.001f);

    const FloatPixel reusedExtendedVideo = readFloatPixel(
        linearReadback,
        *rhi,
        videoOriginX + extendedSampleX,
        videoOriginY + 1);
    const float expectedExtendedRed = expectedLinearBlend(
        expectedExtended, encodedStraightRed, linearSdrScale);
    const float expectedExtendedGreenBlue = expectedLinearBlend(
        expectedExtended, 0.0f, linearSdrScale);
    QVERIFY(expectedExtendedRed > 1.0f);
    QVERIFY(expectedExtendedGreenBlue > 1.0f);
    compareNear(
        reusedExtendedVideo.r, expectedExtendedRed, 0.01f);
    compareNear(
        reusedExtendedVideo.g, expectedExtendedGreenBlue, 0.01f);
    compareNear(
        reusedExtendedVideo.b, expectedExtendedGreenBlue, 0.01f);
    compareNear(reusedExtendedVideo.a, 1.0f, 0.001f);

    // The compositor owns a valid fallback binding when no page publishes a
    // visible video viewport. No video surface is prepared or sampled.
    QCOMPARE(
        compositor.setTextures(nullptr, *uiTexture),
        HdrCompositor::ResourceResult::Ready);
    QCOMPARE(
        rhi->beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(
        uiTexture.get(),
        QRhiTextureUploadDescription(QRhiTextureUploadEntry(
            0, 0, QRhiTextureSubresourceUploadDescription(
                transparentUi))));
    commandBuffer->resourceUpdate(updates);
    HdrCompositorParameters hiddenVideoParameters =
        compositorParameters;
    hiddenVideoParameters.videoSize = {0.0f, 0.0f};
    compositor.render(
        *commandBuffer,
        *outputTarget,
        outputSize,
        hiddenVideoParameters);

    bool hiddenVideoReadbackCompleted = false;
    QRhiReadbackResult hiddenVideoReadback;
    hiddenVideoReadback.completed =
        [&hiddenVideoReadbackCompleted] {
            hiddenVideoReadbackCompleted = true;
        };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(outputTexture.get()),
        &hiddenVideoReadback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->commitPendingRender();
    QVERIFY(hiddenVideoReadbackCompleted);
    QCOMPARE(hiddenVideoReadback.pixelSize, outputSize);
    QCOMPARE(hiddenVideoReadback.format, QRhiTexture::RGBA8);
    QCOMPARE(
        hiddenVideoReadback.data.size(),
        qsizetype(outputSize.width() * outputSize.height() * 4));
    const BytePixel hiddenVideoPixel = readBytePixel(
        hiddenVideoReadback,
        *rhi,
        videoOriginX + sampleX,
        videoOriginY + 1);
    compareByteNear(hiddenVideoPixel.r, 17);
    compareByteNear(hiddenVideoPixel.g, 19);
    compareByteNear(hiddenVideoPixel.b, 24);
    QCOMPARE(hiddenVideoPixel.a, 255);

    // Submission tracking and content-state promotion are separate. Discarding
    // a rendered state after an accepted submission must leave it dirty.
    RenderedVideoSurfaceState resubmittedState = requestedState;
    ++resubmittedState.contentRevision;
    QCOMPARE(
        rhi->beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QCOMPARE(
        producer->render(*commandBuffer, resubmittedState),
        VideoOperationResult::Ready);
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->discardPendingRender();
    QVERIFY(producer->needsRender(resubmittedState));

    QCOMPARE(
        rhi->beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QCOMPARE(
        producer->render(*commandBuffer, resubmittedState),
        VideoOperationResult::Ready);
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->commitPendingRender();
    QVERIFY(!producer->needsRender(resubmittedState));

    // Recreate texture storage through the same target wrapper, observe the
    // revision, rebind the compositor as production does, and capture again.
    constexpr QSize resizedVideoSize{8, 6};
    RenderedVideoSurfaceState resizedState = resubmittedState;
    resizedState.description.pixelSize = resizedVideoSize;
    ++resizedState.contentRevision;
    const std::uint64_t previousTextureRevision =
        producer->compositionTextureRevision();
    QCOMPARE(
        producer->ensureSurface(resizedState),
        VideoOperationResult::Ready);
    QVERIFY(
        producer->compositionTextureRevision()
        > previousTextureRevision);
    QCOMPARE(
        producer->textureForComposition().pixelSize(),
        resizedVideoSize);
    QVERIFY(producer->needsRender(resizedState));
    QCOMPARE(
        compositor.setTextures(
            &producer->textureForComposition(), *uiTexture),
        HdrCompositor::ResourceResult::Ready);

    QCOMPARE(
        rhi->beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(
        uiTexture.get(),
        QRhiTextureUploadDescription(QRhiTextureUploadEntry(
            0, 0, QRhiTextureSubresourceUploadDescription(
                transparentUi))));
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(
        producer->render(*commandBuffer, resizedState),
        VideoOperationResult::Ready);
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);
    HdrCompositorParameters resizedParameters = compositorParameters;
    resizedParameters.videoSize = {
        static_cast<float>(resizedVideoSize.width()),
        static_cast<float>(resizedVideoSize.height()),
    };
    compositor.render(
        *commandBuffer,
        *outputTarget,
        outputSize,
        resizedParameters);

    bool resizedVideoReadbackCompleted = false;
    bool resizedOutputReadbackCompleted = false;
    QRhiReadbackResult resizedVideoReadback;
    QRhiReadbackResult resizedOutputReadback;
    resizedVideoReadback.completed =
        [&resizedVideoReadbackCompleted] {
            resizedVideoReadbackCompleted = true;
        };
    resizedOutputReadback.completed =
        [&resizedOutputReadbackCompleted] {
            resizedOutputReadbackCompleted = true;
        };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(
            &producer->textureForComposition()),
        &resizedVideoReadback);
    updates->readBackTexture(
        QRhiReadbackDescription(outputTexture.get()),
        &resizedOutputReadback);
    commandBuffer->resourceUpdate(updates);
    const QRhi::FrameOpResult resizedFrameResult =
        rhi->endOffscreenFrame();
    if (resizedFrameResult == QRhi::FrameOpSuccess) {
        producer->submissionAccepted();
        producer->commitPendingRender();
    } else {
        producer->submissionAborted();
        producer->discardPendingRender();
    }
    QCOMPARE(resizedFrameResult, QRhi::FrameOpSuccess);
    QVERIFY(resizedVideoReadbackCompleted);
    QVERIFY(resizedOutputReadbackCompleted);
    QCOMPARE(resizedVideoReadback.pixelSize, resizedVideoSize);
    QCOMPARE(resizedVideoReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(
        resizedVideoReadback.data.size(),
        qsizetype(
            resizedVideoSize.width()
            * resizedVideoSize.height() * 8));
    QCOMPARE(resizedOutputReadback.pixelSize, outputSize);
    QCOMPARE(resizedOutputReadback.format, QRhiTexture::RGBA8);
    QCOMPARE(
        resizedOutputReadback.data.size(),
        qsizetype(outputSize.width() * outputSize.height() * 4));
    QVERIFY(!producer->needsRender(resizedState));

    const FloatPixel resizedProducerPixel =
        readFloatPixel(resizedVideoReadback, *rhi, sampleX, 1);
    compareNear(
        resizedProducerPixel.r,
        expectedRamp(sampleX, resizedVideoSize.width()),
        0.002f);
    const BytePixel resizedComposedPixel = readBytePixel(
        resizedOutputReadback,
        *rhi,
        videoOriginX + sampleX,
        videoOriginY + 1);
    compareByteNear(
        resizedComposedPixel.r,
        encodedByte(expectedRamp(
            sampleX, resizedVideoSize.width())));
#endif
}

// QTEST_MAIN would instantiate QGuiApplication because this target links Qt
// Gui for QRhi. This test is deliberately headless but needs QCoreApplication
// lifetime for QRhi's process services and orderly teardown.
QTEST_GUILESS_MAIN(QrhiCompositorTest)
#include "tst_QrhiCompositor.moc"
