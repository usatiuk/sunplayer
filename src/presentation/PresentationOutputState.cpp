#include "presentation/PresentationOutputState.h"

#include <utility>

#include <QScreen>
#include <QWindow>

#include "platform/DisplayStateProvider.h"

PresentationOutputState::PresentationOutputState(QObject* parent)
    : PresentationOutputState(createDisplayStateProvider(), parent) {}

PresentationOutputState::PresentationOutputState(std::unique_ptr<DisplayStateProvider> provider, QObject* parent)
    : QObject(parent), m_provider(std::move(provider)) {
    Q_ASSERT(m_provider);
    qRegisterMetaType<DisplayState>();
    connect(m_provider.get(), &DisplayStateProvider::stateChanged, this, &PresentationOutputState::applyDisplayState);
}

PresentationOutputState::~PresentationOutputState() = default;

void PresentationOutputState::attach(QWindow& window) {
    Q_ASSERT(!m_window);
    m_window = &window;
    connect(m_window, &QWindow::screenChanged, this, [this](QScreen* screen) {
        attachScreen(screen);
        m_provider->refresh();
        emit outputCharacteristicsChanged();
    });
    attachScreen(m_window->screen());
    m_provider->attach(*m_window);
}

QString PresentationOutputState::screenName() const { return m_screenName; }
QString PresentationOutputState::graphicsApi() const { return m_backendState.graphicsApi; }
QString PresentationOutputState::graphicsAdapter() const { return m_backendState.graphicsAdapter; }
QString PresentationOutputState::swapChainFormat() const { return m_backendState.swapChainFormat; }
QString PresentationOutputState::videoSurfaceFormat() const { return m_backendState.videoSurfaceFormat; }
QString PresentationOutputState::videoSurfaceProducer() const { return m_backendState.videoSurfaceProducer; }
QString PresentationOutputState::videoInputPath() const { return m_backendState.videoInputPath; }
QString PresentationOutputState::videoColorPolicy() const { return m_backendState.videoColorPolicy; }
QString PresentationOutputState::videoOutputPath() const { return m_backendState.videoOutputPath; }
QString PresentationOutputState::videoSynchronization() const { return m_backendState.videoSynchronization; }
QString PresentationOutputState::videoCopySummary() const { return m_backendState.videoCopySummary; }
QString PresentationOutputState::videoFallbackReason() const { return m_backendState.videoFallbackReason; }
qreal PresentationOutputState::devicePixelRatio() const { return m_dpr; }
qreal PresentationOutputState::refreshRate() const { return m_refreshRate; }
QString PresentationOutputState::displayColorMode() const {
    if (!m_state.valid) {
        return tr("Display color state unavailable");
    }
    switch (m_state.colorMode) {
    case DisplayColorMode::StandardDynamicRange:
        return tr("Standard dynamic range");
    case DisplayColorMode::WideColorGamut:
        return tr("Wide color gamut");
    case DisplayColorMode::HighDynamicRange:
        return tr("HDR / extended range");
    }
    Q_UNREACHABLE_RETURN(QString{});
}
QString PresentationOutputState::targetGamut() const {
    PresentationTarget const target = presentationTarget();
    if (!target.targetPrimariesKnown) {
        return tr("BT.709 / sRGB target");
    }
    ColorPrimaries const& primaries = target.targetPrimaries;
    return tr("Native target xy · R %1,%2 · G %3,%4 · B %5,%6 · W %7,%8")
        .arg(primaries.red.x, 0, 'f', 4)
        .arg(primaries.red.y, 0, 'f', 4)
        .arg(primaries.green.x, 0, 'f', 4)
        .arg(primaries.green.y, 0, 'f', 4)
        .arg(primaries.blue.x, 0, 'f', 4)
        .arg(primaries.blue.y, 0, 'f', 4)
        .arg(primaries.white.x, 0, 'f', 4)
        .arg(primaries.white.y, 0, 'f', 4);
}
bool PresentationOutputState::displayHdrEnabled() const {
    return m_state.valid && m_state.colorMode == DisplayColorMode::HighDynamicRange;
}
bool PresentationOutputState::hdrPresentationActive() const { return presentationTarget().hdrPresentationActive; }
bool PresentationOutputState::sceneReferred() const { return presentationTarget().sceneReferred; }
bool PresentationOutputState::sdrWhiteKnown() const { return presentationTarget().sdrWhiteKnown; }
bool PresentationOutputState::luminanceKnown() const { return presentationTarget().luminanceKnown; }
float PresentationOutputState::sdrWhiteNits() const { return presentationTarget().sdrWhiteNits; }
float PresentationOutputState::minLuminanceNits() const { return presentationTarget().minLuminanceNits; }
float PresentationOutputState::maxLuminanceNits() const { return presentationTarget().maxLuminanceNits; }
float PresentationOutputState::currentHeadroom() const { return presentationTarget().currentHeadroom; }
float PresentationOutputState::potentialHeadroom() const { return presentationTarget().potentialHeadroom; }
float PresentationOutputState::effectiveTargetHeadroom() const { return presentationTarget().effectiveTargetHeadroom; }
float PresentationOutputState::sdrScale() const { return presentationTarget().sdrScale; }

PresentationTarget PresentationOutputState::presentationTarget() const {
    return calculatePresentationTarget(m_state, m_backendState);
}

void PresentationOutputState::reprobePresentation() {
    Q_ASSERT(m_window);
    updateMetrics();
    m_provider->refresh();
    emit outputCharacteristicsChanged();
}

void PresentationOutputState::setBackendState(PresentationBackendState const& state) {
    if (m_backendState == state) {
        return;
    }
    m_backendState = state;
    emit stateChanged();
}

void PresentationOutputState::attachScreen(QScreen* screen) {
    for (auto const& connection : std::as_const(m_connections)) {
        disconnect(connection);
    }
    m_connections.clear();
    m_screen = screen;

    if (m_screen) {
        auto const changed = [this] { updateMetrics(); };
        m_connections.append(connect(m_screen, &QScreen::geometryChanged, this, changed));
        m_connections.append(connect(m_screen, &QScreen::refreshRateChanged, this, changed));
    }
    updateMetrics();
}

void PresentationOutputState::updateMetrics() {
    Q_ASSERT(m_window);
    QString const name = m_screen ? m_screen->name() : QStringLiteral("Unavailable");
    qreal const dpr = m_window->devicePixelRatio();
    qreal const refresh = m_screen ? m_screen->refreshRate() : 0.0;
    if (name == m_screenName && qFuzzyCompare(dpr, m_dpr) && qFuzzyCompare(refresh, m_refreshRate)) {
        return;
    }
    m_screenName = name;
    m_dpr = dpr;
    m_refreshRate = refresh;
    emit stateChanged();
}

void PresentationOutputState::applyDisplayState(DisplayState const& state) {
    Q_ASSERT(m_window);
    if (m_state == state) {
        return;
    }
    bool const presentationModeChanged = m_state.valid != state.valid || m_state.colorMode != state.colorMode;
    m_state = state;
    if (presentationModeChanged) {
        emit outputCharacteristicsChanged();
    }
    emit stateChanged();
}
