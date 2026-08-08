#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <QMargins>
#include <QSize>
#include <QString>

struct AVFrame;

struct VideoFrameRational {
    bool operator==(VideoFrameRational const&) const = default;

    int numerator = 0;
    int denominator = 0;

    bool isValid() const;
};

struct VideoFrameIdentity {
    bool operator==(VideoFrameIdentity const&) const = default;

    std::uint64_t playbackGeneration = 0;
    std::uint64_t decoderRevision = 0;
    std::uint64_t frameId = 0;

    bool isValid() const;
};

struct VideoFrameTiming {
    std::optional<std::int64_t> pts;
    std::optional<std::int64_t> duration;
    VideoFrameRational timeBase;

    bool isValid() const;
    std::optional<std::int64_t> ptsMicroseconds() const;
    std::optional<std::int64_t> durationMicroseconds() const;
};

struct VideoFrameGeometry {
    QSize codedSize;
    QMargins crop;
    QSize visibleSize;
    VideoFrameRational sampleAspectRatio{1, 1};
    bool sampleAspectRatioKnown = false;
    double rotationDegrees = 0.0;
    bool displayMatrixPresent = false;

    bool isValid() const;
};

enum class VideoFrameStorageKind {
    SoftwarePlanes,
    D3D11Surface,
    VulkanImage,
    DrmPrime,
    VaapiSurface,
    VideoToolboxSurface,
    OtherHardwareSurface,
};

struct VideoFrameStorageDescription {
    VideoFrameStorageKind kind = VideoFrameStorageKind::SoftwarePlanes;
    QString hardwareFormat;
    QString softwareFormat;
    std::optional<std::uint64_t> graphicsDeviceGeneration;

    bool isHardware() const;
    bool isValid() const;
    bool isCompatibleWithGraphicsDevice(std::uint64_t generation) const;
};

// Small diagnostic snapshot of the decoded signal. The retained AVFrame is
// authoritative for color fields and side data consumed by libplacebo.
struct VideoSignalDescription {
    QString pixelFormat;
    QString colorPrimaries;
    QString transferFunction;
    QString matrixCoefficients;
    QString colorRange;
    QString chromaLocation;
    int componentDepth = 0;
    bool interlaced = false;

    bool isValid() const;
    QString summary() const;
};

// Immutable, reference-counted boundary between decoding, scheduling, and
// frame import. The retained AVFrame owns or references the actual software
// buffers or hardware surface; Sunroom never copies a native pointer out of
// it and pretends to own the underlying pixels.
class DecodedVideoFrame final {
  public:
    static std::shared_ptr<DecodedVideoFrame const> clone(AVFrame const& frame, VideoFrameIdentity const& identity,
                                                          VideoFrameRational const& timeBase,
                                                          std::optional<std::uint64_t> graphicsDeviceGeneration,
                                                          QString* error = nullptr);

    ~DecodedVideoFrame();

    DecodedVideoFrame(DecodedVideoFrame const&) = delete;
    DecodedVideoFrame& operator=(DecodedVideoFrame const&) = delete;

    VideoFrameIdentity const& identity() const;
    VideoFrameTiming const& timing() const;
    VideoFrameGeometry const& geometry() const;
    VideoFrameStorageDescription const& storage() const;
    VideoSignalDescription const& signal() const;
    AVFrame const& ffmpegFrame() const;

  private:
    DecodedVideoFrame(AVFrame* frame, VideoFrameIdentity identity, VideoFrameTiming timing, VideoFrameGeometry geometry,
                      VideoFrameStorageDescription storage, VideoSignalDescription signal);

    AVFrame* m_frame = nullptr;
    VideoFrameIdentity m_identity;
    VideoFrameTiming m_timing;
    VideoFrameGeometry m_geometry;
    VideoFrameStorageDescription m_storage;
    VideoSignalDescription m_signal;
};
