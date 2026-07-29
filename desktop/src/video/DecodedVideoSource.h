#pragma once

#include <memory>

#include "video/RenderedVideoSource.h"
#include "video/VideoTargetInterop.h"

class DecodedVideoFrame;

// Presentation-thread source for the frame selected by the future playback
// scheduler. Decoder workers retain AVFrames independently, then publish an
// immutable DecodedVideoFrame to this source through the application thread.
class DecodedVideoSource final : public RenderedVideoSource {
    Q_OBJECT

public:
    explicit DecodedVideoSource(
        std::shared_ptr<const DecodedVideoFrame> frame,
        VideoTargetReadback readback,
        QObject *parent = nullptr);

    const std::shared_ptr<const DecodedVideoFrame> &
        currentFrame() const;
    void setFrame(
        std::shared_ptr<const DecodedVideoFrame> frame);
    void clearFrame();

    void prepareForPresentation(
        std::chrono::steady_clock::time_point now) override;
    std::uint64_t contentRevision() const override;
    std::uint64_t producerConfigurationRevision() const override;
    std::optional<double> displayAspectRatio() const override;
    bool wantsContinuousFrames() const override;
    std::unique_ptr<RenderedVideoProducer> createProducer(
        GraphicsDeviceDomain &graphicsDevice) const override;
    bool reportPresentationFailure(
        const VideoFailure &failure) override;

signals:
    void presentationFailed(const VideoFailure &failure);

private:
    void advanceContentRevision();
    void advanceProducerConfigurationRevision();

    std::shared_ptr<const DecodedVideoFrame> m_frame;
    VideoTargetReadback m_readback;
    std::uint64_t m_contentRevision = 1;
    std::uint64_t m_producerConfigurationRevision = 1;
};
