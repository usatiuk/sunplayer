#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QtCore/qfloat16.h>
#include <QtCore/qscopeguard.h>
#include <QtTest>
#include <libplacebo/colorspace.h>
#include <rhi/qrhi.h>

#include "graphics/GraphicsBackendFactory.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "presentation/HdrCompositor.h"
#include "video/DiagnosticVideoSource.h"
#include "video/LibplaceboDiagnosticVideoProducer.h"
#include "video/RenderedVideoProducer.h"
#include "video/RenderedVideoSurface.h"
#include "video/VideoTargetInterop.h"
#include "video/libplacebo/LibplaceboRenderContext.h"

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
constexpr float sourcePeak = masteredSourcePeakNits / PL_COLOR_SDR_WHITE;
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

ColorPrimaries bt709Primaries() {
    return {
        .red = {0.640f, 0.330f},
        .green = {0.300f, 0.600f},
        .blue = {0.150f, 0.060f},
        .white = {0.3127f, 0.3290f},
    };
}

ColorPrimaries displayP3Primaries() {
    return {
        .red = {0.680f, 0.320f},
        .green = {0.265f, 0.690f},
        .blue = {0.150f, 0.060f},
        .white = {0.3127f, 0.3290f},
    };
}

RenderedVideoSurfaceState surfaceState() {
    RenderedVideoSurfaceState state;
    state.description.pixelSize = videoSize;
    state.description.pixelFormat = RenderedVideoPixelFormat::Rgba16Float;
    state.description.colorSpace = RenderedVideoColorSpace::LinearSrgb;
    state.description.luminance = RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative;
    state.description.alphaMode = RenderedVideoAlphaMode::Opaque;
    state.description.referenceWhiteNits = 80.0f;
    state.description.targetMinimumLuminanceKnown = true;
    state.description.targetMinimumLuminanceNits = 0.005f;
    state.description.targetPeakHeadroom = sourcePeak;
    state.graphicsDeviceGeneration = 1;
    state.contentRevision = 1;
    return state;
}

int storageY(QRhi const& rhi, int height, int canonicalY) {
    return rhi.isYUpInFramebuffer() ? height - 1 - canonicalY : canonicalY;
}

FloatPixel readFloatPixel(QRhiReadbackResult const& result, QRhi const& rhi, int x, int y) {
    Q_ASSERT(result.format == QRhiTexture::RGBA16F);
    Q_ASSERT(result.pixelSize.width() > x && x >= 0);
    Q_ASSERT(result.pixelSize.height() > y && y >= 0);
    Q_ASSERT(result.data.size() >= result.pixelSize.width() * result.pixelSize.height() * 8);

    int const row = storageY(rhi, result.pixelSize.height(), y);
    qsizetype const offset = (row * result.pixelSize.width() + x) * 4 * sizeof(qfloat16);
    std::array<qfloat16, 4> channels;
    std::memcpy(channels.data(), result.data.constData() + offset, sizeof(channels));
    return {
        static_cast<float>(channels[0]),
        static_cast<float>(channels[1]),
        static_cast<float>(channels[2]),
        static_cast<float>(channels[3]),
    };
}

BytePixel readBytePixel(QRhiReadbackResult const& result, QRhi const& rhi, int x, int y) {
    Q_ASSERT(result.format == QRhiTexture::RGBA8);
    Q_ASSERT(result.pixelSize.width() > x && x >= 0);
    Q_ASSERT(result.pixelSize.height() > y && y >= 0);
    Q_ASSERT(result.data.size() >= result.pixelSize.width() * result.pixelSize.height() * 4);

    int const row = storageY(rhi, result.pixelSize.height(), y);
    qsizetype const offset = (row * result.pixelSize.width() + x) * 4;
    auto const* pixel = reinterpret_cast<uchar const*>(result.data.constData() + offset);
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

float smoothStep(float value) { return value * value * (3.0f - 2.0f * value); }

float expectedRamp(int x, int width = videoSize.width()) {
    float const u = (static_cast<float>(x) + 0.5f) / width;
    return 0.02f + (sourcePeak - 0.02f) * smoothStep(u);
}

float analyticRamp(int x, int width, float peakHeadroom) {
    float const u = (static_cast<float>(x) + 0.5f) / width;
    return 0.02f + (peakHeadroom - 0.02f) * smoothStep(u);
}

float analyticStep(int x, int width, float peakHeadroom) {
    float const u = (static_cast<float>(x) + 0.5f) / width;
    return std::floor(u * 8.0f) / 7.0f * peakHeadroom;
}

std::array<float, 3> analyticSpectrum(int x, int width, float peakHeadroom, float phase = 0.0f) {
    float const u = (static_cast<float>(x) + 0.5f) / width;
    float const ramp = analyticRamp(x, width, peakHeadroom);
    return {
        (0.5f + 0.5f * std::cos(tau * (u + phase))) * ramp,
        (0.5f + 0.5f * std::cos(tau * (u + phase + 0.33f))) * ramp,
        (0.5f + 0.5f * std::cos(tau * (u + phase + 0.67f))) * ramp,
    };
}

float expectedStep(int x) {
    float const u = (static_cast<float>(x) + 0.5f) / videoSize.width();
    return std::floor(u * 8.0f) / 7.0f * sourcePeak;
}

float srgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value) {
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

std::array<float, 3> linearSrgbToBt2020(FloatPixel const& value) {
    return {
        0.627404f * value.r + 0.329283f * value.g + 0.043313f * value.b,
        0.069097f * value.r + 0.919540f * value.g + 0.011362f * value.b,
        0.016391f * value.r + 0.088013f * value.g + 0.895595f * value.b,
    };
}

float linearToPq(float value) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    float const normalized = std::clamp(value * (PL_COLOR_SDR_WHITE / 10000.0f), 0.0f, 1.0f);
    float const powered = std::pow(normalized, m1);
    return std::pow((c1 + c2 * powered) / (1.0f + c3 * powered), m2);
}

int encodedByte(float linear) {
    float const encoded = linearToSrgb(std::clamp(linear, 0.0f, 1.0f));
    return static_cast<int>(std::lround(encoded * 255.0f));
}

int gamma22EncodedByte(float linear) {
    float const encoded = std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f);
    return static_cast<int>(std::lround(encoded * 255.0f));
}

void compareNear(float actual, float expected, float tolerance) {
    QVERIFY2(std::abs(actual - expected) <= tolerance, qPrintable(QStringLiteral("actual %1, expected %2 ± %3")
                                                                      .arg(actual, 0, 'g', 8)
                                                                      .arg(expected, 0, 'g', 8)
                                                                      .arg(tolerance, 0, 'g', 8)));
}

void compareByteNear(int actual, int expected, int tolerance = 2) {
    QVERIFY2(std::abs(actual - expected) <= tolerance,
             qPrintable(QStringLiteral("actual %1, expected %2 ± %3").arg(actual).arg(expected).arg(tolerance)));
}
} // namespace

class QrhiCompositorTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void realD3d11ProducerAndCompositionReadback();
    void libplaceboD3d11SurfaceAndCompositionReadback();
    void libplaceboTargetGamutBoundary();
    void libplaceboAnimatedDiagnosticThroughput();
};

void QrhiCompositorTest::realD3d11ProducerAndCompositionReadback() {
#ifndef Q_OS_WIN
    QSKIP("The current supported QRhi integration test is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphicsDevice, "Could not create the D3D11 graphics domain");
    QRhi* const rhi = &graphicsDevice->rhi();
    QVERIFY(graphicsDevice->generation() != 0);
    QCOMPARE(graphicsDevice->backend(), GraphicsBackend::D3D11);
    QVERIFY(graphicsDevice->diagnostics().isValid());
    bool const floatCaptureSupported = rhi->isTextureFormatSupported(
        QRhiTexture::RGBA16F, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    qInfo().noquote() << "QRhi backend:" << rhi->backendName() << "| device:" << rhi->driverInfo().deviceName
                      << "| requested RGBA16F capture format:" << (floatCaptureSupported ? "accepted" : "unavailable");
    QVERIFY(floatCaptureSupported);

    DiagnosticVideoSource source(VideoProducerApi::Qrhi, VideoTargetReadback::Enabled);
    source.setSourcePeakHeadroom(sourcePeak);
    source.setToneMappingEnabled(false);
    source.setAnimatePattern(false);
    std::unique_ptr<RenderedVideoProducer> producer = source.createProducer(*graphicsDevice);
    QVERIFY(producer);
    RenderedVideoProducerDiagnostics const unprovisionedDiagnostics = producer->diagnostics();
    QVERIFY(unprovisionedDiagnostics.isValid());
    QCOMPARE(unprovisionedDiagnostics.target.outputPath, VideoOutputPath::Unavailable);
    QVERIFY(!unprovisionedDiagnostics.target.fallbackReason.isEmpty());
    RenderedVideoSurfaceState const requestedState = surfaceState();
    QCOMPARE(producer->ensureSurface(requestedState), VideoOperationResult::Ready);
    QVERIFY(producer->needsRender(requestedState));
    RenderedVideoProducerDiagnostics const producerDiagnostics = producer->diagnostics();
    QVERIFY(producerDiagnostics.isValid());
    QCOMPARE(producerDiagnostics.target.outputPath, VideoOutputPath::DirectRenderTarget);
    QCOMPARE(producerDiagnostics.knownInputCpuTransfersPerInputFrame, 0U);
    QCOMPARE(producerDiagnostics.target.knownOutputGpuCopiesPerRender, 0U);
    QCOMPARE(producerDiagnostics.target.knownOutputCpuTransfersPerRender, 0U);
    QVERIFY(producerDiagnostics.target.fallbackReason.isEmpty());

    std::unique_ptr<QRhiTexture> uiTexture(rhi->newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(uiTexture->create());
    std::unique_ptr<QRhiTexture> subtitleTexture(rhi->newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(subtitleTexture->create());

    std::unique_ptr<QRhiTexture> outputTexture(rhi->newTexture(
        QRhiTexture::RGBA8, outputSize, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(outputTexture->create());

    QRhiTextureRenderTargetDescription const outputDescription(QRhiColorAttachment(outputTexture.get()));
    std::unique_ptr<QRhiRenderPassDescriptor> outputRenderPass;
    std::unique_ptr<QRhiTextureRenderTarget> outputTarget(rhi->newTextureRenderTarget(outputDescription));
    outputRenderPass.reset(outputTarget->newCompatibleRenderPassDescriptor());
    outputTarget->setRenderPassDescriptor(outputRenderPass.get());
    QVERIFY(outputTarget->create());

    std::unique_ptr<QRhiTexture> linearOutputTexture(rhi->newTexture(
        QRhiTexture::RGBA16F, outputSize, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(linearOutputTexture->create());

    QRhiTextureRenderTargetDescription const linearOutputDescription(QRhiColorAttachment(linearOutputTexture.get()));
    std::unique_ptr<QRhiRenderPassDescriptor> linearOutputRenderPass;
    std::unique_ptr<QRhiTextureRenderTarget> linearOutputTarget(rhi->newTextureRenderTarget(linearOutputDescription));
    linearOutputRenderPass.reset(linearOutputTarget->newCompatibleRenderPassDescriptor());
    linearOutputTarget->setRenderPassDescriptor(linearOutputRenderPass.get());
    QVERIFY(linearOutputTarget->create());

    HdrCompositor compositor(*rhi);
    QCOMPARE(
        compositor.initialize(*outputRenderPass, &producer->textureForComposition(), subtitleTexture.get(), *uiTexture),
        HdrCompositor::ResourceResult::Ready);
    HdrCompositor linearOutputCompositor(*rhi);
    QCOMPARE(linearOutputCompositor.initialize(*linearOutputRenderPass, &producer->textureForComposition(),
                                               subtitleTexture.get(), *uiTexture),
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
    compositorParameters.outputEncoding = 0.0f;

    QRhiCommandBuffer* commandBuffer = nullptr;
    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QVERIFY(commandBuffer);

    QByteArray const transparentUi(4, '\0');
    QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    updates->uploadTexture(subtitleTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                      0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    commandBuffer->resourceUpdate(updates);

    QCOMPARE(producer->render(*commandBuffer, requestedState), VideoOperationResult::Ready);
    QVERIFY(producer->needsRender(requestedState));
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    compositor.render(*commandBuffer, *outputTarget, outputSize, compositorParameters);

    bool videoReadbackCompleted = false;
    bool outputReadbackCompleted = false;
    QRhiReadbackResult videoReadback;
    QRhiReadbackResult outputReadback;
    videoReadback.completed = [&videoReadbackCompleted] { videoReadbackCompleted = true; };
    outputReadback.completed = [&outputReadbackCompleted] { outputReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(&producer->textureForComposition()), &videoReadback);
    updates->readBackTexture(QRhiReadbackDescription(outputTexture.get()), &outputReadback);
    commandBuffer->resourceUpdate(updates);

    QRhi::FrameOpResult const firstFrameResult = rhi->endOffscreenFrame();
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
    QCOMPARE(videoReadback.data.size(), qsizetype(videoSize.width() * videoSize.height() * 8));
    QCOMPARE(outputReadback.pixelSize, outputSize);
    QCOMPARE(outputReadback.format, QRhiTexture::RGBA8);
    QCOMPARE(outputReadback.data.size(), qsizetype(outputSize.width() * outputSize.height() * 4));
    qInfo("RGBA16F producer capture completed with validated byte count");

    constexpr int sampleX = 2;
    FloatPixel const top = readFloatPixel(videoReadback, *rhi, sampleX, 1);
    FloatPixel const middle = readFloatPixel(videoReadback, *rhi, sampleX, 5);
    FloatPixel const bottom = readFloatPixel(videoReadback, *rhi, sampleX, 10);
    compareNear(top.r, expectedRamp(sampleX), 0.002f);
    compareNear(top.g, top.r, 0.001f);
    compareNear(top.b, top.r, 0.001f);
    compareNear(top.a, 1.0f, 0.001f);
    QVERIFY(std::abs(middle.r - middle.g) > 0.01f || std::abs(middle.g - middle.b) > 0.01f);
    compareNear(bottom.r, expectedStep(sampleX), 0.002f);
    compareNear(bottom.g, bottom.r, 0.001f);
    compareNear(bottom.b, bottom.r, 0.001f);
    compareNear(bottom.a, 1.0f, 0.001f);
    QVERIFY(std::abs(top.r - bottom.r) > 0.1f);

    constexpr int extendedSampleX = 12;
    float const expectedExtended = expectedRamp(extendedSampleX);
    QVERIFY(expectedExtended > 1.0f);
    FloatPixel const extendedProducerPixel = readFloatPixel(videoReadback, *rhi, extendedSampleX, 1);
    QVERIFY(extendedProducerPixel.r > 1.0f);
    compareNear(extendedProducerPixel.r, expectedExtended, 0.004f);
    compareNear(extendedProducerPixel.g, expectedExtended, 0.004f);
    compareNear(extendedProducerPixel.b, expectedExtended, 0.004f);

    BytePixel const background = readBytePixel(outputReadback, *rhi, 1, 1);
    compareByteNear(background.r, 0);
    compareByteNear(background.g, 0);
    compareByteNear(background.b, 0);
    QCOMPARE(background.a, 255);

    BytePixel const composedVideo = readBytePixel(outputReadback, *rhi, videoOriginX + sampleX, videoOriginY + 1);
    int const expectedVideoByte = encodedByte(expectedRamp(sampleX));
    compareByteNear(composedVideo.r, expectedVideoByte);
    compareByteNear(composedVideo.g, expectedVideoByte);
    compareByteNear(composedVideo.b, expectedVideoByte);
    QCOMPARE(composedVideo.a, 255);

    // Managed Wayland SDR uses the same composition in linear light but
    // encodes the final normalized value as the gamma-2.2 transfer declared
    // by Qt's color-management-v1 surface description.
    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    HdrCompositorParameters gamma22Parameters = compositorParameters;
    gamma22Parameters.outputEncoding = 1.0f;
    compositor.render(*commandBuffer, *outputTarget, outputSize, gamma22Parameters);

    bool gamma22ReadbackCompleted = false;
    QRhiReadbackResult gamma22Readback;
    gamma22Readback.completed = [&gamma22ReadbackCompleted] { gamma22ReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(outputTexture.get()), &gamma22Readback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    QVERIFY(gamma22ReadbackCompleted);
    BytePixel const gamma22Video = readBytePixel(gamma22Readback, *rhi, videoOriginX + sampleX, videoOriginY + 1);
    int const expectedGamma22Byte = gamma22EncodedByte(expectedRamp(sampleX));
    compareByteNear(gamma22Video.r, expectedGamma22Byte);
    compareByteNear(gamma22Video.g, expectedGamma22Byte);
    compareByteNear(gamma22Video.b, expectedGamma22Byte);
    QCOMPARE(gamma22Video.a, 255);

    // Linux HDR10 keeps every layer linear through composition, then maps
    // linear sRGB into the BT.2020/PQ surface exactly once.
    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    HdrCompositorParameters pqParameters = compositorParameters;
    pqParameters.outputEncoding = 3.0f;
    linearOutputCompositor.render(*commandBuffer, *linearOutputTarget, outputSize, pqParameters);

    bool pqReadbackCompleted = false;
    QRhiReadbackResult pqReadback;
    pqReadback.completed = [&pqReadbackCompleted] { pqReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(linearOutputTexture.get()), &pqReadback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    QVERIFY(pqReadbackCompleted);

    FloatPixel const pqVideo = readFloatPixel(pqReadback, *rhi, videoOriginX + sampleX, videoOriginY + 5);
    std::array<float, 3> const expectedBt2020 = linearSrgbToBt2020(middle);
    compareNear(pqVideo.r, linearToPq(expectedBt2020[0]), 0.002f);
    compareNear(pqVideo.g, linearToPq(expectedBt2020[1]), 0.002f);
    compareNear(pqVideo.b, linearToPq(expectedBt2020[2]), 0.002f);
    compareNear(pqVideo.a, 1.0f, 0.001f);

    // Working neutral 1.0 is the platform reference white. The Linux final
    // encoder serializes it at PQ's 203-nit source-reference coordinate.
    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QByteArray const opaqueWhiteUi(4, static_cast<char>(0xff));
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                0, 0, QRhiTextureSubresourceUploadDescription(opaqueWhiteUi))));
    updates->uploadTexture(subtitleTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                      0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    commandBuffer->resourceUpdate(updates);
    HdrCompositorParameters neutralPqParameters = pqParameters;
    neutralPqParameters.videoSize = {0.0f, 0.0f};
    linearOutputCompositor.render(*commandBuffer, *linearOutputTarget, outputSize, neutralPqParameters);

    bool neutralPqReadbackCompleted = false;
    QRhiReadbackResult neutralPqReadback;
    neutralPqReadback.completed = [&neutralPqReadbackCompleted] { neutralPqReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(linearOutputTexture.get()), &neutralPqReadback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    QVERIFY(neutralPqReadbackCompleted);
    FloatPixel const neutralPq = readFloatPixel(neutralPqReadback, *rhi, 1, 1);
    float const pqReferenceWhite = linearToPq(1.0f);
    compareNear(pqReferenceWhite, 0.580688881f, 0.000002f);
    compareNear(neutralPq.r, pqReferenceWhite, 0.002f);
    compareNear(neutralPq.g, pqReferenceWhite, 0.002f);
    compareNear(neutralPq.b, pqReferenceWhite, 0.002f);
    compareNear(neutralPq.a, 1.0f, 0.001f);

    // Do not clamp signed linear-sRGB components before changing primaries.
    // This vector also produces a negative BT.2020 green component, which PQ
    // must clamp only after the matrix.
    std::unique_ptr<QRhiTexture> signedVideoTexture(rhi->newTexture(QRhiTexture::RGBA16F, {1, 1}, 1));
    QVERIFY(signedVideoTexture->create());
    QCOMPARE(linearOutputCompositor.setTextures(signedVideoTexture.get(), subtitleTexture.get(), *uiTexture),
             HdrCompositor::ResourceResult::Ready);
    std::array<qfloat16, 4> const signedLinearSrgb{
        qfloat16(1.0f),
        qfloat16(-0.1f),
        qfloat16(0.0f),
        qfloat16(1.0f),
    };
    QByteArray signedPixelBytes(static_cast<qsizetype>(sizeof(signedLinearSrgb)), '\0');
    std::memcpy(signedPixelBytes.data(), signedLinearSrgb.data(), sizeof(signedLinearSrgb));

    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(signedVideoTexture.get(),
                           QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                               0, 0, QRhiTextureSubresourceUploadDescription(signedPixelBytes))));
    updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    commandBuffer->resourceUpdate(updates);
    HdrCompositorParameters signedPqParameters = pqParameters;
    signedPqParameters.videoOrigin = {0.0f, 0.0f};
    signedPqParameters.videoSize = {
        static_cast<float>(outputSize.width()),
        static_cast<float>(outputSize.height()),
    };
    linearOutputCompositor.render(*commandBuffer, *linearOutputTarget, outputSize, signedPqParameters);

    bool signedPqReadbackCompleted = false;
    QRhiReadbackResult signedPqReadback;
    signedPqReadback.completed = [&signedPqReadbackCompleted] { signedPqReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(linearOutputTexture.get()), &signedPqReadback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    QVERIFY(signedPqReadbackCompleted);

    FloatPixel const signedPq = readFloatPixel(signedPqReadback, *rhi, 1, 1);
    FloatPixel const signedInput{
        1.0f,
        -0.1f,
        0.0f,
        1.0f,
    };
    std::array<float, 3> const signedBt2020 = linearSrgbToBt2020(signedInput);
    QVERIFY(signedBt2020[1] < 0.0f);
    compareNear(signedPq.r, linearToPq(std::max(0.0f, signedBt2020[0])), 0.002f);
    compareNear(signedPq.g, linearToPq(std::max(0.0f, signedBt2020[1])), 0.002f);
    compareNear(signedPq.b, linearToPq(std::max(0.0f, signedBt2020[2])), 0.002f);
    compareNear(signedPq.a, 1.0f, 0.001f);
    QCOMPARE(linearOutputCompositor.setTextures(&producer->textureForComposition(), subtitleTexture.get(), *uiTexture),
             HdrCompositor::ResourceResult::Ready);

    // A later frame changes only presentation, subtitle, and UI state. It
    // must reuse the submitted video surface while proving the intended layer
    // order: video, then premultiplied sRGB subtitles, then UI.
    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QByteArray translucentRedUi(4, '\0');
    translucentRedUi[0] = static_cast<char>(64);
    translucentRedUi[3] = static_cast<char>(128);
    QByteArray translucentBlueSubtitle(4, '\0');
    translucentBlueSubtitle[2] = static_cast<char>(64);
    translucentBlueSubtitle[3] = static_cast<char>(128);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                0, 0, QRhiTextureSubresourceUploadDescription(translucentRedUi))));
    updates->uploadTexture(subtitleTexture.get(),
                           QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                               0, 0, QRhiTextureSubresourceUploadDescription(translucentBlueSubtitle))));
    commandBuffer->resourceUpdate(updates);
    QVERIFY(!producer->needsRender(requestedState));
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    HdrCompositorParameters linearParameters = compositorParameters;
    constexpr float linearSdrScale = 1.5f;
    constexpr float subtitleLayerOpacity = 0.5f;
    linearParameters.sdrScale = linearSdrScale;
    linearParameters.subtitleOpacity = subtitleLayerOpacity;
    linearParameters.outputEncoding = 2.0f;
    linearOutputCompositor.render(*commandBuffer, *linearOutputTarget, outputSize, linearParameters);

    bool linearReadbackCompleted = false;
    QRhiReadbackResult linearReadback;
    linearReadback.completed = [&linearReadbackCompleted] { linearReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(linearOutputTexture.get()), &linearReadback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    QVERIFY(linearReadbackCompleted);
    QCOMPARE(linearReadback.pixelSize, outputSize);
    QCOMPARE(linearReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(linearReadback.data.size(), qsizetype(outputSize.width() * outputSize.height() * 8));

    float const alpha = 128.0f / 255.0f;
    float const encodedStraightRed = (64.0f / 255.0f) / alpha;
    constexpr float subtitleBrightness = 0.8f;
    auto const expectedLinearBlend =
        [alpha, subtitleBrightness, subtitleLayerOpacity](float baseLinear, float encodedSubtitle, float encodedUi,
                                                         float scale) {
            float const subtitleAlpha = alpha * subtitleLayerOpacity;
            float const withSubtitle = srgbToLinear(encodedSubtitle) * (subtitleBrightness * subtitleAlpha) +
                                       baseLinear * (1.0f - subtitleAlpha);
            return (srgbToLinear(encodedUi) * alpha + withSubtitle * (1.0f - alpha)) * scale;
        };

    FloatPixel const blendedBackground = readFloatPixel(linearReadback, *rhi, 1, 1);
    compareNear(blendedBackground.r, expectedLinearBlend(0.0f, 0.0f, encodedStraightRed, linearSdrScale), 0.002f);
    compareNear(blendedBackground.g, expectedLinearBlend(0.0f, 0.0f, 0.0f, linearSdrScale), 0.002f);
    compareNear(blendedBackground.b, expectedLinearBlend(0.0f, encodedStraightRed, 0.0f, linearSdrScale), 0.002f);
    compareNear(blendedBackground.a, 1.0f, 0.001f);

    FloatPixel const reusedExtendedVideo =
        readFloatPixel(linearReadback, *rhi, videoOriginX + extendedSampleX, videoOriginY + 1);
    float const expectedExtendedRed = expectedLinearBlend(expectedExtended, 0.0f, encodedStraightRed, linearSdrScale);
    float const expectedExtendedGreen = expectedLinearBlend(expectedExtended, 0.0f, 0.0f, linearSdrScale);
    float const expectedExtendedBlue = expectedLinearBlend(expectedExtended, encodedStraightRed, 0.0f, linearSdrScale);
    QVERIFY(expectedExtendedRed > 1.0f);
    QVERIFY(expectedExtendedGreen > 0.0f);
    compareNear(reusedExtendedVideo.r, expectedExtendedRed, 0.01f);
    compareNear(reusedExtendedVideo.g, expectedExtendedGreen, 0.01f);
    compareNear(reusedExtendedVideo.b, expectedExtendedBlue, 0.01f);
    compareNear(reusedExtendedVideo.a, 1.0f, 0.001f);

    // The compositor owns a valid fallback binding when no page publishes a
    // visible video viewport. No video surface is prepared or sampled.
    QCOMPARE(compositor.setTextures(nullptr, nullptr, *uiTexture), HdrCompositor::ResourceResult::Ready);
    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    updates->uploadTexture(subtitleTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                      0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    commandBuffer->resourceUpdate(updates);
    HdrCompositorParameters hiddenVideoParameters = compositorParameters;
    hiddenVideoParameters.videoSize = {0.0f, 0.0f};
    compositor.render(*commandBuffer, *outputTarget, outputSize, hiddenVideoParameters);

    bool hiddenVideoReadbackCompleted = false;
    QRhiReadbackResult hiddenVideoReadback;
    hiddenVideoReadback.completed = [&hiddenVideoReadbackCompleted] { hiddenVideoReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(outputTexture.get()), &hiddenVideoReadback);
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->commitPendingRender();
    QVERIFY(hiddenVideoReadbackCompleted);
    QCOMPARE(hiddenVideoReadback.pixelSize, outputSize);
    QCOMPARE(hiddenVideoReadback.format, QRhiTexture::RGBA8);
    QCOMPARE(hiddenVideoReadback.data.size(), qsizetype(outputSize.width() * outputSize.height() * 4));
    BytePixel const hiddenVideoPixel =
        readBytePixel(hiddenVideoReadback, *rhi, videoOriginX + sampleX, videoOriginY + 1);
    compareByteNear(hiddenVideoPixel.r, 0);
    compareByteNear(hiddenVideoPixel.g, 0);
    compareByteNear(hiddenVideoPixel.b, 0);
    QCOMPARE(hiddenVideoPixel.a, 255);

    // Submission tracking and content-state promotion are separate. Discarding
    // a rendered state after an accepted submission must leave it dirty.
    RenderedVideoSurfaceState resubmittedState = requestedState;
    ++resubmittedState.contentRevision;
    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QCOMPARE(producer->render(*commandBuffer, resubmittedState), VideoOperationResult::Ready);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->discardPendingRender();
    QVERIFY(producer->needsRender(resubmittedState));

    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QCOMPARE(producer->render(*commandBuffer, resubmittedState), VideoOperationResult::Ready);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
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
    std::uint64_t const previousTextureRevision = producer->compositionTextureRevision();
    QCOMPARE(producer->ensureSurface(resizedState), VideoOperationResult::Ready);
    QVERIFY(producer->compositionTextureRevision() > previousTextureRevision);
    QCOMPARE(producer->textureForComposition().pixelSize(), resizedVideoSize);
    QVERIFY(producer->needsRender(resizedState));
    QCOMPARE(compositor.setTextures(&producer->textureForComposition(), subtitleTexture.get(), *uiTexture),
             HdrCompositor::ResourceResult::Ready);

    QCOMPARE(rhi->beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    updates = rhi->nextResourceUpdateBatch();
    updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    updates->uploadTexture(subtitleTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                      0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(producer->render(*commandBuffer, resizedState), VideoOperationResult::Ready);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    HdrCompositorParameters resizedParameters = compositorParameters;
    resizedParameters.videoSize = {
        static_cast<float>(resizedVideoSize.width()),
        static_cast<float>(resizedVideoSize.height()),
    };
    compositor.render(*commandBuffer, *outputTarget, outputSize, resizedParameters);

    bool resizedVideoReadbackCompleted = false;
    bool resizedOutputReadbackCompleted = false;
    QRhiReadbackResult resizedVideoReadback;
    QRhiReadbackResult resizedOutputReadback;
    resizedVideoReadback.completed = [&resizedVideoReadbackCompleted] { resizedVideoReadbackCompleted = true; };
    resizedOutputReadback.completed = [&resizedOutputReadbackCompleted] { resizedOutputReadbackCompleted = true; };
    updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(&producer->textureForComposition()), &resizedVideoReadback);
    updates->readBackTexture(QRhiReadbackDescription(outputTexture.get()), &resizedOutputReadback);
    commandBuffer->resourceUpdate(updates);
    QRhi::FrameOpResult const resizedFrameResult = rhi->endOffscreenFrame();
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
    QCOMPARE(resizedVideoReadback.data.size(), qsizetype(resizedVideoSize.width() * resizedVideoSize.height() * 8));
    QCOMPARE(resizedOutputReadback.pixelSize, outputSize);
    QCOMPARE(resizedOutputReadback.format, QRhiTexture::RGBA8);
    QCOMPARE(resizedOutputReadback.data.size(), qsizetype(outputSize.width() * outputSize.height() * 4));
    QVERIFY(!producer->needsRender(resizedState));

    FloatPixel const resizedProducerPixel = readFloatPixel(resizedVideoReadback, *rhi, sampleX, 1);
    compareNear(resizedProducerPixel.r, expectedRamp(sampleX, resizedVideoSize.width()), 0.002f);
    BytePixel const resizedComposedPixel =
        readBytePixel(resizedOutputReadback, *rhi, videoOriginX + sampleX, videoOriginY + 1);
    compareByteNear(resizedComposedPixel.r, encodedByte(expectedRamp(sampleX, resizedVideoSize.width())));
#endif
}

void QrhiCompositorTest::libplaceboD3d11SurfaceAndCompositionReadback() {
#ifndef Q_OS_WIN
    QSKIP("The current libplacebo integration is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphicsDevice, "Could not create the shared QRhi/libplacebo D3D11 domain");
    QVERIFY(graphicsDevice->libplaceboContext().isValid());
    QRhi& rhi = graphicsDevice->rhi();

    DiagnosticVideoSource source(VideoProducerApi::Libplacebo, VideoTargetReadback::Enabled, videoSize);
    QVERIFY(source.useLibplacebo());
    std::uint64_t const libplaceboRevision = source.producerConfigurationRevision();
    source.setUseLibplacebo(false);
    QVERIFY(!source.useLibplacebo());
    QVERIFY(source.producerConfigurationRevision() != libplaceboRevision);
    std::unique_ptr<RenderedVideoProducer> comparisonProducer = source.createProducer(*graphicsDevice);
    QVERIFY(comparisonProducer);
    RenderedVideoProducerDiagnostics const comparisonDiagnostics = comparisonProducer->diagnostics();
    QCOMPARE(comparisonDiagnostics.producerName, QStringLiteral("Diagnostic pattern"));
    comparisonProducer.reset();
    std::uint64_t const proceduralRevision = source.producerConfigurationRevision();
    source.setUseLibplacebo(true);
    QVERIFY(source.useLibplacebo());
    QVERIFY(source.producerConfigurationRevision() != proceduralRevision);
    source.setSourcePeakHeadroom(1.0f);
    source.setToneMappingEnabled(false);
    source.setAnimatePattern(false);
    std::unique_ptr<RenderedVideoProducer> producer = source.createProducer(*graphicsDevice);
    QVERIFY(producer);
    auto* const libplaceboProducer = dynamic_cast<LibplaceboDiagnosticVideoProducer*>(producer.get());
    QVERIFY(libplaceboProducer);

    RenderedVideoSurfaceState sdrState = surfaceState();
    sdrState.graphicsDeviceGeneration = graphicsDevice->generation();
    sdrState.contentRevision = source.contentRevision();
    QRhiCommandBuffer* commandBuffer = nullptr;
    constexpr int centerX = 8;
    constexpr int grayscaleY = 1;
    constexpr int spectrumY = 5;
    constexpr int steppedY = 10;
    float const expectedSdr = analyticRamp(centerX, videoSize.width(), 1.0f);

    auto const captureProducerSurface = [&](RenderedVideoSurfaceState const& state,
                                            QRhiReadbackResult& readback) -> QString {
        if (producer->ensureSurface(state) != VideoOperationResult::Ready) {
            return QStringLiteral("Could not provision the producer surface");
        }
        if (rhi.beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess || !commandBuffer) {
            return QStringLiteral("Could not begin the producer capture frame");
        }
        if (producer->render(*commandBuffer, state) != VideoOperationResult::Ready ||
            producer->prepareForComposition(*commandBuffer) != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            producer->submissionAborted();
            producer->discardPendingRender();
            return QStringLiteral("Could not render the producer surface");
        }

        bool completed = false;
        readback.completed = [&completed] { completed = true; };
        QRhiResourceUpdateBatch* captureUpdates = rhi.nextResourceUpdateBatch();
        captureUpdates->readBackTexture(QRhiReadbackDescription(&producer->textureForComposition()), &readback);
        commandBuffer->resourceUpdate(captureUpdates);
        QRhi::FrameOpResult const frameResult = rhi.endOffscreenFrame();
        if (frameResult == QRhi::FrameOpSuccess) {
            producer->submissionAccepted();
            producer->commitPendingRender();
        } else {
            producer->submissionAborted();
            producer->discardPendingRender();
        }
        if (frameResult != QRhi::FrameOpSuccess || !completed) {
            return QStringLiteral("Could not complete the producer readback");
        }
        return {};
    };

    std::array<float, 3> const referenceWhites{
        80.0f,
        100.0f,
        PL_COLOR_SDR_WHITE,
    };
    for (float const referenceWhite : referenceWhites) {
        sdrState.description.referenceWhiteNits = referenceWhite;
        sdrState.description.targetPeakHeadroom = 4.0f;

        QRhiReadbackResult sdrReadback;
        QString const captureError = captureProducerSurface(sdrState, sdrReadback);
        QVERIFY2(captureError.isEmpty(), qPrintable(captureError));
        QCOMPARE(sdrReadback.format, QRhiTexture::RGBA16F);
        QCOMPARE(sdrReadback.pixelSize, videoSize);
        QVERIFY(!producer->needsRender(sdrState));

        FloatPixel const sdrNeutral = readFloatPixel(sdrReadback, rhi, centerX, grayscaleY);
        compareNear(sdrNeutral.r, expectedSdr, 0.01f);
        compareNear(sdrNeutral.g, expectedSdr, 0.01f);
        compareNear(sdrNeutral.b, expectedSdr, 0.01f);
        compareNear(sdrNeutral.a, 1.0f, 0.002f);

        FloatPixel const sdrSpectrum = readFloatPixel(sdrReadback, rhi, centerX, spectrumY);
        std::array<float, 3> const expectedSdrSpectrum = analyticSpectrum(centerX, videoSize.width(), 1.0f);
        compareNear(sdrSpectrum.r, expectedSdrSpectrum[0], 0.015f);
        compareNear(sdrSpectrum.g, expectedSdrSpectrum[1], 0.015f);
        compareNear(sdrSpectrum.b, expectedSdrSpectrum[2], 0.015f);
        FloatPixel const sdrStep = readFloatPixel(sdrReadback, rhi, centerX, steppedY);
        float const expectedSdrStep = analyticStep(centerX, videoSize.width(), 1.0f);
        compareNear(sdrStep.r, expectedSdrStep, 0.01f);
        compareNear(sdrStep.g, expectedSdrStep, 0.01f);
        compareNear(sdrStep.b, expectedSdrStep, 0.01f);
    }

    RenderedVideoProducerDiagnostics const diagnostics = producer->diagnostics();
    QVERIFY(diagnostics.isValid());
    QVERIFY(diagnostics.producerName.contains(QStringLiteral("libplacebo")));
    QCOMPARE(diagnostics.target.outputPath, VideoOutputPath::DirectRenderTarget);
    QCOMPARE(diagnostics.knownInputCpuTransfersPerInputFrame, 1U);
    QCOMPARE(diagnostics.target.knownOutputGpuCopiesPerRender, 0U);
    QCOMPARE(diagnostics.target.knownOutputCpuTransfersPerRender, 0U);
    QVERIFY(diagnostics.target.synchronizationMode.contains(QStringLiteral("D3D11")));
    QVERIFY(diagnostics.target.synchronizationMode.contains(QStringLiteral("external commands")));
    QVERIFY(diagnostics.target.synchronizationMode.contains(QStringLiteral("rgba16"), Qt::CaseInsensitive));
    QVERIFY(diagnostics.target.fallbackReason.isEmpty());

    std::unique_ptr<QRhiTexture> uiTexture(rhi.newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(uiTexture->create());
    std::unique_ptr<QRhiTexture> subtitleTexture(rhi.newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
    QVERIFY(subtitleTexture->create());
    std::unique_ptr<QRhiTexture> outputTexture(rhi.newTexture(
        QRhiTexture::RGBA16F, videoSize, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(outputTexture->create());
    QRhiTextureRenderTargetDescription const outputDescription(QRhiColorAttachment(outputTexture.get()));
    std::unique_ptr<QRhiTextureRenderTarget> outputTarget(rhi.newTextureRenderTarget(outputDescription));
    std::unique_ptr<QRhiRenderPassDescriptor> outputPass(outputTarget->newCompatibleRenderPassDescriptor());
    outputTarget->setRenderPassDescriptor(outputPass.get());
    QVERIFY(outputTarget->create());

    HdrCompositor compositor(rhi);
    QCOMPARE(compositor.initialize(*outputPass, &producer->textureForComposition(), subtitleTexture.get(), *uiTexture),
             HdrCompositor::ResourceResult::Ready);

    // With a 7x pattern peak, the first nonzero step is exactly 1.0 in
    // libplacebo's PQ normalization: one 203-nit HDR reference-white patch.
    // Leave target minimum unknown here to isolate the white anchor from
    // black-point adaptation, which the surrounding captures cover.
    source.setSourcePeakHeadroom(exactReferenceWhitePatternPeak);
    source.setToneMappingEnabled(true);
    RenderedVideoSurfaceState referenceWhiteState = sdrState;
    referenceWhiteState.description.referenceWhiteNits = 100.0f;
    referenceWhiteState.description.targetMinimumLuminanceKnown = false;
    referenceWhiteState.description.targetMinimumLuminanceNits = 0.0f;
    referenceWhiteState.description.targetPeakHeadroom = 10.0f;
    referenceWhiteState.contentRevision = source.contentRevision();
    QRhiReadbackResult referenceWhiteReadback;
    QString const referenceWhiteCaptureError = captureProducerSurface(referenceWhiteState, referenceWhiteReadback);
    QVERIFY2(referenceWhiteCaptureError.isEmpty(), qPrintable(referenceWhiteCaptureError));
    constexpr int referenceWhiteX = 2;
    compareNear(analyticStep(referenceWhiteX, videoSize.width(), exactReferenceWhitePatternPeak), 1.0f, 0.0001f);
    FloatPixel const referenceWhitePatch = readFloatPixel(referenceWhiteReadback, rhi, referenceWhiteX, steppedY);
    compareNear(referenceWhitePatch.r, 1.0f, 0.02f);
    compareNear(referenceWhitePatch.g, 1.0f, 0.02f);
    compareNear(referenceWhitePatch.b, 1.0f, 0.02f);

    std::uint64_t const referencePatchUploadCount = libplaceboProducer->sourceUploadCount();
    source.setSourcePeakHeadroom(sourcePeak);
    // Hold one 1000-nit PQ signal fixed while changing only the output
    // reference white. A 600-nit display has 6x headroom at 100-nit white, so
    // the source fits without highlight compression.
    RenderedVideoSurfaceState hdr100State = sdrState;
    hdr100State.description.referenceWhiteNits = 100.0f;
    hdr100State.description.targetPeakHeadroom = physicalTargetPeakNits / hdr100State.description.referenceWhiteNits;
    hdr100State.contentRevision = source.contentRevision();
    QRhiReadbackResult hdr100Readback;
    QString const hdr100CaptureError = captureProducerSurface(hdr100State, hdr100Readback);
    QVERIFY2(hdr100CaptureError.isEmpty(), qPrintable(hdr100CaptureError));
    FloatPixel const hdr100Right = readFloatPixel(hdr100Readback, rhi, 13, grayscaleY);
    std::uint64_t const fixedPqUploadCount = libplaceboProducer->sourceUploadCount();
    QCOMPARE(fixedPqUploadCount, referencePatchUploadCount + 1);
    QVERIFY(hdr100Right.r > 1.0f);
    QVERIFY(hdr100Right.r < sourcePeak);
    compareNear(hdr100Right.r, analyticRamp(13, videoSize.width(), sourcePeak), 0.04f);

    // The same fixed source also fits at 80-nit white. The rendered-video
    // surface remains reference-white-relative, so its samples stay stable;
    // platform presentation changes their physical luminance later.
    RenderedVideoSurfaceState hdr80State = hdr100State;
    hdr80State.description.referenceWhiteNits = 80.0f;
    hdr80State.description.targetPeakHeadroom = physicalTargetPeakNits / hdr80State.description.referenceWhiteNits;
    QRhiReadbackResult hdr80Readback;
    QString const hdr80CaptureError = captureProducerSurface(hdr80State, hdr80Readback);
    QVERIFY2(hdr80CaptureError.isEmpty(), qPrintable(hdr80CaptureError));
    FloatPixel const hdr80Right = readFloatPixel(hdr80Readback, rhi, 13, grayscaleY);
    QCOMPARE(libplaceboProducer->sourceUploadCount(), fixedPqUploadCount);
    compareNear(hdr80Right.r, hdr100Right.r, 0.03f);
    compareNear(hdr80Right.g, hdr100Right.g, 0.03f);
    compareNear(hdr80Right.b, hdr100Right.b, 0.03f);

    // Raising reference white to 203 nits reduces the same physical display
    // to less than 3x headroom. The 1000-nit source no longer fits, so
    // libplacebo must compress highlights into the declared target.
    RenderedVideoSurfaceState hdrState = hdr80State;
    hdrState.description.referenceWhiteNits = PL_COLOR_SDR_WHITE;
    hdrState.description.targetPeakHeadroom = physicalTargetPeakNits / hdrState.description.referenceWhiteNits;
    QCOMPARE(producer->ensureSurface(hdrState), VideoOperationResult::Ready);
    QVERIFY(producer->needsRender(hdrState));

    QCOMPARE(rhi.beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QByteArray const transparentUi(4, '\0');
    QRhiResourceUpdateBatch* updates = rhi.nextResourceUpdateBatch();
    updates->uploadTexture(uiTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    updates->uploadTexture(subtitleTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                      0, 0, QRhiTextureSubresourceUploadDescription(transparentUi))));
    commandBuffer->resourceUpdate(updates);
    QCOMPARE(producer->render(*commandBuffer, hdrState), VideoOperationResult::Ready);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);

    HdrCompositorParameters parameters;
    parameters.viewportSize = {
        static_cast<float>(videoSize.width()),
        static_cast<float>(videoSize.height()),
    };
    parameters.videoOrigin = {0.0f, 0.0f};
    parameters.videoSize = parameters.viewportSize;
    parameters.sdrScale = hdrState.description.referenceWhiteNits / 80.0f;
    parameters.ndcYUp = rhi.isYUpInNDC() ? 1.0f : 0.0f;
    parameters.outputEncoding = 2.0f;
    compositor.render(*commandBuffer, *outputTarget, videoSize, parameters);

    bool hdrReadbackCompleted = false;
    bool compositionReadbackCompleted = false;
    QRhiReadbackResult hdrReadback;
    QRhiReadbackResult compositionReadback;
    hdrReadback.completed = [&hdrReadbackCompleted] { hdrReadbackCompleted = true; };
    compositionReadback.completed = [&compositionReadbackCompleted] { compositionReadbackCompleted = true; };
    updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(&producer->textureForComposition()), &hdrReadback);
    updates->readBackTexture(QRhiReadbackDescription(outputTexture.get()), &compositionReadback);
    commandBuffer->resourceUpdate(updates);
    QRhi::FrameOpResult const hdrFrameResult = rhi.endOffscreenFrame();
    if (hdrFrameResult == QRhi::FrameOpSuccess) {
        producer->submissionAccepted();
        producer->commitPendingRender();
    } else {
        producer->submissionAborted();
        producer->discardPendingRender();
    }
    QCOMPARE(hdrFrameResult, QRhi::FrameOpSuccess);
    QCOMPARE(libplaceboProducer->sourceUploadCount(), fixedPqUploadCount);
    QVERIFY(hdrReadbackCompleted);
    QVERIFY(compositionReadbackCompleted);
    QCOMPARE(hdrReadback.format, QRhiTexture::RGBA16F);
    QCOMPARE(compositionReadback.format, QRhiTexture::RGBA16F);
    QVERIFY(!producer->needsRender(hdrState));

    FloatPixel const hdrLeft = readFloatPixel(hdrReadback, rhi, 2, grayscaleY);
    FloatPixel const hdrRight = readFloatPixel(hdrReadback, rhi, 13, grayscaleY);
    QVERIFY(std::isfinite(hdrLeft.r));
    QVERIFY(std::isfinite(hdrRight.r));
    QVERIFY(hdrRight.r > hdrLeft.r);
    QVERIFY(hdrRight.r > 1.0f);
    QVERIFY(hdrRight.r <= hdrState.description.targetPeakHeadroom + 0.03f);
    QVERIFY(hdrRight.r < hdr100Right.r - 0.05f);
    compareNear(hdrRight.r, hdrRight.g, 0.02f);
    compareNear(hdrRight.g, hdrRight.b, 0.02f);
    compareNear(hdrRight.a, 1.0f, 0.002f);

    FloatPixel const hdrSpectrum = readFloatPixel(hdrReadback, rhi, centerX, spectrumY);
    QVERIFY(std::abs(hdrSpectrum.r - hdrSpectrum.g) > 0.01f || std::abs(hdrSpectrum.g - hdrSpectrum.b) > 0.01f);
    FloatPixel const hdrStep = readFloatPixel(hdrReadback, rhi, centerX, steppedY);
    compareNear(hdrStep.r, hdrStep.g, 0.02f);
    compareNear(hdrStep.g, hdrStep.b, 0.02f);

    FloatPixel const composed = readFloatPixel(compositionReadback, rhi, 13, grayscaleY);
    compareNear(composed.r, hdrRight.r * parameters.sdrScale, 0.03f);
    compareNear(composed.g, hdrRight.g * parameters.sdrScale, 0.03f);
    compareNear(composed.b, hdrRight.b * parameters.sdrScale, 0.03f);
    compareNear(composed.a, 1.0f, 0.002f);

    source.setSourcePeakHeadroom(1.0f);
    source.setToneMappingEnabled(false);
    RenderedVideoSurfaceState resizedState = hdrState;
    resizedState.description.pixelSize = {8, 6};
    resizedState.contentRevision = source.contentRevision();
    std::uint64_t const previousRevision = producer->compositionTextureRevision();
    QCOMPARE(producer->ensureSurface(resizedState), VideoOperationResult::Ready);
    QVERIFY(producer->compositionTextureRevision() > previousRevision);
    QCOMPARE(producer->textureForComposition().pixelSize(), QSize(8, 6));
    QCOMPARE(rhi.beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QCOMPARE(producer->render(*commandBuffer, resizedState), VideoOperationResult::Ready);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    bool resizedReadbackCompleted = false;
    QRhiReadbackResult resizedReadback;
    resizedReadback.completed = [&resizedReadbackCompleted] { resizedReadbackCompleted = true; };
    updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(&producer->textureForComposition()), &resizedReadback);
    commandBuffer->resourceUpdate(updates);
    QRhi::FrameOpResult const resizedFrameResult = rhi.endOffscreenFrame();
    QCOMPARE(resizedFrameResult, QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->commitPendingRender();
    QVERIFY(resizedReadbackCompleted);
    QCOMPARE(resizedReadback.pixelSize, QSize(8, 6));
    FloatPixel const resizedPixel = readFloatPixel(resizedReadback, rhi, 6, 0);
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
    QCOMPARE(producer->ensureSurface(switchedState), VideoOperationResult::Ready);
    QCOMPARE(rhi.beginOffscreenFrame(&commandBuffer), QRhi::FrameOpSuccess);
    QCOMPARE(producer->render(*commandBuffer, switchedState), VideoOperationResult::Ready);
    QCOMPARE(producer->prepareForComposition(*commandBuffer), VideoOperationResult::Ready);
    QCOMPARE(compositor.setTextures(&producer->textureForComposition(), subtitleTexture.get(), *uiTexture),
             HdrCompositor::ResourceResult::Ready);
    compositor.render(*commandBuffer, *outputTarget, videoSize, parameters);

    bool switchedSurfaceCompleted = false;
    bool switchedCompositionCompleted = false;
    QRhiReadbackResult switchedSurfaceReadback;
    QRhiReadbackResult switchedCompositionReadback;
    switchedSurfaceReadback.completed = [&switchedSurfaceCompleted] { switchedSurfaceCompleted = true; };
    switchedCompositionReadback.completed = [&switchedCompositionCompleted] { switchedCompositionCompleted = true; };
    updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(&producer->textureForComposition()), &switchedSurfaceReadback);
    updates->readBackTexture(QRhiReadbackDescription(outputTexture.get()), &switchedCompositionReadback);
    commandBuffer->resourceUpdate(updates);
    QRhi::FrameOpResult const switchedFrameResult = rhi.endOffscreenFrame();
    QCOMPARE(switchedFrameResult, QRhi::FrameOpSuccess);
    producer->submissionAccepted();
    producer->commitPendingRender();
    QVERIFY(switchedSurfaceCompleted);
    QVERIFY(switchedCompositionCompleted);

    RenderedVideoProducerDiagnostics const switchedDiagnostics = producer->diagnostics();
    QCOMPARE(switchedDiagnostics.producerName, QStringLiteral("Diagnostic pattern"));
    QCOMPARE(switchedDiagnostics.knownInputCpuTransfersPerInputFrame, 0U);
    FloatPixel const switchedSurface = readFloatPixel(switchedSurfaceReadback, rhi, 13, grayscaleY);
    FloatPixel const switchedComposition = readFloatPixel(switchedCompositionReadback, rhi, 13, grayscaleY);
    float const expectedSwitched = analyticRamp(13, videoSize.width(), 1.0f);
    compareNear(switchedSurface.r, expectedSwitched, 0.015f);
    compareNear(switchedComposition.r, switchedSurface.r * parameters.sdrScale, 0.03f);
    compareNear(switchedComposition.g, switchedSurface.g * parameters.sdrScale, 0.03f);
    compareNear(switchedComposition.b, switchedSurface.b * parameters.sdrScale, 0.03f);
#endif
}

void QrhiCompositorTest::libplaceboTargetGamutBoundary() {
#ifndef Q_OS_WIN
    QSKIP("The current libplacebo integration is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY2(graphicsDevice, "Could not create the shared QRhi/libplacebo D3D11 domain");
    LibplaceboGraphicsContext const& graphics = graphicsDevice->libplaceboContext();
    QVERIFY(graphics.isValid());

    std::unique_ptr<VideoTargetInterop> target = graphicsDevice->createVideoTarget({
        .producerApi = VideoProducerApi::Libplacebo,
        .readback = VideoTargetReadback::Enabled,
    });
    QVERIFY(target);

    RenderedVideoSurfaceDescription description = surfaceState().description;
    description.pixelSize = {2, 1};
    description.referenceWhiteNits = PL_COLOR_SDR_WHITE;
    description.targetMinimumLuminanceKnown = false;
    description.targetMinimumLuminanceNits = 0.0f;
    description.targetPeakHeadroom = 1.0f;

    pl_fmt const sourceFormat = pl_find_named_fmt(graphics.gpu, "rgba32f");
    QVERIFY(sourceFormat);
    pl_tex_params sourceParameters{};
    sourceParameters.w = 2;
    sourceParameters.h = 1;
    sourceParameters.format = sourceFormat;
    sourceParameters.sampleable = true;
    sourceParameters.host_writable = true;
    pl_tex sourceTexture = pl_tex_create(graphics.gpu, &sourceParameters);
    QVERIFY(sourceTexture);
    auto const releaseSource = qScopeGuard([&] { pl_tex_destroy(graphics.gpu, &sourceTexture); });

    std::array<float, 8> sourcePixels{
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
    };
    pl_frame source{};
    source.num_planes = 1;
    source.planes[0].texture = sourceTexture;
    source.planes[0].components = 4;
    source.planes[0].component_mapping[0] = 0;
    source.planes[0].component_mapping[1] = 1;
    source.planes[0].component_mapping[2] = 2;
    source.planes[0].component_mapping[3] = 3;
    source.repr = pl_color_repr_rgb;
    source.repr.alpha = PL_ALPHA_INDEPENDENT;
    source.color = pl_color_space_srgb;
    source.color.primaries = PL_COLOR_PRIM_DISPLAY_P3;
    source.color.hdr.prim = *pl_raw_primaries_get(PL_COLOR_PRIM_DISPLAY_P3);
    source.crop = {0.0f, 0.0f, 2.0f, 1.0f};

    LibplaceboRenderContext renderContext(graphics);
    QVERIFY(renderContext.isValid());
    QRhi& rhi = graphicsDevice->rhi();

    struct Capture {
        FloatPixel red;
        FloatPixel white;
    };
    auto const capture = [&](std::optional<ColorPrimaries> targetPrimaries, Capture& result) -> QString {
        description.targetPrimariesKnown = targetPrimaries.has_value();
        description.targetPrimaries = targetPrimaries.value_or(ColorPrimaries{});
        VideoTargetUpdate const targetUpdate = target->ensureTarget(description);
        if (targetUpdate == VideoTargetUpdate::Unavailable || targetUpdate == VideoTargetUpdate::DeviceLost) {
            return QStringLiteral("Could not provision the libplacebo gamut target");
        }

        QRhiCommandBuffer* commandBuffer = nullptr;
        if (rhi.beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess || !commandBuffer) {
            return QStringLiteral("Could not begin the gamut capture frame");
        }
        if (target->beginProducerAccess(*commandBuffer) != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            target->submissionAborted();
            return QStringLiteral("Could not begin libplacebo target access");
        }

        pl_tex_transfer_params upload{};
        upload.tex = sourceTexture;
        upload.row_pitch = 2 * 4 * sizeof(float);
        upload.ptr = sourcePixels.data();
        QString renderError;
        bool const rendered = pl_tex_upload(graphics.gpu, &upload) &&
                              renderContext.render(source, target->libplaceboRenderTarget(), description, false,
                                                   &renderError);
        VideoOperationResult const endResult = target->endProducerAccess(*commandBuffer);
        if (!rendered || endResult != VideoOperationResult::Ready ||
            target->prepareForComposition(*commandBuffer) != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            target->submissionAborted();
            return renderError.isEmpty() ? QStringLiteral("Could not render the gamut capture") : renderError;
        }

        bool completed = false;
        QRhiReadbackResult readback;
        readback.completed = [&completed] { completed = true; };
        QRhiResourceUpdateBatch* updates = rhi.nextResourceUpdateBatch();
        updates->readBackTexture(QRhiReadbackDescription(&target->textureForComposition()), &readback);
        commandBuffer->resourceUpdate(updates);
        QRhi::FrameOpResult const frameResult = rhi.endOffscreenFrame();
        if (frameResult != QRhi::FrameOpSuccess) {
            target->submissionAborted();
            return QStringLiteral("Could not submit the gamut capture frame");
        }
        target->submissionAccepted();
        if (!completed || readback.format != QRhiTexture::RGBA16F) {
            return QStringLiteral("Could not read the gamut capture");
        }

        result.red = readFloatPixel(readback, rhi, 0, 0);
        result.white = readFloatPixel(readback, rhi, 1, 0);
        return {};
    };

    Capture fallback;
    QString const fallbackError = capture(std::nullopt, fallback);
    QVERIFY2(fallbackError.isEmpty(), qPrintable(fallbackError));
    Capture explicitBt709;
    QString const explicitBt709Error = capture(bt709Primaries(), explicitBt709);
    QVERIFY2(explicitBt709Error.isEmpty(), qPrintable(explicitBt709Error));
    Capture displayP3;
    QString const displayP3Error = capture(displayP3Primaries(), displayP3);
    QVERIFY2(displayP3Error.isEmpty(), qPrintable(displayP3Error));

    QVERIFY(fallback.red.r >= -0.01f && fallback.red.r <= 1.01f);
    QVERIFY(fallback.red.g >= -0.01f && fallback.red.g <= 1.01f);
    QVERIFY(fallback.red.b >= -0.01f && fallback.red.b <= 1.01f);
    compareNear(fallback.red.r, explicitBt709.red.r, 0.005f);
    compareNear(fallback.red.g, explicitBt709.red.g, 0.005f);
    compareNear(fallback.red.b, explicitBt709.red.b, 0.005f);
    compareNear(fallback.red.a, explicitBt709.red.a, 0.005f);
    QVERIFY(displayP3.red.r > 1.10f);
    QVERIFY(displayP3.red.g < -0.02f);
    QVERIFY(displayP3.red.b < -0.005f);
    QVERIFY(displayP3.red.r > fallback.red.r + 0.10f);
    compareNear(displayP3.red.r, 1.22494f, 0.02f);
    compareNear(displayP3.red.g, -0.04206f, 0.01f);
    compareNear(displayP3.red.b, -0.01964f, 0.01f);
    for (FloatPixel const neutral : {fallback.white, explicitBt709.white, displayP3.white}) {
        compareNear(neutral.r, 1.0f, 0.005f);
        compareNear(neutral.g, 1.0f, 0.005f);
        compareNear(neutral.b, 1.0f, 0.005f);
        compareNear(neutral.a, 1.0f, 0.005f);
    }
#endif
}

void QrhiCompositorTest::libplaceboAnimatedDiagnosticThroughput() {
#ifndef Q_OS_WIN
    QSKIP("The current libplacebo integration is Windows D3D11");
#else
    constexpr QSize inputFrameSize{640, 360};
    constexpr QSize targetSize{1100, 600};
    constexpr int measuredFrames = 60;

    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice = GraphicsBackendFactory::createDeviceDomain();
    QVERIFY(graphicsDevice);
    QRhi& rhi = graphicsDevice->rhi();
    DiagnosticVideoSource source(VideoProducerApi::Libplacebo, VideoTargetReadback::Disabled, inputFrameSize);
    source.setSourcePeakHeadroom(12.5f);
    source.setToneMappingEnabled(true);
    source.setAnimatePattern(true);
    std::unique_ptr<RenderedVideoProducer> producer = source.createProducer(*graphicsDevice);
    QVERIFY(producer);

    RenderedVideoSurfaceState state = surfaceState();
    state.description.pixelSize = targetSize;
    state.description.referenceWhiteNits = 203.0f;
    state.description.targetMinimumLuminanceKnown = true;
    state.description.targetMinimumLuminanceNits = 0.005f;
    state.description.targetPeakHeadroom = 4.0f;
    state.graphicsDeviceGeneration = graphicsDevice->generation();
    state.contentRevision = source.contentRevision();

    auto timestamp = std::chrono::steady_clock::now();
    auto const renderFrame = [&]() {
        timestamp += std::chrono::milliseconds(16);
        source.prepareForPresentation(timestamp);
        state.contentRevision = source.contentRevision();
        if (producer->ensureSurface(state) != VideoOperationResult::Ready) {
            return false;
        }
        QRhiCommandBuffer* commandBuffer = nullptr;
        if (rhi.beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess || !commandBuffer) {
            return false;
        }
        if (producer->render(*commandBuffer, state) != VideoOperationResult::Ready ||
            producer->prepareForComposition(*commandBuffer) != VideoOperationResult::Ready) {
            rhi.endOffscreenFrame(QRhi::SkipPresent);
            producer->submissionAborted();
            producer->discardPendingRender();
            return false;
        }
        QRhi::FrameOpResult const result = rhi.endOffscreenFrame();
        if (result != QRhi::FrameOpSuccess) {
            producer->submissionAborted();
            producer->discardPendingRender();
            return false;
        }
        producer->submissionAccepted();
        producer->commitPendingRender();
        return true;
    };

    for (int i = 0; i < 3; ++i) {
        QVERIFY(renderFrame());
    }

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < measuredFrames; ++i) {
        QVERIFY(renderFrame());
    }
    qint64 const elapsedNanoseconds = timer.nsecsElapsed();
    double const millisecondsPerFrame = static_cast<double>(elapsedNanoseconds) / 1'000'000.0 / measuredFrames;
    double const framesPerSecond = 1000.0 / millisecondsPerFrame;
    qInfo().nospace() << "libplacebo diagnostic throughput: " << QString::number(framesPerSecond, 'f', 1)
                      << " FPS CPU submission (" << QString::number(millisecondsPerFrame, 'f', 2) << " ms/frame), "
                      << inputFrameSize.width() << "x" << inputFrameSize.height() << " input -> " << targetSize.width()
                      << "x" << targetSize.height() << " target";

    RenderedVideoProducerDiagnostics const diagnostics = producer->diagnostics();
    QCOMPARE(diagnostics.knownInputCpuTransfersPerInputFrame, 1U);
    QVERIFY(diagnostics.inputPath.contains(QStringLiteral("640×360")));
#endif
}

// QTEST_MAIN would instantiate QGuiApplication because this target links Qt
// Gui for QRhi. This test is deliberately headless but needs QCoreApplication
// lifetime for QRhi's process services and orderly teardown.
QTEST_GUILESS_MAIN(QrhiCompositorTest)
#include "tst_QrhiCompositor.moc"
