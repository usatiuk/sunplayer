#include "presentation/PresentationOutputState.h"

#include <utility>

#include <QScreen>
#include <QWindow>

#include "platform/DisplayStateProvider.h"

PresentationOutputState::PresentationOutputState(QObject *parent)
    : PresentationOutputState(
          createDisplayStateProvider(), parent) {}

PresentationOutputState::PresentationOutputState(
        std::unique_ptr<DisplayStateProvider> provider,
        QObject *parent)
    : QObject(parent),
      m_provider(std::move(provider)) {
    Q_ASSERT(m_provider);
    qRegisterMetaType<DisplayState>();
    connect(m_provider.get(), &DisplayStateProvider::stateChanged,
            this, &PresentationOutputState::applyDisplayState);
}

PresentationOutputState::~PresentationOutputState() = default;

void PresentationOutputState::attach(QWindow &window) {
    Q_ASSERT(!m_window);
    m_window = &window;
    connect(m_window, &QWindow::screenChanged, this, [this](QScreen *screen) {
        attachScreen(screen);
        m_provider->refresh();
        emit outputCharacteristicsChanged();
    });
    attachScreen(m_window->screen());
    m_provider->attach(*m_window);
}

QString PresentationOutputState::screenName() const { return m_screenName; }
QString PresentationOutputState::graphicsApi() const { return m_backendState.graphicsApi; }
QString PresentationOutputState::graphicsAdapter() const {
    return m_backendState.graphicsAdapter;
}
QString PresentationOutputState::swapChainFormat() const { return m_backendState.swapChainFormat; }
QString PresentationOutputState::videoSurfaceFormat() const {
    return m_backendState.videoSurfaceFormat;
}
QString PresentationOutputState::videoSurfaceProducer() const {
    return m_backendState.videoSurfaceProducer;
}
QString PresentationOutputState::videoInputPath() const {
    return m_backendState.videoInputPath;
}
QString PresentationOutputState::videoColorPolicy() const {
    return m_backendState.videoColorPolicy;
}
QString PresentationOutputState::videoOutputPath() const {
    return m_backendState.videoOutputPath;
}
QString PresentationOutputState::videoSynchronization() const {
    return m_backendState.videoSynchronization;
}
QString PresentationOutputState::videoCopySummary() const {
    return m_backendState.videoCopySummary;
}
QString PresentationOutputState::videoFallbackReason() const {
    return m_backendState.videoFallbackReason;
}
qreal PresentationOutputState::devicePixelRatio() const { return m_dpr; }
qreal PresentationOutputState::refreshRate() const { return m_refreshRate; }
bool PresentationOutputState::displayHdrEnabled() const {
    return m_state.valid && m_state.hdrActive;
}
bool PresentationOutputState::hdrPresentationActive() const {
    return presentationTarget().hdrPresentationActive;
}
bool PresentationOutputState::sceneReferred() const {
    return presentationTarget().sceneReferred;
}
bool PresentationOutputState::sdrWhiteKnown() const {
    return presentationTarget().sdrWhiteKnown;
}
bool PresentationOutputState::luminanceKnown() const {
    return presentationTarget().luminanceKnown;
}
float PresentationOutputState::sdrWhiteNits() const {
    return presentationTarget().sdrWhiteNits;
}
float PresentationOutputState::minLuminanceNits() const {
    return presentationTarget().minLuminanceNits;
}
float PresentationOutputState::maxLuminanceNits() const {
    return presentationTarget().maxLuminanceNits;
}
float PresentationOutputState::currentHeadroom() const {
    return presentationTarget().currentHeadroom;
}
float PresentationOutputState::potentialHeadroom() const {
    return presentationTarget().potentialHeadroom;
}
float PresentationOutputState::effectiveTargetHeadroom() const {
    return presentationTarget().effectiveTargetHeadroom;
}
float PresentationOutputState::sdrScale() const {
    return presentationTarget().sdrScale;
}

PresentationTarget PresentationOutputState::presentationTarget() const {
    return calculatePresentationTarget(m_state, m_backendState);
}

void PresentationOutputState::reprobePresentation() {
    Q_ASSERT(m_window);
    updateMetrics();
    m_provider->refresh();
    emit outputCharacteristicsChanged();
}

void PresentationOutputState::setBackendState(const PresentationBackendState &state) {
    if (m_backendState == state)
        return;
    m_backendState = state;
    emit stateChanged();
}

void PresentationOutputState::attachScreen(QScreen *screen) {
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_screen = screen;

    if (m_screen) {
        const auto changed = [this] { updateMetrics(); };
        m_connections.append(connect(m_screen, &QScreen::geometryChanged, this, changed));
        m_connections.append(connect(m_screen, &QScreen::refreshRateChanged, this, changed));
    }
    updateMetrics();
}

void PresentationOutputState::updateMetrics() {
    Q_ASSERT(m_window);
    const QString name = m_screen ? m_screen->name() : QStringLiteral("Unavailable");
    const qreal dpr = m_window->devicePixelRatio();
    const qreal refresh = m_screen ? m_screen->refreshRate() : 0.0;
    if (name == m_screenName
        && qFuzzyCompare(dpr, m_dpr)
        && qFuzzyCompare(refresh, m_refreshRate)) {
        return;
    }
    m_screenName = name;
    m_dpr = dpr;
    m_refreshRate = refresh;
    emit stateChanged();
}

void PresentationOutputState::applyDisplayState(const DisplayState &state) {
    Q_ASSERT(m_window);
    if (m_state == state)
        return;
    const bool presentationModeChanged =
        m_state.valid != state.valid
        || m_state.hdrActive != state.hdrActive;
    m_state = state;
    if (presentationModeChanged)
        emit outputCharacteristicsChanged();
    emit stateChanged();
}
