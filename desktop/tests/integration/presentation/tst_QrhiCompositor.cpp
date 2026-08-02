#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QtCore/qfloat16.h>
#include <libplacebo/colorspace.h>
#include <rhi/qrhi.h>

#include "graphics/GraphicsBackendFactory.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "presentation/HdrCompositor.h"
#include "video/DiagnosticVideoSource.h"
#include "video/LibplaceboDiagnosticVideoProducer.h"
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
constexpr float physicalTargetPeakNits = 600.0f;
constexpr float masteredSourcePeakNits = 1000.0f;
constexpr float sourcePeak =
    masteredSourcePeakNits / PL_COLOR_SDR_WHITE;
constexpr float exactReferenceWhitePatternPeak = 7.0f;
constexpr float tau = 6.28318530718f;

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
    state.description.targetMinimumLuminanceKnown = true;
    state.description.targetMinimumLuminanceNits = 0.005f;
    state.description.targetPeakHeadroom = sourcePeak;
    state.graphicsDeviceGeneration = 1;
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

float analyticRamp(
        int x,
        int width,
        float peakHeadroom) {
    const float u =
        (static_cast<float>(x) + 0.5f) / width;
    return 0.02f
        + (peakHeadroom - 0.02f) * smoothStep(u);
}

float analyticStep(
        int x,
        int width,
        float peakHeadroom) {
    const float u =
        (static_cast<float>(x) + 0.5f) / width;
    return std::floor(u * 8.0f) / 7.0f
        * peakHeadroom;
}

std::array<float, 3> analyticSpectrum(
        int x,
        int width,
        float peakHeadroom,
        float phase = 0.0f) {
    const float u =
        (static_cast<float>(x) + 0.5f) / width;
    const float ramp =
        analyticRamp(x, width, peakHeadroom);
    return {
        (0.5f + 0.5f * std::cos(
            tau * (u + phase))) * ramp,
        (0.5f + 0.5f * std::cos(
            tau * (u + phase + 0.33f))) * ramp,
        (0.5f + 0.5f * std::cos(
            tau * (u + phase + 0.67f))) * ramp,
    };
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
    void libplaceboD3d11SurfaceAndCompositionReadback();
    void libplaceboAnimatedDiagnosticThroughput();
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

    DiagnosticVideoSource source(
        VideoProducerApi::Qrhi,
        VideoTargetReadback::Enabled);
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
    QCOMPARE(
        producerDiagnostics.knownInputCpuTransfersPerInputFrame,
        0U);
    QCOMPARE(
        producerDiagnostics.target.knownOutputGpuCopiesPerRender,
        0U);
    QCOMPARE(
        producerDiagnostics.target.knownOutputCpuTransfersPerRender,
        0U);
    QVERIFY(producerDiagnostics.target.fallbackReason.isEmpty());

    std::unique_ptr<QRhiTexture> uiTexture(rhi->newTexture(
        QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(uiTexture->create());
    std::unique_ptr<QRhiTexture> subtitleTexture(rhi->newTexture(
        QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(subtitleTexture->create());

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
            subtitleTexture.get(),
            *uiTexture),
        HdrCompositor::ResourceResult::Ready);
    HdrCompositor linearOutputCompositor(*rhi);
    QCOMPARE(
        linearOutputCompositor.initialize(
            *linearOutputRenderPass,
            &producer->textureForComposition(),
            subtitleTexture.get(),
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
    updates->uploadTexture(
        subtitleTexture.get(),
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
    compareByteNear(background.r, 0);
    compareByteNear(background.g, 0);
    compareByteNear(background.b, 0);
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

    // A second frame changes only presentation, subtitle, and UI state. It
    // must reuse the submitted video surface while proving the intended layer
    // order: video, then premultiplied sRGB subtitles, then UI.
    QCOMPARE(
        rhi->beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QByteArray translucentRedUi(4, '\0');
    translucentRedUi[0] = static_cast<char>(64);
    translucentRedUi[3] = static_cast<char>(128);
    QByteArray translucentBlueSubtitle(4, '\0');
    translucentBlueSubtitle[2] = static_cast<char>(64);
    translucentBlueSubtitle[3] = static_cast<char>(128);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(
        uiTexture.get(),
        QRhiTextureUploadDescription(QRhiTextureUploadEntry(
            0, 0,
            QRhiTextureSubresourceUploadDescription(translucentRedUi))));
    updates->uploadTexture(
        subtitleTexture.get(),
        QRhiTextureUploadDescription(QRhiTextureUploadEntry(
            0, 0,
            QRhiTextureSubresourceUploadDescription(
                translucentBlueSubtitle))));
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
    const auto expectedLinearBlend = [alpha](
            float baseLinear,
            float encodedSubtitle,
            float encodedUi,
            float scale) {
            const float withSubtitle =
                srgbToLinear(encodedSubtitle) * alpha
                + baseLinear * (1.0f - alpha);
            return (
                srgbToLinear(encodedUi) * alpha
                + withSubtitle * (1.0f - alpha)) * scale;
        };

    const FloatPixel blendedBackground =
        readFloatPixel(linearReadback, *rhi, 1, 1);
    compareNear(
        blendedBackground.r,
        expectedLinearBlend(
            0.0f,
            0.0f,
            encodedStraightRed,
            linearSdrScale),
        0.002f);
    compareNear(
        blendedBackground.g,
        expectedLinearBlend(
            0.0f,
            0.0f,
            0.0f,
            linearSdrScale),
        0.002f);
    compareNear(
        blendedBackground.b,
        expectedLinearBlend(
            0.0f,
            encodedStraightRed,
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
        expectedExtended, 0.0f, encodedStraightRed, linearSdrScale);
    const float expectedExtendedGreen = expectedLinearBlend(
        expectedExtended, 0.0f, 0.0f, linearSdrScale);
    const float expectedExtendedBlue = expectedLinearBlend(
        expectedExtended, encodedStraightRed, 0.0f, linearSdrScale);
    QVERIFY(expectedExtendedRed > 1.0f);
    QVERIFY(expectedExtendedGreen > 0.0f);
    compareNear(
        reusedExtendedVideo.r, expectedExtendedRed, 0.01f);
    compareNear(
        reusedExtendedVideo.g, expectedExtendedGreen, 0.01f);
    compareNear(
        reusedExtendedVideo.b, expectedExtendedBlue, 0.01f);
    compareNear(reusedExtendedVideo.a, 1.0f, 0.001f);

    // The compositor owns a valid fallback binding when no page publishes a
    // visible video viewport. No video surface is prepared or sampled.
    QCOMPARE(
        compositor.setTextures(nullptr, nullptr, *uiTexture),
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
    updates->uploadTexture(
        subtitleTexture.get(),
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
    compareByteNear(hiddenVideoPixel.r, 0);
    compareByteNear(hiddenVideoPixel.g, 0);
    compareByteNear(hiddenVideoPixel.b, 0);
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
            &producer->textureForComposition(),
            subtitleTexture.get(),
            *uiTexture),
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
    updates->uploadTexture(
        subtitleTexture.get(),
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

void QrhiCompositorTest::
libplaceboD3d11SurfaceAndCompositionReadback() {
#ifndef Q_OS_WIN
    QSKIP("The current libplacebo integration is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(
        graphicsDevice,
        "Could not create the shared QRhi/libplacebo D3D11 domain");
    QVERIFY(graphicsDevice->libplaceboContext().isValid());
    QRhi &rhi = graphicsDevice->rhi();

    DiagnosticVideoSource source(
        VideoProducerApi::Libplacebo,
        VideoTargetReadback::Enabled,
        videoSize);
    QVERIFY(source.useLibplacebo());
    const std::uint64_t libplaceboRevision =
        source.producerConfigurationRevision();
    source.setUseLibplacebo(false);
    QVERIFY(!source.useLibplacebo());
    QVERIFY(
        source.producerConfigurationRevision()
        != libplaceboRevision);
    std::unique_ptr<RenderedVideoProducer> comparisonProducer =
        source.createProducer(*graphicsDevice);
    QVERIFY(comparisonProducer);
    const RenderedVideoProducerDiagnostics comparisonDiagnostics =
        comparisonProducer->diagnostics();
    QCOMPARE(
        comparisonDiagnostics.producerName,
        QStringLiteral("Diagnostic pattern"));
    comparisonProducer.reset();
    const std::uint64_t proceduralRevision =
        source.producerConfigurationRevision();
    source.setUseLibplacebo(true);
    QVERIFY(source.useLibplacebo());
    QVERIFY(
        source.producerConfigurationRevision()
        != proceduralRevision);
    source.setSourcePeakHeadroom(1.0f);
    source.setToneMappingEnabled(false);
    source.setAnimatePattern(false);
    std::unique_ptr<RenderedVideoProducer> producer =
        source.createProducer(*graphicsDevice);
    QVERIFY(producer);
    auto *const libplaceboProducer =
        dynamic_cast<LibplaceboDiagnosticVideoProducer *>(
            producer.get());
    QVERIFY(libplaceboProducer);

    RenderedVideoSurfaceState sdrState = surfaceState();
    sdrState.graphicsDeviceGeneration =
        graphicsDevice->generation();
    sdrState.contentRevision = source.contentRevision();
    QRhiCommandBuffer *commandBuffer = nullptr;
    constexpr int centerX = 8;
    constexpr int grayscaleY = 1;
    constexpr int spectrumY = 5;
    constexpr int steppedY = 10;
    const float expectedSdr =
        analyticRamp(centerX, videoSize.width(), 1.0f);

    const auto captureProducerSurface =
        [&](const RenderedVideoSurfaceState &state,
            QRhiReadbackResult &readback) -> QString {
        if (producer->ensureSurface(state)
                != VideoOperationResult::Ready) {
            return QStringLiteral(
                "Could not provision the producer surface");
        }
        if (rhi.beginOffscreenFrame(&commandBuffer)
                != QRhi::FrameOpSuccess
                || !commandBuffer) {
            return QStringLiteral(
                "Could not begin the producer capture frame");
        }
        if (producer->render(*commandBuffer, state)
                != VideoOperationResult::Ready
                || producer->prepareForComposition(*commandBuffer)
                    != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            producer->submissionAborted();
            producer->discardPendingRender();
            return QStringLiteral(
                "Could not render the producer surface");
        }

        bool completed = false;
        readback.completed = [&completed] {
            completed = true;
        };
        QRhiResourceUpdateBatch *captureUpdates =
            rhi.nextResourceUpdateBatch();
        captureUpdates->readBackTexture(
            QRhiReadbackDescription(
                &producer->textureForComposition()),
            &readback);
        commandBuffer->resourceUpdate(captureUpdates);
        const QRhi::FrameOpResult frameResult =
            rhi.endOffscreenFrame();
        if (frameResult == QRhi::FrameOpSuccess) {
            producer->submissionAccepted();
            producer->commitPendingRender();
        } else {
            producer->submissionAborted();
            producer->discardPendingRender();
        }
        if (frameResult != QRhi::FrameOpSuccess
                || !completed) {
            return QStringLiteral(
                "Could not complete the producer readback");
        }
        return {};
    };

    const std::array<float, 3> referenceWhites{
        80.0f,
        100.0f,
        PL_COLOR_SDR_WHITE,
    };
    for (const float referenceWhite : referenceWhites) {
        sdrState.description.referenceWhiteNits =
            referenceWhite;
        sdrState.description.targetPeakHeadroom = 4.0f;

        QRhiReadbackResult sdrReadback;
        const QString captureError =
            captureProducerSurface(sdrState, sdrReadback);
        QVERIFY2(captureError.isEmpty(), qPrintable(captureError));
        QCOMPARE(sdrReadback.format, QRhiTexture::RGBA16F);
        QCOMPARE(sdrReadback.pixelSize, videoSize);
        QVERIFY(!producer->needsRender(sdrState));

        const FloatPixel sdrNeutral = readFloatPixel(
            sdrReadback, rhi, centerX, grayscaleY);
        compareNear(sdrNeutral.r, expectedSdr, 0.01f);
        compareNear(sdrNeutral.g, expectedSdr, 0.01f);
        compareNear(sdrNeutral.b, expectedSdr, 0.01f);
        compareNear(sdrNeutral.a, 1.0f, 0.002f);

        const FloatPixel sdrSpectrum = readFloatPixel(
            sdrReadback, rhi, centerX, spectrumY);
        const std::array<float, 3> expectedSdrSpectrum =
            analyticSpectrum(
                centerX, videoSize.width(), 1.0f);
        compareNear(
            sdrSpectrum.r, expectedSdrSpectrum[0], 0.015f);
        compareNear(
            sdrSpectrum.g, expectedSdrSpectrum[1], 0.015f);
        compareNear(
            sdrSpectrum.b, expectedSdrSpectrum[2], 0.015f);
        const FloatPixel sdrStep = readFloatPixel(
            sdrReadback, rhi, centerX, steppedY);
        const float expectedSdrStep =
            analyticStep(
                centerX, videoSize.width(), 1.0f);
        compareNear(sdrStep.r, expectedSdrStep, 0.01f);
        compareNear(sdrStep.g, expectedSdrStep, 0.01f);
        compareNear(sdrStep.b, expectedSdrStep, 0.01f);
    }

    const RenderedVideoProducerDiagnostics diagnostics =
        producer->diagnostics();
    QVERIFY(diagnostics.isValid());
    QVERIFY(diagnostics.producerName.contains(
        QStringLiteral("libplacebo")));
    QCOMPARE(
        diagnostics.target.outputPath,
        VideoOutputPath::DirectRenderTarget);
    QCOMPARE(
        diagnostics.knownInputCpuTransfersPerInputFrame,
        1U);
    QCOMPARE(
        diagnostics.target.knownOutputGpuCopiesPerRender,
        0U);
    QCOMPARE(
        diagnostics.target.knownOutputCpuTransfersPerRender,
        0U);
    QVERIFY(diagnostics.target.synchronizationMode.contains(
        QStringLiteral("D3D11")));
    QVERIFY(diagnostics.target.synchronizationMode.contains(
        QStringLiteral("external commands")));
    QVERIFY(diagnostics.target.synchronizationMode.contains(
        QStringLiteral("rgba16"),
        Qt::CaseInsensitive));
    QVERIFY(diagnostics.target.fallbackReason.isEmpty());

    std::unique_ptr<QRhiTexture> uiTexture(rhi.newTexture(
        QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(uiTexture->create());
    std::unique_ptr<QRhiTexture> subtitleTexture(rhi.newTexture(
        QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(subtitleTexture->create());
    std::unique_ptr<QRhiTexture> outputTexture(rhi.newTexture(
        QRhiTexture::RGBA16F,
        videoSize,
        1,
        QRhiTexture::RenderTarget
            | QRhiTexture::UsedAsTransferSource));
    QVERIFY(outputTexture->create());
    const QRhiTextureRenderTargetDescription outputDescription(
        QRhiColorAttachment(outputTexture.get()));
    std::unique_ptr<QRhiTextureRenderTarget> outputTarget(
        rhi.newTextureRenderTarget(outputDescription));
    std::unique_ptr<QRhiRenderPassDescriptor> outputPass(
        outputTarget->newCompatibleRenderPassDescriptor());
    outputTarget->setRenderPassDescriptor(outputPass.get());
    QVERIFY(outputTarget->create());

    HdrCompositor compositor(rhi);
    QCOMPARE(
        compositor.initialize(
            *outputPass,
            &producer->textureForComposition(),
            subtitleTexture.get(),
            *uiTexture),
        HdrCompositor::ResourceResult::Ready);

    // With a 7x pattern peak, the first nonzero step is exactly 1.0 in
    // libplacebo's PQ normalization: one 203-nit HDR reference-white patch.
    // Leave target minimum unknown here to isolate the white anchor from
    // black-point adaptation, which the surrounding captures cover.
    source.setSourcePeakHeadroom(
        exactReferenceWhitePatternPeak);
    source.setToneMappingEnabled(true);
    RenderedVideoSurfaceState referenceWhiteState = sdrState;
    referenceWhiteState.description.referenceWhiteNits =
        100.0f;
    referenceWhiteState.description.targetMinimumLuminanceKnown =
        false;
    referenceWhiteState.description.targetMinimumLuminanceNits =
        0.0f;
    referenceWhiteState.description.targetPeakHeadroom =
        10.0f;
    referenceWhiteState.contentRevision =
        source.contentRevision();
    QRhiReadbackResult referenceWhiteReadback;
    const QString referenceWhiteCaptureError =
        captureProducerSurface(
            referenceWhiteState,
            referenceWhiteReadback);
    QVERIFY2(
        referenceWhiteCaptureError.isEmpty(),
        qPrintable(referenceWhiteCaptureError));
    constexpr int referenceWhiteX = 2;
    compareNear(
        analyticStep(
            referenceWhiteX,
            videoSize.width(),
            exactReferenceWhitePatternPeak),
        1.0f,
        0.0001f);
    const FloatPixel referenceWhitePatch =
        readFloatPixel(
            referenceWhiteReadback,
            rhi,
            referenceWhiteX,
            steppedY);
    compareNear(referenceWhitePatch.r, 1.0f, 0.02f);
    compareNear(referenceWhitePatch.g, 1.0f, 0.02f);
    compareNear(referenceWhitePatch.b, 1.0f, 0.02f);

    const std::uint64_t referencePatchUploadCount =
        libplaceboProducer->sourceUploadCount();
    source.setSourcePeakHeadroom(sourcePeak);
    // Hold one 1000-nit PQ signal fixed while changing only the output
    // reference white. A 600-nit display has 6x headroom at 100-nit white, so
    // the source fits without highlight compression.
    RenderedVideoSurfaceState hdr100State = sdrState;
    hdr100State.description.referenceWhiteNits = 100.0f;
    hdr100State.description.targetPeakHeadroom =
        physicalTargetPeakNits
        / hdr100State.description.referenceWhiteNits;
    hdr100State.contentRevision = source.contentRevision();
    QRhiReadbackResult hdr100Readback;
    const QString hdr100CaptureError =
        captureProducerSurface(
            hdr100State, hdr100Readback);
    QVERIFY2(
        hdr100CaptureError.isEmpty(),
        qPrintable(hdr100CaptureError));
    const FloatPixel hdr100Right = readFloatPixel(
        hdr100Readback, rhi, 13, grayscaleY);
    const std::uint64_t fixedPqUploadCount =
        libplaceboProducer->sourceUploadCount();
    QCOMPARE(
        fixedPqUploadCount,
        referencePatchUploadCount + 1);
    QVERIFY(hdr100Right.r > 1.0f);
    QVERIFY(hdr100Right.r < sourcePeak);
    compareNear(
        hdr100Right.r,
        analyticRamp(13, videoSize.width(), sourcePeak),
        0.04f);

    // The same fixed source also fits at 80-nit white. The rendered-video
    // surface remains reference-white-relative, so its samples stay stable;
    // platform presentation changes their physical luminance later.
    RenderedVideoSurfaceState hdr80State = hdr100State;
    hdr80State.description.referenceWhiteNits = 80.0f;
    hdr80State.description.targetPeakHeadroom =
        physicalTargetPeakNits
        / hdr80State.description.referenceWhiteNits;
    QRhiReadbackResult hdr80Readback;
    const QString hdr80CaptureError =
        captureProducerSurface(
            hdr80State, hdr80Readback);
    QVERIFY2(
        hdr80CaptureError.isEmpty(),
        qPrintable(hdr80CaptureError));
    const FloatPixel hdr80Right = readFloatPixel(
        hdr80Readback, rhi, 13, grayscaleY);
    QCOMPARE(
        libplaceboProducer->sourceUploadCount(),
        fixedPqUploadCount);
    compareNear(hdr80Right.r, hdr100Right.r, 0.03f);
    compareNear(hdr80Right.g, hdr100Right.g, 0.03f);
    compareNear(hdr80Right.b, hdr100Right.b, 0.03f);

    // Raising reference white to 203 nits reduces the same physical display
    // to less than 3x headroom. The 1000-nit source no longer fits, so
    // libplacebo must compress highlights into the declared target.
    RenderedVideoSurfaceState hdrState = hdr80State;
    hdrState.description.referenceWhiteNits =
        PL_COLOR_SDR_WHITE;
    hdrState.description.targetPeakHeadroom =
        physicalTargetPeakNits
        / hdrState.description.referenceWhiteNits;
    QCOMPARE(
        producer->ensureSurface(hdrState),
        VideoOperationResult::Ready);
    QVERIFY(producer->needsRender(hdrState));

    QCOMPARE(
        rhi.beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
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
    updates->uploadTexture(
        subtitleTexture.get(),
        QRhiTextureUploadDescription(
            QRhiTextureUploadEntry(
                0,
                0,
                QRhiTextureSubresourceUploadDescription(
                    transparentUi))));
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(
        producer->render(*commandBuffer, hdrState),
        VideoOperationResult::Ready);
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);

    HdrCompositorParameters parameters;
    parameters.viewportSize = {
        static_cast<float>(videoSize.width()),
        static_cast<float>(videoSize.height()),
    };
    parameters.videoOrigin = {0.0f, 0.0f};
    parameters.videoSize = parameters.viewportSize;
    parameters.sdrScale =
        hdrState.description.referenceWhiteNits / 80.0f;
    parameters.ndcYUp =
        rhi.isYUpInNDC() ? 1.0f : 0.0f;
    parameters.linearOutput = 1.0f;
    compositor.render(
        *commandBuffer,
        *outputTarget,
        videoSize,
        parameters);

    bool hdrReadbackCompleted = false;
    bool compositionReadbackCompleted = false;
    QRhiReadbackResult hdrReadback;
    QRhiReadbackResult compositionReadback;
    hdrReadback.completed = [&hdrReadbackCompleted] {
        hdrReadbackCompleted = true;
    };
    compositionReadback.completed =
        [&compositionReadbackCompleted] {
            compositionReadbackCompleted = true;
        };
    updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(
            &producer->textureForComposition()),
        &hdrReadback);
    updates->readBackTexture(
        QRhiReadbackDescription(outputTexture.get()),
        &compositionReadback);
    commandBuffer->resourceUpdate(updates);
    const QRhi::FrameOpResult hdrFrameResult =
        rhi.endOffscreenFrame();
    if (hdrFrameResult == QRhi::FrameOpSuccess) {
        producer->submissionAccepted();
        producer->commitPendingRender();
    } else {
        producer->submissionAborted();
        producer->discardPendingRender();
    }
    QCOMPARE(hdrFrameResult, QRhi::FrameOpSuccess);
    QCOMPARE(
        libplaceboProducer->sourceUploadCount(),
        fixedPqUploadCount);
    QVERIFY(hdrReadbackCompleted);
    QVERIFY(compositionReadbackCompleted);
    QCOMPARE(hdrReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(compositionReadback.format, QRhiTexture::RGBA16F);
    QVERIFY(!producer->needsRender(hdrState));

    const FloatPixel hdrLeft = readFloatPixel(
        hdrReadback, rhi, 2, grayscaleY);
    const FloatPixel hdrRight = readFloatPixel(
        hdrReadback, rhi, 13, grayscaleY);
    QVERIFY(std::isfinite(hdrLeft.r));
    QVERIFY(std::isfinite(hdrRight.r));
    QVERIFY(hdrRight.r > hdrLeft.r);
    QVERIFY(hdrRight.r > 1.0f);
    QVERIFY(
        hdrRight.r
        <= hdrState.description.targetPeakHeadroom + 0.03f);
    QVERIFY(hdrRight.r < hdr100Right.r - 0.05f);
    compareNear(hdrRight.r, hdrRight.g, 0.02f);
    compareNear(hdrRight.g, hdrRight.b, 0.02f);
    compareNear(hdrRight.a, 1.0f, 0.002f);

    const FloatPixel hdrSpectrum = readFloatPixel(
        hdrReadback, rhi, centerX, spectrumY);
    QVERIFY(
        std::abs(hdrSpectrum.r - hdrSpectrum.g) > 0.01f
        || std::abs(hdrSpectrum.g - hdrSpectrum.b) > 0.01f);
    const FloatPixel hdrStep = readFloatPixel(
        hdrReadback, rhi, centerX, steppedY);
    compareNear(hdrStep.r, hdrStep.g, 0.02f);
    compareNear(hdrStep.g, hdrStep.b, 0.02f);

    const FloatPixel composed = readFloatPixel(
        compositionReadback, rhi, 13, grayscaleY);
    compareNear(
        composed.r,
        hdrRight.r * parameters.sdrScale,
        0.03f);
    compareNear(
        composed.g,
        hdrRight.g * parameters.sdrScale,
        0.03f);
    compareNear(
        composed.b,
        hdrRight.b * parameters.sdrScale,
        0.03f);
    compareNear(composed.a, 1.0f, 0.002f);

    source.setSourcePeakHeadroom(1.0f);
    source.setToneMappingEnabled(false);
    RenderedVideoSurfaceState resizedState = hdrState;
    resizedState.description.pixelSize = {8, 6};
    resizedState.contentRevision = source.contentRevision();
    const std::uint64_t previousRevision =
        producer->compositionTextureRevision();
    QCOMPARE(
        producer->ensureSurface(resizedState),
        VideoOperationResult::Ready);
    QVERIFY(
        producer->compositionTextureRevision()
        > previousRevision);
    QCOMPARE(
        producer->textureForComposition().pixelSize(),
        QSize(8, 6));
    QCOMPARE(
        rhi.beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QCOMPARE(
        producer->render(*commandBuffer, resizedState),
        VideoOperationResult::Ready);
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);
    bool resizedReadbackCompleted = false;
    QRhiReadbackResult resizedReadback;
    resizedReadback.completed = [&resizedReadbackCompleted] {
        resizedReadbackCompleted = true;
    };
    updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(
            &producer->textureForComposition()),
        &resizedReadback);
    commandBuffer->resourceUpdate(updates);
    const QRhi::FrameOpResult resizedFrameResult =
        rhi.endOffscreenFrame();
    QCOMPARE(resizedFrameResult, QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->commitPendingRender();
    QVERIFY(resizedReadbackCompleted);
    QCOMPARE(resizedReadback.pixelSize, QSize(8, 6));
    const FloatPixel resizedPixel = readFloatPixel(
        resizedReadback, rhi, 6, 0);
    QVERIFY(resizedPixel.r > 0.5f);
    QVERIFY(resizedPixel.r <= 1.02f);
    compareNear(resizedPixel.r, resizedPixel.g, 0.08f);
    compareNear(resizedPixel.g, resizedPixel.b, 0.08f);
    compareNear(resizedPixel.a, 1.0f, 0.002f);
    QVERIFY(!producer->needsRender(resizedState));

    // Reproduce the engine's live diagnostic switch boundary: destroy a
    // producer whose texture is still referenced by the compositor, create
    // the other implementation, and replace the bindings before rendering.
    producer.reset();
    source.setUseLibplacebo(false);
    QVERIFY(!source.useLibplacebo());
    producer = source.createProducer(*graphicsDevice);
    QVERIFY(producer);

    RenderedVideoSurfaceState switchedState = resizedState;
    switchedState.description.pixelSize = videoSize;
    QCOMPARE(
        producer->ensureSurface(switchedState),
        VideoOperationResult::Ready);
    QCOMPARE(
        rhi.beginOffscreenFrame(&commandBuffer),
        QRhi::FrameOpSuccess);
    QCOMPARE(
        producer->render(*commandBuffer, switchedState),
        VideoOperationResult::Ready);
    QCOMPARE(
        producer->prepareForComposition(*commandBuffer),
        VideoOperationResult::Ready);
    QCOMPARE(
        compositor.setTextures(
            &producer->textureForComposition(),
            subtitleTexture.get(),
            *uiTexture),
        HdrCompositor::ResourceResult::Ready);
    compositor.render(
        *commandBuffer,
        *outputTarget,
        videoSize,
        parameters);

    bool switchedSurfaceCompleted = false;
    bool switchedCompositionCompleted = false;
    QRhiReadbackResult switchedSurfaceReadback;
    QRhiReadbackResult switchedCompositionReadback;
    switchedSurfaceReadback.completed =
        [&switchedSurfaceCompleted] {
            switchedSurfaceCompleted = true;
        };
    switchedCompositionReadback.completed =
        [&switchedCompositionCompleted] {
            switchedCompositionCompleted = true;
        };
    updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(
            &producer->textureForComposition()),
        &switchedSurfaceReadback);
    updates->readBackTexture(
        QRhiReadbackDescription(outputTexture.get()),
        &switchedCompositionReadback);
    commandBuffer->resourceUpdate(updates);
    const QRhi::FrameOpResult switchedFrameResult =
        rhi.endOffscreenFrame();
    QCOMPARE(switchedFrameResult, QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->commitPendingRender();
    QVERIFY(switchedSurfaceCompleted);
    QVERIFY(switchedCompositionCompleted);

    const RenderedVideoProducerDiagnostics switchedDiagnostics =
        producer->diagnostics();
    QCOMPARE(
        switchedDiagnostics.producerName,
        QStringLiteral("Diagnostic pattern"));
    QCOMPARE(
        switchedDiagnostics.knownInputCpuTransfersPerInputFrame,
        0U);
    const FloatPixel switchedSurface = readFloatPixel(
        switchedSurfaceReadback, rhi, 13, grayscaleY);
    const FloatPixel switchedComposition = readFloatPixel(
        switchedCompositionReadback, rhi, 13, grayscaleY);
    const float expectedSwitched =
        analyticRamp(13, videoSize.width(), 1.0f);
    compareNear(
        switchedSurface.r, expectedSwitched, 0.015f);
    compareNear(
        switchedComposition.r,
        switchedSurface.r * parameters.sdrScale,
        0.03f);
    compareNear(
        switchedComposition.g,
        switchedSurface.g * parameters.sdrScale,
        0.03f);
    compareNear(
        switchedComposition.b,
        switchedSurface.b * parameters.sdrScale,
        0.03f);
#endif
}

void QrhiCompositorTest::
libplaceboAnimatedDiagnosticThroughput() {
#ifndef Q_OS_WIN
    QSKIP("The current libplacebo integration is Windows D3D11");
#else
    constexpr QSize inputFrameSize{640, 360};
    constexpr QSize targetSize{1100, 600};
    constexpr int measuredFrames = 60;

    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY(graphicsDevice);
    QRhi &rhi = graphicsDevice->rhi();
    DiagnosticVideoSource source(
        VideoProducerApi::Libplacebo,
        VideoTargetReadback::Disabled,
        inputFrameSize);
    source.setSourcePeakHeadroom(12.5f);
    source.setToneMappingEnabled(true);
    source.setAnimatePattern(true);
    std::unique_ptr<RenderedVideoProducer> producer =
        source.createProducer(*graphicsDevice);
    QVERIFY(producer);

    RenderedVideoSurfaceState state = surfaceState();
    state.description.pixelSize = targetSize;
    state.description.referenceWhiteNits = 203.0f;
    state.description.targetMinimumLuminanceKnown = true;
    state.description.targetMinimumLuminanceNits = 0.005f;
    state.description.targetPeakHeadroom = 4.0f;
    state.graphicsDeviceGeneration =
        graphicsDevice->generation();
    state.contentRevision = source.contentRevision();

    auto timestamp = std::chrono::steady_clock::now();
    const auto renderFrame = [&]() {
        timestamp += std::chrono::milliseconds(16);
        source.prepareForPresentation(timestamp);
        state.contentRevision = source.contentRevision();
        if (producer->ensureSurface(state)
                != VideoOperationResult::Ready) {
            return false;
        }
        QRhiCommandBuffer *commandBuffer = nullptr;
        if (rhi.beginOffscreenFrame(&commandBuffer)
                != QRhi::FrameOpSuccess
                || !commandBuffer) {
            return false;
        }
        if (producer->render(*commandBuffer, state)
                != VideoOperationResult::Ready
                || producer->prepareForComposition(*commandBuffer)
                    != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            producer->submissionAborted();
            producer->discardPendingRender();
            return false;
        }
        const QRhi::FrameOpResult result =
            rhi.endOffscreenFrame();
        if (result != QRhi::FrameOpSuccess) {
            producer->submissionAborted();
            producer->discardPendingRender();
            return false;
        }
        producer->submissionAccepted();
        producer->commitPendingRender();
        return true;
    };

    for (int i = 0; i < 3; ++i)
        QVERIFY(renderFrame());

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < measuredFrames; ++i)
        QVERIFY(renderFrame());
    const qint64 elapsedNanoseconds = timer.nsecsElapsed();
    const double millisecondsPerFrame =
        static_cast<double>(elapsedNanoseconds)
        / 1'000'000.0
        / measuredFrames;
    const double framesPerSecond =
        1000.0 / millisecondsPerFrame;
    qInfo().nospace()
        << "libplacebo diagnostic throughput: "
        << QString::number(framesPerSecond, 'f', 1)
        << " FPS CPU submission ("
        << QString::number(millisecondsPerFrame, 'f', 2)
        << " ms/frame), "
        << inputFrameSize.width() << "x"
        << inputFrameSize.height() << " input -> "
        << targetSize.width() << "x"
        << targetSize.height() << " target";

    const RenderedVideoProducerDiagnostics diagnostics =
        producer->diagnostics();
    QCOMPARE(
        diagnostics.knownInputCpuTransfersPerInputFrame,
        1U);
    QVERIFY(diagnostics.inputPath.contains(
        QStringLiteral("640×360")));
#endif
}

// QTEST_MAIN would instantiate QGuiApplication because this target links Qt
// Gui for QRhi. This test is deliberately headless but needs QCoreApplication
// lifetime for QRhi's process services and orderly teardown.
QTEST_GUILESS_MAIN(QrhiCompositorTest)
#include "tst_QrhiCompositor.moc"
