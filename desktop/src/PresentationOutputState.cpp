#include "PresentationOutputState.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include "DisplayStateProvider.h"

namespace {
constexpr float scRgbReferenceWhiteNits = 80.0f;
}

PresentationOutputState::PresentationOutputState(QObject *parent)
    : QObject(parent) {
    m_provider = createDisplayStateProvider();
    Q_ASSERT(m_provider);
    connect(m_provider.get(), &DisplayStateProvider::stateChanged,
            this, &PresentationOutputState::applyDisplayState);
}

PresentationOutputState::~PresentationOutputState() = default;

void PresentationOutputState::attach(QWindow &window) {
    Q_ASSERT(!m_window);
    m_window = &window;
    connect(m_window, &QWindow::screenChanged, this, [this](QScreen *screen) {
        attachScreen(screen);
        m_provider->attach(*m_window);
    });
    attachScreen(m_window->screen());
    m_provider->attach(*m_window);
}

QString PresentationOutputState::screenName() const { return m_screenName; }
QString PresentationOutputState::graphicsApi() const { return m_backendState.graphicsApi; }
QString PresentationOutputState::swapChainFormat() const { return m_backendState.swapChainFormat; }
qreal PresentationOutputState::devicePixelRatio() const { return m_dpr; }
qreal PresentationOutputState::refreshRate() const { return m_refreshRate; }
bool PresentationOutputState::displayHdrEnabled() const {
    return m_state.valid && m_state.hdrActive;
}
bool PresentationOutputState::extendedLinearActive() const {
    return m_backendState.extendedLinearActive;
}
bool PresentationOutputState::sceneReferred() const {
    return extendedLinearActive() && m_backendState.sceneReferred;
}
bool PresentationOutputState::sdrWhiteKnown() const {
    return extendedLinearActive()
        && ((m_state.valid && m_state.hdrActive
                && m_state.sdrWhiteNits > 0.0f)
            || m_backendState.sdrWhiteKnown);
}
bool PresentationOutputState::luminanceKnown() const {
    return extendedLinearActive()
        && ((m_state.valid && m_state.hdrActive
                && m_state.maxLuminanceNits > 0.0f)
            || m_backendState.luminanceKnown);
}
float PresentationOutputState::sdrWhiteNits() const {
    if (!extendedLinearActive())
        return 0.0f;
    if (m_state.valid && m_state.hdrActive && m_state.sdrWhiteNits > 0.0f)
        return m_state.sdrWhiteNits;
    return m_backendState.sdrWhiteKnown
        ? m_backendState.sdrWhiteNits : 0.0f;
}
float PresentationOutputState::minLuminanceNits() const {
    if (!extendedLinearActive())
        return 0.0f;
    if (m_state.valid && m_state.hdrActive
            && m_state.maxLuminanceNits > 0.0f) {
        return m_state.minLuminanceNits;
    }
    return m_backendState.luminanceKnown
        ? m_backendState.minLuminanceNits : 0.0f;
}
float PresentationOutputState::maxLuminanceNits() const {
    if (!extendedLinearActive())
        return 0.0f;
    if (m_state.valid && m_state.hdrActive
            && m_state.maxLuminanceNits > 0.0f) {
        return m_state.maxLuminanceNits;
    }
    return m_backendState.luminanceKnown
        ? m_backendState.maxLuminanceNits : 0.0f;
}
float PresentationOutputState::currentHeadroom() const {
    if (!extendedLinearActive())
        return 1.0f;
    if (maxLuminanceNits() > 0.0f)
        return std::max(
            1.0f, maxLuminanceNits() / scRgbReferenceWhiteNits);
    return std::max(1.0f, m_backendState.currentHeadroom);
}
float PresentationOutputState::potentialHeadroom() const {
    return extendedLinearActive()
        ? std::max(currentHeadroom(), m_backendState.potentialHeadroom)
        : 1.0f;
}
float PresentationOutputState::effectiveTargetHeadroom() const {
    const float scale = sdrScale();
    Q_ASSERT(std::isfinite(scale) && scale > 0.0f);
    return std::max(1.0f, currentHeadroom() / scale);
}
float PresentationOutputState::sdrScale() const {
    return sceneReferred()
        ? effectiveSdrWhiteNits() / scRgbReferenceWhiteNits
        : 1.0f;
}

float PresentationOutputState::effectiveSdrWhiteNits() const {
    const float queried = sdrWhiteNits();
    return queried > 0.0f ? queried : scRgbReferenceWhiteNits;
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
    bool screenSelectionChanged = false;
    if (QScreen *screen =
            QGuiApplication::screenAt(m_window->geometry().center());
            screen && screen != m_screen) {
        attachScreen(screen);
        screenSelectionChanged = true;
    }
    const bool displayStateChanged = m_state != state;
    const bool presentationModeChanged = displayStateChanged
        && (m_state.valid != state.valid
            || m_state.hdrActive != state.hdrActive);
    if (!displayStateChanged && !screenSelectionChanged)
        return;
    if (displayStateChanged)
        m_state = state;
    if (presentationModeChanged || screenSelectionChanged)
        emit outputCharacteristicsChanged();
    if (displayStateChanged)
        emit stateChanged();
}
