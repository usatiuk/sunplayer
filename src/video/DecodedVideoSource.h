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
    virtual std::shared_ptr<DecodedVideoFrame const>
    selectFrameForPresentation(std::chrono::steady_clock::time_point now) = 0;
    virtual bool wantsContinuousVideoFrames() const = 0;
};

// Presentation-thread source for frames selected by playback policy. Decoder
// workers retain AVFrames independently, while immutable DecodedVideoFrames
// cross into this source through the application and presentation thread.
class DecodedVideoSource final : public RenderedVideoSource {
    Q_OBJECT

  public:
    static constexpr int minimumSourceHdrReferenceWhiteNits = 100;
    static constexpr int maximumSourceHdrReferenceWhiteNits = 203;
    static constexpr int defaultSourceHdrReferenceWhiteNits = minimumSourceHdrReferenceWhiteNits;

    explicit DecodedVideoSource(std::shared_ptr<DecodedVideoFrame const> frame, VideoTargetReadback readback,
                                QObject* parent = nullptr);

    std::shared_ptr<DecodedVideoFrame const> const& currentFrame() const;
    void setFrameSelector(DecodedVideoFrameSelector* selector);
    void requestFrameSelection();
    void setFrame(std::shared_ptr<DecodedVideoFrame const> frame);
    void clearFrame();
    bool preferHdr10Plus() const;
    void setPreferHdr10Plus(bool enabled);
    bool absolutePqEnabled() const;
    void setAbsolutePqEnabled(bool enabled);
    int sourceHdrReferenceWhiteNits() const;
    void setSourceHdrReferenceWhiteNits(int nits);

    void prepareForPresentation(std::chrono::steady_clock::time_point now) override;
    std::uint64_t contentRevision() const override;
    std::uint64_t producerConfigurationRevision() const override;
    std::optional<double> displayAspectRatio() const override;
    bool wantsContinuousFrames() const override;
    std::unique_ptr<RenderedVideoProducer> createProducer(GraphicsDeviceDomain& graphicsDevice) const override;
    bool reportPresentationFailure(VideoFailure const& failure) override;

  signals:
    void frameChanged();
    void preferHdr10PlusChanged();
    void absolutePqEnabledChanged();
    void sourceHdrReferenceWhiteNitsChanged();
    void presentationFailed(VideoFailure const& failure);

  private:
    void advanceContentRevision();
    void advanceProducerConfigurationRevision();

    std::shared_ptr<DecodedVideoFrame const> m_frame;
    DecodedVideoFrameSelector* m_selector = nullptr;
    VideoTargetReadback m_readback;
    bool m_preferHdr10Plus = false;
    bool m_absolutePqEnabled = false;
    int m_sourceHdrReferenceWhiteNits = defaultSourceHdrReferenceWhiteNits;
    std::uint64_t m_contentRevision = 1;
    std::uint64_t m_producerConfigurationRevision = 1;
};
