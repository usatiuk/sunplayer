#include "video/LibplaceboDecodedVideoProducer.h"

#include <utility>

#include <QtCore/qlogging.h>
#include <rhi/qrhi.h>

#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "video/DecodedVideoSource.h"
#include "video/libplacebo/LibplaceboRenderContext.h"

LibplaceboDecodedVideoProducer::
LibplaceboDecodedVideoProducer(
        GraphicsDeviceDomain &graphicsDevice,
        const DecodedVideoSource &source,
        VideoTargetReadback readback)
    : m_rhi(graphicsDevice.rhi()),
      m_gpu(graphicsDevice.libplaceboContext().gpu),
      m_source(source),
      m_target(graphicsDevice.createVideoTarget({
          .producerApi = VideoProducerApi::Libplacebo,
          .readback = readback,
      })) {
    const LibplaceboGraphicsContext &context =
        graphicsDevice.libplaceboContext();
    if (!context.isValid()) {
        m_failureReason =
            QStringLiteral(
                "Libplacebo graphics context is unavailable");
        return;
    }
    if (!m_target) {
        m_failureReason =
            QStringLiteral(
                "The graphics backend does not provide "
                "a libplacebo target");
        return;
    }

    m_renderContext =
        std::make_unique<LibplaceboRenderContext>(context);
    if (!m_renderContext->isValid()) {
        m_failureReason =
            QStringLiteral(
                "Could not create the libplacebo renderer");
        return;
    }
    m_importer =
        std::make_unique<LibplaceboFrameImporter>(
            m_gpu,
            graphicsDevice.generation(),
            graphicsDevice.createHardwareFrameImporter());
}

LibplaceboDecodedVideoProducer::
~LibplaceboDecodedVideoProducer() = default;

VideoOperationResult
LibplaceboDecodedVideoProducer::ensureSurface(
        const RenderedVideoSurfaceState &requestedState) {
    Q_ASSERT(requestedState.isValid());
    if (!m_target
            || !m_renderContext
            || !m_renderContext->isValid()
            || !m_importer) {
        return unavailable(m_failureReason.isEmpty()
            ? QStringLiteral(
                "Decoded libplacebo video path is unavailable")
            : m_failureReason);
    }
    if (!m_source.currentFrame()) {
        return unavailable(QStringLiteral(
            "No decoded video frame is available"));
    }

    const VideoTargetUpdate update =
        m_target->ensureTarget(requestedState.description);
    if (update == VideoTargetUpdate::DeviceLost)
        return VideoOperationResult::DeviceLost;
    if (update == VideoTargetUpdate::Unavailable) {
        return unavailable(
            m_target->diagnostics().fallbackReason);
    }
    if (update == VideoTargetUpdate::Created
            || update == VideoTargetUpdate::Resized) {
        m_completedState.reset();
        m_pendingState.reset();
    }

    m_failureReason.clear();
    m_failureKind = VideoFailureKind::None;
    return VideoOperationResult::Ready;
}

bool LibplaceboDecodedVideoProducer::needsRender(
        const RenderedVideoSurfaceState &requestedState) const {
    return !m_completedState
        || !m_completedState->isReusableFor(requestedState);
}

VideoOperationResult
LibplaceboDecodedVideoProducer::render(
        QRhiCommandBuffer &commandBuffer,
        const RenderedVideoSurfaceState &requestedState) {
    Q_ASSERT(requestedState.isValid());
    Q_ASSERT(m_target);
    Q_ASSERT(m_renderContext && m_renderContext->isValid());
    Q_ASSERT(m_importer);
    Q_ASSERT(!m_pendingState);

    const std::shared_ptr<const DecodedVideoFrame> frame =
        m_source.currentFrame();
    if (!frame) {
        return unavailable(QStringLiteral(
            "No decoded video frame is available"));
    }

    const VideoOperationResult beginResult =
        m_target->beginProducerAccess(commandBuffer);
    if (beginResult != VideoOperationResult::Ready)
        return beginResult;

    QString renderError;
    VideoFrameImportFailure importFailure =
        VideoFrameImportFailure::None;
    if (!m_mapping || m_mappedSourceFrame != frame) {
        m_mapping.reset();
        m_mappedSourceFrame.reset();
        m_mapping = m_importer->map(
            *frame, &renderError);
        if (m_mapping) {
            m_mappedSourceFrame = frame;
        } else {
            importFailure =
                m_importer->lastDiagnostics().failure;
        }
    }

    const bool rendered =
        m_mapping
        && m_renderContext->render(
            m_mapping->frame(),
            m_target->libplaceboRenderTarget(),
            requestedState.description,
            true,
            &renderError);

    const VideoOperationResult endResult =
        m_target->endProducerAccess(commandBuffer);
    if (endResult == VideoOperationResult::DeviceLost
            || deviceLost()) {
        m_failureReason =
            QStringLiteral(
                "Graphics device lost during decoded video rendering");
        return VideoOperationResult::DeviceLost;
    }
    if (endResult != VideoOperationResult::Ready) {
        return unavailable(QStringLiteral(
            "Could not complete decoded-video producer access"));
    }
    if (!rendered) {
        return unavailable(renderError.isEmpty()
            ? QStringLiteral(
                "Could not import or render decoded video frame")
            : renderError,
            importFailure
                    == VideoFrameImportFailure::
                        NativeHardwareImportUnavailable
                ? VideoFailureKind::
                    HardwareFrameImportUnavailable
                : VideoFailureKind::General);
    }

    m_failureReason.clear();
    m_failureKind = VideoFailureKind::None;
    m_pendingState = requestedState;
    return VideoOperationResult::Ready;
}

VideoOperationResult
LibplaceboDecodedVideoProducer::prepareForComposition(
        QRhiCommandBuffer &commandBuffer) {
    return m_target
        ? m_target->prepareForComposition(commandBuffer)
        : VideoOperationResult::Unavailable;
}

void LibplaceboDecodedVideoProducer::submissionAccepted() {
    if (m_target)
        m_target->submissionAccepted();
}

void LibplaceboDecodedVideoProducer::submissionAborted() {
    if (m_target)
        m_target->submissionAborted();
}

void LibplaceboDecodedVideoProducer::commitPendingRender() {
    if (!m_pendingState)
        return;
    m_completedState = std::move(m_pendingState);
    m_pendingState.reset();
}

void LibplaceboDecodedVideoProducer::discardPendingRender() {
    m_pendingState.reset();
}

QRhiTexture &
LibplaceboDecodedVideoProducer::textureForComposition() const {
    Q_ASSERT(m_target);
    return m_target->textureForComposition();
}

std::uint64_t
LibplaceboDecodedVideoProducer::
compositionTextureRevision() const {
    return m_target
        ? m_target->compositionTextureRevision()
        : 0;
}

RenderedVideoProducerDiagnostics
LibplaceboDecodedVideoProducer::diagnostics() const {
    RenderedVideoProducerDiagnostics result;
    result.producerName =
        QStringLiteral("FFmpeg decoded frame via libplacebo");

    if (m_importer
            && m_importer->lastDiagnostics().isValid()) {
        const VideoFrameImportDiagnostics &input =
            m_importer->lastDiagnostics();
        switch (input.path) {
        case VideoFrameImportPath::SoftwareUpload:
            result.inputPath = QStringLiteral(
                "FFmpeg %1 software planes → libplacebo upload")
                .arg(input.softwareFormat);
            break;
        case VideoFrameImportPath::DirectHardwareSurface:
            result.inputPath = input.nativeResource.isEmpty()
                ? QStringLiteral(
                    "FFmpeg %1 surface → direct libplacebo import")
                    .arg(input.hardwareFormat)
                : QStringLiteral(
                    "FFmpeg %1 surface → direct libplacebo import · %2")
                    .arg(
                        input.hardwareFormat,
                        input.nativeResource);
            break;
        case VideoFrameImportPath::SameDeviceGpuCopy:
            result.inputPath = QStringLiteral(
                "FFmpeg %1 surface → same-device GPU copy")
                .arg(input.hardwareFormat);
            break;
        case VideoFrameImportPath::CpuRoundTrip:
            result.inputPath = QStringLiteral(
                "FFmpeg %1 surface → CPU round trip")
                .arg(input.hardwareFormat);
            break;
        case VideoFrameImportPath::Unavailable:
            result.inputPath = QStringLiteral(
                "FFmpeg frame import unavailable");
            break;
        }
        if (!input.sourceDescription.isEmpty()) {
            result.inputPath += QStringLiteral(" · %1")
                .arg(input.sourceDescription);
        }
        if (!input.metadataPath.isEmpty()) {
            result.inputPath += QStringLiteral(" · %1")
                .arg(input.metadataPath);
        }
        result.knownInputCpuTransfersPerInputFrame =
            input.knownCpuDownloadsPerFrame
            + input.knownCpuUploadsPerFrame;
        result.knownInputGpuCopiesPerInputFrame =
            input.knownGpuCopiesPerFrame;
    } else {
        result.inputPath =
            QStringLiteral("Awaiting decoded frame import");
    }

    if (m_target) {
        result.target = m_target->diagnostics();
    } else {
        result.target.outputPath =
            VideoOutputPath::Unavailable;
        result.target.synchronizationMode =
            QStringLiteral("Not active");
        result.target.fallbackReason =
            QStringLiteral(
                "Libplacebo target is unavailable");
    }
    if (!m_failureReason.isEmpty()) {
        result.target = {};
        result.target.outputPath =
            VideoOutputPath::Unavailable;
        result.target.synchronizationMode =
            QStringLiteral("Not active");
        result.target.fallbackReason = m_failureReason;
    }
    result.failureKind = m_failureKind;
    Q_ASSERT(result.isValid());
    return result;
}

const VideoFrameImportDiagnostics &
LibplaceboDecodedVideoProducer::
frameImportDiagnostics() const {
    Q_ASSERT(m_importer);
    return m_importer->lastDiagnostics();
}

std::uint64_t
LibplaceboDecodedVideoProducer::inputImportCount() const {
    return m_importer
        ? m_importer->successfulImportCount()
        : 0;
}

bool LibplaceboDecodedVideoProducer::deviceLost() const {
    return m_rhi.isDeviceLost()
        || (m_gpu && pl_gpu_is_failed(m_gpu));
}

VideoOperationResult
LibplaceboDecodedVideoProducer::unavailable(
        const QString &reason,
        VideoFailureKind failureKind) {
    Q_ASSERT(failureKind != VideoFailureKind::None);
    m_failureReason = reason.isEmpty()
        ? QStringLiteral(
            "Decoded libplacebo video path is unavailable")
        : reason;
    m_failureKind = failureKind;
    qCWarning(sunroomLogVideo).noquote()
        << m_failureReason;
    return VideoOperationResult::Unavailable;
}
