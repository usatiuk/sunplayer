#include "video/libplacebo/LibplaceboFrameImporter.h"

#include <utility>

extern "C" {
#include <libavutil/frame.h>
}

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

bool VideoFrameImportDiagnostics::isValid() const {
    if (softwareFormat.isEmpty()
            || synchronizationMode.isEmpty()) {
        return false;
    }

    const bool hardware =
        storageKind
        != VideoFrameStorageKind::SoftwarePlanes;
    if (hardware != !hardwareFormat.isEmpty())
        return false;

    switch (path) {
    case VideoFrameImportPath::SoftwareUpload:
        return !hardware
            && knownCpuDownloadsPerFrame == 0
            && knownCpuUploadsPerFrame == 1
            && knownGpuCopiesPerFrame == 0
            && fallbackReason.isEmpty();
    case VideoFrameImportPath::DirectHardwareSurface:
        return hardware
            && knownCpuDownloadsPerFrame == 0
            && knownCpuUploadsPerFrame == 0
            && knownGpuCopiesPerFrame == 0
            && fallbackReason.isEmpty();
    case VideoFrameImportPath::SameDeviceGpuCopy:
        return hardware
            && knownCpuDownloadsPerFrame == 0
            && knownCpuUploadsPerFrame == 0
            && knownGpuCopiesPerFrame > 0
            && !fallbackReason.isEmpty();
    case VideoFrameImportPath::CpuRoundTrip:
        return hardware
            && knownCpuDownloadsPerFrame > 0
            && knownCpuUploadsPerFrame > 0
            && !fallbackReason.isEmpty();
    case VideoFrameImportPath::Unavailable:
        return knownCpuDownloadsPerFrame == 0
            && knownCpuUploadsPerFrame == 0
            && knownGpuCopiesPerFrame == 0
            && !fallbackReason.isEmpty();
    }
    return false;
}

LibplaceboFrameImporter::Mapping::Mapping(
        LibplaceboFrameImporter &owner)
    : m_owner(&owner) {}

LibplaceboFrameImporter::Mapping::~Mapping() {
    if (m_owner)
        m_owner->releaseMapping();
}

LibplaceboFrameImporter::Mapping::Mapping(
        Mapping &&other) noexcept
    : m_owner(std::exchange(other.m_owner, nullptr)) {}

const pl_frame &
LibplaceboFrameImporter::Mapping::frame() const {
    Q_ASSERT(m_owner && m_owner->m_mappingActive);
    return m_owner->m_mappedFrame;
}

const VideoFrameImportDiagnostics &
LibplaceboFrameImporter::Mapping::diagnostics() const {
    Q_ASSERT(m_owner);
    return m_owner->m_lastDiagnostics;
}

LibplaceboFrameImporter::LibplaceboFrameImporter(
        pl_gpu gpu,
        std::uint64_t graphicsDeviceGeneration,
        std::unique_ptr<LibplaceboHardwareFrameImporter>
            hardwareImporter)
    : m_gpu(gpu),
      m_graphicsDeviceGeneration(
          graphicsDeviceGeneration),
      m_hardwareImporter(std::move(hardwareImporter)) {
    Q_ASSERT(m_gpu);
    Q_ASSERT(m_graphicsDeviceGeneration != 0);
}

LibplaceboFrameImporter::~LibplaceboFrameImporter() {
    releaseMapping();
    for (pl_tex &texture : m_softwareTextures)
        pl_tex_destroy(m_gpu, &texture);
}

std::unique_ptr<LibplaceboFrameImporter::Mapping>
LibplaceboFrameImporter::map(
        const DecodedVideoFrame &frame,
        QString *error) {
    Q_ASSERT(!m_mappingActive);
    if (error)
        error->clear();

    if (!frame.storage().isCompatibleWithGraphicsDevice(
            m_graphicsDeviceGeneration)) {
        return unavailable(
            frame,
            QStringLiteral(
                "Decoded frame belongs to a stale graphics-device "
                "generation"),
            error);
    }

    if (frame.storage().kind
            != VideoFrameStorageKind::SoftwarePlanes) {
        QString hardwareError;
        if (m_hardwareImporter
                && m_hardwareImporter->map(
                    frame,
                    m_mappedFrame,
                    m_lastDiagnostics,
                    &hardwareError)) {
            m_mappingActive = true;
            m_hardwareMapping = true;
            ++m_successfulImportCount;
            Q_ASSERT(m_lastDiagnostics.isValid());
            return std::unique_ptr<Mapping>(
                new Mapping(*this));
        }
        return unavailable(
            frame,
            !hardwareError.isEmpty()
                ? hardwareError
                : QStringLiteral(
                    "No native importer is implemented for %1 frames")
                    .arg(frame.storage().hardwareFormat),
            error);
    }

    const AVFrame &avFrame = frame.ffmpegFrame();
    const auto pixelFormat =
        static_cast<enum AVPixelFormat>(avFrame.format);
    if (!pl_test_pixfmt(m_gpu, pixelFormat)) {
        return unavailable(
            frame,
            QStringLiteral(
                "Libplacebo cannot upload FFmpeg pixel format %1")
                .arg(frame.signal().pixelFormat),
            error);
    }

    pl_avframe_params parameters{};
    parameters.frame = &avFrame;
    parameters.tex = m_softwareTextures.data();
    parameters.map_dovi = true;
    if (!pl_map_avframe_ex(
            m_gpu, &m_mappedFrame, &parameters)) {
        return unavailable(
            frame,
            QStringLiteral(
                "Libplacebo could not map decoded software frame"),
            error);
    }

    m_mappingActive = true;
    ++m_successfulImportCount;
    m_lastDiagnostics = {
        .storageKind = frame.storage().kind,
        .path = VideoFrameImportPath::SoftwareUpload,
        .hardwareFormat = {},
        .softwareFormat =
            frame.storage().softwareFormat,
        .nativeResource = {},
        .synchronizationMode =
            QStringLiteral(
                "libplacebo-managed software-plane upload"),
        .knownCpuDownloadsPerFrame = 0,
        .knownCpuUploadsPerFrame = 1,
        .knownGpuCopiesPerFrame = 0,
        .fallbackReason = {},
    };
    Q_ASSERT(m_lastDiagnostics.isValid());
    return std::unique_ptr<Mapping>(
        new Mapping(*this));
}

const VideoFrameImportDiagnostics &
LibplaceboFrameImporter::lastDiagnostics() const {
    return m_lastDiagnostics;
}

std::uint64_t
LibplaceboFrameImporter::successfulImportCount() const {
    return m_successfulImportCount;
}

void LibplaceboFrameImporter::releaseMapping() {
    if (!m_mappingActive)
        return;
    if (m_hardwareMapping) {
        Q_ASSERT(m_hardwareImporter);
        m_hardwareImporter->unmap(m_mappedFrame);
    } else {
        pl_unmap_avframe(m_gpu, &m_mappedFrame);
    }
    m_mappedFrame = {};
    m_mappingActive = false;
    m_hardwareMapping = false;
}

std::unique_ptr<LibplaceboFrameImporter::Mapping>
LibplaceboFrameImporter::unavailable(
        const DecodedVideoFrame &frame,
        const QString &reason,
        QString *error) {
    m_lastDiagnostics = {
        .storageKind = frame.storage().kind,
        .path = VideoFrameImportPath::Unavailable,
        .hardwareFormat =
            frame.storage().isHardware()
            ? frame.storage().hardwareFormat
            : QString(),
        .softwareFormat =
            frame.storage().softwareFormat,
        .nativeResource = {},
        .synchronizationMode =
            QStringLiteral("Not active"),
        .knownCpuDownloadsPerFrame = 0,
        .knownCpuUploadsPerFrame = 0,
        .knownGpuCopiesPerFrame = 0,
        .fallbackReason = reason,
    };
    Q_ASSERT(m_lastDiagnostics.isValid());
    if (error)
        *error = reason;
    return {};
}
