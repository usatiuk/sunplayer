#pragma once

#include <optional>

#include <QSize>
#include <QtQml/qqmlregistration.h>

#include "video/RenderedVideoSource.h"
#include "video/VideoTargetInterop.h"

class DiagnosticVideoSource final : public RenderedVideoSource {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("DiagnosticVideoSource is owned by the application")

    Q_PROPERTY(float sourcePeakHeadroom READ sourcePeakHeadroom
               WRITE setSourcePeakHeadroom NOTIFY settingsChanged)
    Q_PROPERTY(bool toneMappingEnabled READ toneMappingEnabled
               WRITE setToneMappingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool animatePattern READ animatePattern
               WRITE setAnimatePattern NOTIFY settingsChanged)
    Q_PROPERTY(bool useLibplacebo READ useLibplacebo
               WRITE setUseLibplacebo NOTIFY rendererChanged)

public:
    explicit DiagnosticVideoSource(
        VideoTargetReadback readback,
        QObject *parent = nullptr);
    DiagnosticVideoSource(
        VideoProducerApi producerApi,
        VideoTargetReadback readback,
        QObject *parent = nullptr);
    DiagnosticVideoSource(
        VideoProducerApi producerApi,
        VideoTargetReadback readback,
        const QSize &inputFrameSize,
        QObject *parent = nullptr);

    float sourcePeakHeadroom() const;
    bool toneMappingEnabled() const;
    bool animatePattern() const;
    bool useLibplacebo() const;
    float phase() const;
    QSize inputFrameSize() const;

    void setSourcePeakHeadroom(float value);
    void setToneMappingEnabled(bool value);
    void setAnimatePattern(bool value);
    void setUseLibplacebo(bool value);

    void prepareForPresentation(
        std::chrono::steady_clock::time_point now) override;
    std::uint64_t contentRevision() const override;
    std::uint64_t producerConfigurationRevision() const override;
    bool wantsContinuousFrames() const override;
    std::unique_ptr<RenderedVideoProducer> createProducer(
        GraphicsDeviceDomain &graphicsDevice) const override;

signals:
    void settingsChanged();
    void rendererChanged();

private:
    void advanceRevision();
    void markContentChanged();

    VideoProducerApi m_producerApi;
    VideoTargetReadback m_readback;
    QSize m_inputFrameSize;
    float m_sourcePeakHeadroom = 12.5f;
    float m_phase = 0.0f;
    bool m_toneMappingEnabled = true;
    bool m_animatePattern = true;
    std::uint64_t m_contentRevision = 1;
    std::uint64_t m_producerConfigurationRevision = 1;
    std::optional<std::chrono::steady_clock::time_point>
        m_lastAnimationFrame;
};
