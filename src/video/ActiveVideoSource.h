#pragma once

#include <cstdint>

#include <QtQml/qqmlregistration.h>

#include "video/RenderedVideoSource.h"

// Stable presentation-facing source whose active delegate is selected by the
// application shell. It owns revision semantics, not either delegate or any
// graphics resources.
class ActiveVideoSource final : public RenderedVideoSource {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ActiveVideoSource is owned by the application")

    Q_PROPERTY(Route route READ route WRITE setRoute NOTIFY routeChanged)

  public:
    enum class Route {
        Player,
        Diagnostics,
    };
    Q_ENUM(Route)

    ActiveVideoSource(RenderedVideoSource& playerSource, RenderedVideoSource& diagnosticSource,
                      QObject* parent = nullptr);

    Route route() const;
    void setRoute(Route route);

    void prepareForPresentation(std::chrono::steady_clock::time_point now) override;
    std::uint64_t contentRevision() const override;
    std::uint64_t producerConfigurationRevision() const override;
    std::optional<double> displayAspectRatio() const override;
    bool wantsContinuousFrames() const override;
    std::unique_ptr<RenderedVideoProducer> createProducer(GraphicsDeviceDomain& graphicsDevice) const override;
    bool reportPresentationFailure(VideoFailure const& failure) override;

  signals:
    void routeChanged();

  private:
    RenderedVideoSource& activeDelegate() const;
    void handleDelegateUpdate(RenderedVideoSource& source);
    void synchronizeDelegateRevisions(bool requestUpdate);
    static void advanceRevision(std::uint64_t& revision);

    RenderedVideoSource& m_playerSource;
    RenderedVideoSource& m_diagnosticSource;
    Route m_route = Route::Player;
    std::uint64_t m_contentRevision = 1;
    std::uint64_t m_producerConfigurationRevision = 1;
    std::uint64_t m_delegateContentRevision = 0;
    std::uint64_t m_delegateProducerConfigurationRevision = 0;
};
