#pragma once

#include <chrono>
#include <memory>

#include "video/RenderedVideoSource.h"
#include "video/VideoTargetInterop.h"

class DecodedVideoFrame;

// Playback-owned policy queried synchronously on the presentation thread.
// It selects immutable decoded frames without exposing queues or clocks to
// the renderer.
class DecodedVideoFrameSelector {
public:
    virtual ~DecodedVideoFrameSelector() = default;
    virtual std::shared_ptr<const DecodedVideoFrame>
        selectFrameForPresentation(
            std::chrono::steady_clock::time_point now) = 0;
    virtual bool wantsContinuousVideoFrames() const = 0;
};

// Presentation-thread source for frames selected by playback policy. Decoder
// workers retain AVFrames independently, while immutable DecodedVideoFrames
// cross into this source through the application and presentation thread.
class DecodedVideoSource final : public RenderedVideoSource {
    Q_OBJECT

public:
    explicit DecodedVideoSource(
        std::shared_ptr<const DecodedVideoFrame> frame,
        VideoTargetReadback readback,
        QObject *parent = nullptr);

    const std::shared_ptr<const DecodedVideoFrame> &
        currentFrame() const;
    void setFrameSelector(
        DecodedVideoFrameSelector *selector);
    void requestFrameSelection();
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
    void frameChanged();
    void presentationFailed(const VideoFailure &failure);

private:
    void advanceContentRevision();
    void advanceProducerConfigurationRevision();

    std::shared_ptr<const DecodedVideoFrame> m_frame;
    DecodedVideoFrameSelector *m_selector = nullptr;
    VideoTargetReadback m_readback;
    std::uint64_t m_contentRevision = 1;
    std::uint64_t m_producerConfigurationRevision = 1;
};
