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

enum class VideoFrameImportFailure {
    None,
    NativeHardwareImportUnavailable,
    General,
};

struct VideoFrameImportDiagnostics {
    VideoFrameStorageKind storageKind = VideoFrameStorageKind::SoftwarePlanes;
    VideoFrameImportPath path = VideoFrameImportPath::Unavailable;
    QString hardwareFormat;
    QString softwareFormat;
    QString sourceDescription;
    QString metadataPath;
    QString nativeResource;
    QString synchronizationMode;
    std::uint32_t knownCpuDownloadsPerFrame = 0;
    std::uint32_t knownCpuUploadsPerFrame = 0;
    std::uint32_t knownGpuCopiesPerFrame = 0;
    QString fallbackReason;
    VideoFrameImportFailure failure = VideoFrameImportFailure::None;

    bool isValid() const;
};

QString describeLibplaceboMetadataPath(DecodedVideoFrame const& frame, pl_frame const* mappedFrame);

// Backend implementation for native decoded surfaces. It maps one retained
// hardware AVFrame into libplacebo planes without defining fallback policy.
class LibplaceboHardwareFrameImporter {
  public:
    virtual ~LibplaceboHardwareFrameImporter() = default;

    virtual bool map(DecodedVideoFrame const& frame, pl_frame& mappedFrame, VideoFrameImportDiagnostics& diagnostics,
                     VideoFrameImportFailure& failure, QString* error) = 0;
    virtual void unmap(pl_frame& mappedFrame) = 0;
};

// Maps one retained decoded frame into libplacebo input planes. The importer
// owns reusable software-upload textures; a Mapping owns and releases the
// transient pl_frame state through the matching software or native path.
class LibplaceboFrameImporter final {
  public:
    class Mapping final {
      public:
        ~Mapping();

        Mapping(Mapping const&) = delete;
        Mapping& operator=(Mapping const&) = delete;
        Mapping(Mapping&& other) noexcept;
        Mapping& operator=(Mapping&& other) = delete;

        pl_frame const& frame() const;
        VideoFrameImportDiagnostics const& diagnostics() const;

      private:
        friend class LibplaceboFrameImporter;
        explicit Mapping(LibplaceboFrameImporter& owner);

        LibplaceboFrameImporter* m_owner = nullptr;
    };

    LibplaceboFrameImporter(pl_gpu gpu, std::uint64_t graphicsDeviceGeneration,
                            std::unique_ptr<LibplaceboHardwareFrameImporter> hardwareImporter);
    ~LibplaceboFrameImporter();

    LibplaceboFrameImporter(LibplaceboFrameImporter const&) = delete;
    LibplaceboFrameImporter& operator=(LibplaceboFrameImporter const&) = delete;

    std::unique_ptr<Mapping> map(DecodedVideoFrame const& frame, QString* error = nullptr);
    VideoFrameImportDiagnostics const& lastDiagnostics() const;
    std::uint64_t successfulImportCount() const;

  private:
    void releaseMapping();
    std::unique_ptr<Mapping> unavailable(DecodedVideoFrame const& frame, QString const& reason,
                                         VideoFrameImportFailure failure, QString* error);

    pl_gpu m_gpu = nullptr;
    std::uint64_t m_graphicsDeviceGeneration = 0;
    std::unique_ptr<LibplaceboHardwareFrameImporter> m_hardwareImporter;
    std::array<pl_tex, 4> m_softwareTextures{};
    pl_frame m_mappedFrame{};
    bool m_hardwareMapping = false;
    bool m_mappingActive = false;
    std::uint64_t m_successfulImportCount = 0;
    VideoFrameImportDiagnostics m_lastDiagnostics;
};
