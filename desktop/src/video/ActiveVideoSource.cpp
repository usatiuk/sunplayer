#include "video/ActiveVideoSource.h"

#include <QThread>

#include "video/RenderedVideoProducer.h"

ActiveVideoSource::ActiveVideoSource(
        RenderedVideoSource &playerSource,
        RenderedVideoSource &diagnosticSource,
        QObject *parent)
    : RenderedVideoSource(parent),
      m_playerSource(playerSource),
      m_diagnosticSource(diagnosticSource) {
    const auto observe =
        [this](RenderedVideoSource &source) {
            RenderedVideoSource *const observedSource =
                &source;
            connect(
                &source,
                &RenderedVideoSource::updateRequested,
                this,
                [this, observedSource] {
                    handleDelegateUpdate(*observedSource);
                });
        };
    observe(m_playerSource);
    observe(m_diagnosticSource);

    m_delegateContentRevision =
        activeDelegate().contentRevision();
    m_delegateProducerConfigurationRevision =
        activeDelegate().producerConfigurationRevision();
}

ActiveVideoSource::Route ActiveVideoSource::route() const {
    return m_route;
}

void ActiveVideoSource::setRoute(Route route) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (route == m_route)
        return;
    m_route = route;
    m_delegateContentRevision =
        activeDelegate().contentRevision();
    m_delegateProducerConfigurationRevision =
        activeDelegate().producerConfigurationRevision();
    advanceRevision(m_contentRevision);
    advanceRevision(m_producerConfigurationRevision);
    emit routeChanged();
    emit updateRequested();
}

void ActiveVideoSource::prepareForPresentation(
        std::chrono::steady_clock::time_point now) {
    Q_ASSERT(QThread::currentThread() == thread());
    activeDelegate().prepareForPresentation(now);
    synchronizeDelegateRevisions(false);
}

std::uint64_t ActiveVideoSource::contentRevision() const {
    return m_contentRevision;
}

std::uint64_t
ActiveVideoSource::producerConfigurationRevision() const {
    return m_producerConfigurationRevision;
}

std::optional<double>
ActiveVideoSource::displayAspectRatio() const {
    return activeDelegate().displayAspectRatio();
}

bool ActiveVideoSource::wantsContinuousFrames() const {
    return activeDelegate().wantsContinuousFrames();
}

std::unique_ptr<RenderedVideoProducer>
ActiveVideoSource::createProducer(
        GraphicsDeviceDomain &graphicsDevice) const {
    return activeDelegate().createProducer(graphicsDevice);
}

bool ActiveVideoSource::reportPresentationFailure(
        const VideoFailure &failure) {
    return activeDelegate().reportPresentationFailure(failure);
}

RenderedVideoSource &
ActiveVideoSource::activeDelegate() const {
    return m_route == Route::Player
        ? m_playerSource
        : m_diagnosticSource;
}

void ActiveVideoSource::handleDelegateUpdate(
        RenderedVideoSource &source) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (&source != &activeDelegate())
        return;
    synchronizeDelegateRevisions(true);
}

void ActiveVideoSource::synchronizeDelegateRevisions(
        bool requestUpdate) {
    RenderedVideoSource &source = activeDelegate();
    const std::uint64_t delegateContentRevision =
        source.contentRevision();
    const std::uint64_t delegateProducerRevision =
        source.producerConfigurationRevision();

    if (delegateContentRevision
            != m_delegateContentRevision) {
        m_delegateContentRevision =
            delegateContentRevision;
        advanceRevision(m_contentRevision);
    }
    if (delegateProducerRevision
            != m_delegateProducerConfigurationRevision) {
        m_delegateProducerConfigurationRevision =
            delegateProducerRevision;
        advanceRevision(m_producerConfigurationRevision);
    }
    if (requestUpdate)
        emit updateRequested();
}

void ActiveVideoSource::advanceRevision(
        std::uint64_t &revision) {
    ++revision;
    if (revision == 0)
        ++revision;
}
