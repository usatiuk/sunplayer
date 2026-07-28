#pragma once

#include <optional>

#include "video/RenderedVideoSource.h"
#include "video/VideoTargetInterop.h"

class DiagnosticVideoSource final : public RenderedVideoSource {
    Q_OBJECT

    Q_PROPERTY(float sourcePeakHeadroom READ sourcePeakHeadroom
               WRITE setSourcePeakHeadroom NOTIFY settingsChanged)
    Q_PROPERTY(bool toneMappingEnabled READ toneMappingEnabled
               WRITE setToneMappingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool animatePattern READ animatePattern
               WRITE setAnimatePattern NOTIFY settingsChanged)

public:
    explicit DiagnosticVideoSource(
        VideoTargetReadback readback = VideoTargetReadback::Disabled,
        QObject *parent = nullptr);

    float sourcePeakHeadroom() const;
    bool toneMappingEnabled() const;
    bool animatePattern() const;
    float phase() const;

    void setSourcePeakHeadroom(float value);
    void setToneMappingEnabled(bool value);
    void setAnimatePattern(bool value);

    void prepareForPresentation(
        std::chrono::steady_clock::time_point now) override;
    std::uint64_t contentRevision() const override;
    bool wantsContinuousFrames() const override;
    std::unique_ptr<RenderedVideoProducer> createProducer(
        GraphicsDeviceDomain &graphicsDevice) const override;

signals:
    void settingsChanged();

private:
    void advanceRevision();
    void markContentChanged();

    VideoTargetReadback m_readback;
    float m_sourcePeakHeadroom = 12.5f;
    float m_phase = 0.0f;
    bool m_toneMappingEnabled = true;
    bool m_animatePattern = true;
    std::uint64_t m_contentRevision = 1;
    std::optional<std::chrono::steady_clock::time_point>
        m_lastAnimationFrame;
};
