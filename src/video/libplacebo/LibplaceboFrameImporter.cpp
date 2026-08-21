#include "video/libplacebo/LibplaceboFrameImporter.h"

#include <utility>

#include <QStringList>

extern "C" {
#include <libavutil/frame.h>
}

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

bool VideoFrameImportDiagnostics::isValid() const {
    if (softwareFormat.isEmpty() || synchronizationMode.isEmpty()) {
        return false;
    }

    bool const hardware = storageKind != VideoFrameStorageKind::SoftwarePlanes;
    if (hardware != !hardwareFormat.isEmpty()) {
        return false;
    }

    switch (path) {
    case VideoFrameImportPath::SoftwareUpload:
        return !hardware && failure == VideoFrameImportFailure::None && knownCpuDownloadsPerFrame == 0 &&
               knownCpuUploadsPerFrame == 1 && knownGpuCopiesPerFrame == 0 && fallbackReason.isEmpty();
    case VideoFrameImportPath::DirectHardwareSurface:
        return hardware && failure == VideoFrameImportFailure::None && knownCpuDownloadsPerFrame == 0 &&
               knownCpuUploadsPerFrame == 0 && knownGpuCopiesPerFrame == 0 && fallbackReason.isEmpty();
    case VideoFrameImportPath::SameDeviceGpuCopy:
        return hardware && failure == VideoFrameImportFailure::None && knownCpuDownloadsPerFrame == 0 &&
               knownCpuUploadsPerFrame == 0 && knownGpuCopiesPerFrame > 0 && !fallbackReason.isEmpty();
    case VideoFrameImportPath::CpuRoundTrip:
        return hardware && failure == VideoFrameImportFailure::None && knownCpuDownloadsPerFrame > 0 &&
               knownCpuUploadsPerFrame > 0 && !fallbackReason.isEmpty();
    case VideoFrameImportPath::Unavailable:
        return knownCpuDownloadsPerFrame == 0 && knownCpuUploadsPerFrame == 0 && knownGpuCopiesPerFrame == 0 &&
               !fallbackReason.isEmpty() && failure != VideoFrameImportFailure::None;
    }
    return false;
}

QString describeLibplaceboMetadataPath(DecodedVideoFrame const& frame, pl_frame const* mappedFrame) {
    AVFrame const& source = frame.ffmpegFrame();
    QStringList paths;
    if (mappedFrame && mappedFrame->repr.sys == PL_COLOR_SYSTEM_DOLBYVISION && mappedFrame->repr.dovi) {
        paths.push_back(QStringLiteral("Dolby Vision reshape mapped by libplacebo"));
    } else if (av_frame_get_side_data(&source, AV_FRAME_DATA_DOVI_METADATA)) {
        paths.push_back(mappedFrame ? QStringLiteral("Dolby Vision parsed metadata not mapped; "
                                                     "decoded base-layer representation selected")
                                    : QStringLiteral("Dolby Vision parsed metadata present; "
                                                     "mapping outcome unavailable"));
    } else if (av_frame_get_side_data(&source, AV_FRAME_DATA_DOVI_RPU_BUFFER)) {
        paths.push_back(QStringLiteral("raw Dolby Vision RPU present; no parsed mapper input"));
    }

    if (AVFrameSideData const* hdr10Plus = av_frame_get_side_data(&source, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
        hdr10Plus && hdr10Plus->size > 0) {
        if (!mappedFrame) {
            paths.push_back(QStringLiteral("HDR10+ metadata present; mapping outcome unavailable"));
        } else if (pl_hdr_metadata_contains(&mappedFrame->color.hdr, PL_HDR_METADATA_HDR10PLUS)) {
            paths.push_back(QStringLiteral("HDR10+ scene-luminance subset available on mapped frame"));
        } else {
            paths.push_back(QStringLiteral("HDR10+ metadata present but no usable mapped subset"));
        }
    }
    if (AVFrameSideData const* icc = av_frame_get_side_data(&source, AV_FRAME_DATA_ICC_PROFILE)) {
        paths.push_back(QStringLiteral("source ICC profile present (%1 bytes); not applied").arg(icc->size));
    }
    if (paths.isEmpty()) {
        paths.push_back(QStringLiteral("static/base color metadata"));
    }
    return paths.join(QStringLiteral(" · "));
}

LibplaceboFrameImporter::Mapping::Mapping(LibplaceboFrameImporter& owner) : m_owner(&owner) {}

LibplaceboFrameImporter::Mapping::~Mapping() {
    if (m_owner) {
        m_owner->releaseMapping();
    }
}

LibplaceboFrameImporter::Mapping::Mapping(Mapping&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)) {}

pl_frame const& LibplaceboFrameImporter::Mapping::frame() const {
    Q_ASSERT(m_owner && m_owner->m_mappingActive);
    return m_owner->m_mappedFrame;
}

VideoFrameImportDiagnostics const& LibplaceboFrameImporter::Mapping::diagnostics() const {
    Q_ASSERT(m_owner);
    return m_owner->m_lastDiagnostics;
}

LibplaceboFrameImporter::LibplaceboFrameImporter(pl_gpu gpu, std::uint64_t graphicsDeviceGeneration,
                                                 std::unique_ptr<LibplaceboHardwareFrameImporter> hardwareImporter)
    : m_gpu(gpu), m_graphicsDeviceGeneration(graphicsDeviceGeneration),
      m_hardwareImporter(std::move(hardwareImporter)) {
    Q_ASSERT(m_gpu);
    Q_ASSERT(m_graphicsDeviceGeneration != 0);
}

LibplaceboFrameImporter::~LibplaceboFrameImporter() {
    releaseMapping();
    for (pl_tex& texture : m_softwareTextures) {
        pl_tex_destroy(m_gpu, &texture);
    }
}

std::unique_ptr<LibplaceboFrameImporter::Mapping> LibplaceboFrameImporter::map(DecodedVideoFrame const& frame,
                                                                               bool mapDolbyVision, QString* error) {
    Q_ASSERT(!m_mappingActive);
    if (error) {
        error->clear();
    }

    if (!frame.storage().isCompatibleWithGraphicsDevice(m_graphicsDeviceGeneration)) {
        return unavailable(frame,
                           QStringLiteral("Decoded frame belongs to a stale graphics-device "
                                          "generation"),
                           frame.storage().isHardware() ? VideoFrameImportFailure::NativeHardwareImportUnavailable
                                                        : VideoFrameImportFailure::General,
                           error);
    }

    if (frame.storage().kind != VideoFrameStorageKind::SoftwarePlanes) {
        QString hardwareError;
        VideoFrameImportFailure hardwareFailure = VideoFrameImportFailure::NativeHardwareImportUnavailable;
        if (m_hardwareImporter && m_hardwareImporter->map(frame, mapDolbyVision, m_mappedFrame, m_lastDiagnostics,
                                                          hardwareFailure, &hardwareError)) {
            m_mappingActive = true;
            m_hardwareMapping = true;
            ++m_successfulImportCount;
            Q_ASSERT(m_lastDiagnostics.isValid());
            return std::unique_ptr<Mapping>(new Mapping(*this));
        }
        return unavailable(
            frame,
            !hardwareError.isEmpty()
                ? hardwareError
                : QStringLiteral("No native importer is implemented for %1 frames").arg(frame.storage().hardwareFormat),
            hardwareFailure, error);
    }

    AVFrame const& avFrame = frame.ffmpegFrame();
    auto const pixelFormat = static_cast<enum AVPixelFormat>(avFrame.format);
    if (!pl_test_pixfmt(m_gpu, pixelFormat)) {
        return unavailable(
            frame, QStringLiteral("Libplacebo cannot upload FFmpeg pixel format %1").arg(frame.signal().pixelFormat),
            VideoFrameImportFailure::General, error);
    }

    pl_avframe_params parameters{};
    parameters.frame = &avFrame;
    parameters.tex = m_softwareTextures.data();
    parameters.map_dovi = mapDolbyVision;
    if (!pl_map_avframe_ex(m_gpu, &m_mappedFrame, &parameters)) {
        return unavailable(frame, QStringLiteral("Libplacebo could not map decoded software frame"),
                           VideoFrameImportFailure::General, error);
    }

    m_mappingActive = true;
    ++m_successfulImportCount;
    m_lastDiagnostics = {
        .storageKind = frame.storage().kind,
        .path = VideoFrameImportPath::SoftwareUpload,
        .hardwareFormat = {},
        .softwareFormat = frame.storage().softwareFormat,
        .sourceDescription = frame.signal().summary(),
        .metadataPath = describeLibplaceboMetadataPath(frame, &m_mappedFrame),
        .nativeResource = {},
        .synchronizationMode = QStringLiteral("libplacebo-managed software-plane upload"),
        .knownCpuDownloadsPerFrame = 0,
        .knownCpuUploadsPerFrame = 1,
        .knownGpuCopiesPerFrame = 0,
        .fallbackReason = {},
        .failure = VideoFrameImportFailure::None,
    };
    Q_ASSERT(m_lastDiagnostics.isValid());
    return std::unique_ptr<Mapping>(new Mapping(*this));
}

VideoFrameImportDiagnostics const& LibplaceboFrameImporter::lastDiagnostics() const { return m_lastDiagnostics; }

std::uint64_t LibplaceboFrameImporter::successfulImportCount() const { return m_successfulImportCount; }

void LibplaceboFrameImporter::releaseMapping() {
    if (!m_mappingActive) {
        return;
    }
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

std::unique_ptr<LibplaceboFrameImporter::Mapping> LibplaceboFrameImporter::unavailable(DecodedVideoFrame const& frame,
                                                                                       QString const& reason,
                                                                                       VideoFrameImportFailure failure,
                                                                                       QString* error) {
    m_lastDiagnostics = {
        .storageKind = frame.storage().kind,
        .path = VideoFrameImportPath::Unavailable,
        .hardwareFormat = frame.storage().isHardware() ? frame.storage().hardwareFormat : QString(),
        .softwareFormat = frame.storage().softwareFormat,
        .sourceDescription = frame.signal().summary(),
        .metadataPath = describeLibplaceboMetadataPath(frame, nullptr),
        .nativeResource = {},
        .synchronizationMode = QStringLiteral("Not active"),
        .knownCpuDownloadsPerFrame = 0,
        .knownCpuUploadsPerFrame = 0,
        .knownGpuCopiesPerFrame = 0,
        .fallbackReason = reason,
        .failure = failure,
    };
    Q_ASSERT(m_lastDiagnostics.isValid());
    if (error) {
        *error = reason;
    }
    return {};
}
