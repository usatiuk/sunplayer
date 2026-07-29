#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <QString>
#include <libplacebo/renderer.h>

#include "media/DecodedVideoFrame.h"

enum class VideoFrameImportPath {
    Unavailable,
    SoftwareUpload,
    DirectHardwareSurface,
    SameDeviceGpuCopy,
    CpuRoundTrip,
};

struct VideoFrameImportDiagnostics {
    VideoFrameStorageKind storageKind =
        VideoFrameStorageKind::SoftwarePlanes;
    VideoFrameImportPath path =
        VideoFrameImportPath::Unavailable;
    QString hardwareFormat;
    QString softwareFormat;
    QString nativeResource;
    QString synchronizationMode;
    std::uint32_t knownCpuDownloadsPerFrame = 0;
    std::uint32_t knownCpuUploadsPerFrame = 0;
    std::uint32_t knownGpuCopiesPerFrame = 0;
    QString fallbackReason;

    bool isValid() const;
};

// Backend implementation for native decoded surfaces. It maps one retained
// hardware AVFrame into libplacebo planes without defining fallback policy.
class LibplaceboHardwareFrameImporter {
public:
    virtual ~LibplaceboHardwareFrameImporter() = default;

    virtual bool map(
        const DecodedVideoFrame &frame,
        pl_frame &mappedFrame,
        VideoFrameImportDiagnostics &diagnostics,
        QString *error) = 0;
    virtual void unmap(pl_frame &mappedFrame) = 0;
};

// Maps one retained decoded frame into libplacebo input planes. The importer
// owns reusable software-upload textures; a Mapping owns and releases the
// transient pl_frame state through the matching software or native path.
class LibplaceboFrameImporter final {
public:
    class Mapping final {
    public:
        ~Mapping();

        Mapping(const Mapping &) = delete;
        Mapping &operator=(const Mapping &) = delete;
        Mapping(Mapping &&other) noexcept;
        Mapping &operator=(Mapping &&other) = delete;

        const pl_frame &frame() const;
        const VideoFrameImportDiagnostics &diagnostics() const;

    private:
        friend class LibplaceboFrameImporter;
        explicit Mapping(LibplaceboFrameImporter &owner);

        LibplaceboFrameImporter *m_owner = nullptr;
    };

    LibplaceboFrameImporter(
        pl_gpu gpu,
        std::uint64_t graphicsDeviceGeneration,
        std::unique_ptr<LibplaceboHardwareFrameImporter>
            hardwareImporter);
    ~LibplaceboFrameImporter();

    LibplaceboFrameImporter(
        const LibplaceboFrameImporter &) = delete;
    LibplaceboFrameImporter &operator=(
        const LibplaceboFrameImporter &) = delete;

    std::unique_ptr<Mapping> map(
        const DecodedVideoFrame &frame,
        QString *error = nullptr);
    const VideoFrameImportDiagnostics &
        lastDiagnostics() const;
    std::uint64_t successfulImportCount() const;

private:
    void releaseMapping();
    std::unique_ptr<Mapping> unavailable(
        const DecodedVideoFrame &frame,
        const QString &reason,
        QString *error);

    pl_gpu m_gpu = nullptr;
    std::uint64_t m_graphicsDeviceGeneration = 0;
    std::unique_ptr<LibplaceboHardwareFrameImporter>
        m_hardwareImporter;
    std::array<pl_tex, 4> m_softwareTextures{};
    pl_frame m_mappedFrame{};
    bool m_hardwareMapping = false;
    bool m_mappingActive = false;
    std::uint64_t m_successfulImportCount = 0;
    VideoFrameImportDiagnostics m_lastDiagnostics;
};
