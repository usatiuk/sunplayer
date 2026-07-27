#include "PresentationOutputState.h"

#include <algorithm>

#include <QMetaObject>
#include <QScreen>
#include <QThread>
#include <QWindow>

namespace {
constexpr float scRgbReferenceWhiteNits = 80.0f;
}

PresentationOutputState::PresentationOutputState(QWindow *window, QObject *parent)
    : QObject(parent), m_window(window) {
    const QPointer<PresentationOutputState> guard(this);
    m_monitor = std::make_unique<WindowsDisplayMonitor>(
        [guard](const WindowsAdvancedColorState &state) {
            if (!guard)
                return;
            QMetaObject::invokeMethod(guard, [guard, state] {
                if (guard)
                    guard->applyWindowsState(state);
            }, Qt::QueuedConnection);
        });

    if (m_window) {
        connect(m_window, &QWindow::screenChanged, this, [this](QScreen *screen) {
            attachScreen(screen);
            if (m_monitor)
                m_monitor->attach(m_window);
        });
        attachScreen(m_window->screen());
        m_monitor->attach(m_window);
    }
}

PresentationOutputState::~PresentationOutputState() = default;

QString PresentationOutputState::screenName() const { return m_screenName; }
QString PresentationOutputState::graphicsApi() const { return QStringLiteral("Direct3D 11"); }
QString PresentationOutputState::swapChainFormat() const {
    return QStringLiteral("scRGB / extended linear sRGB");
}
qreal PresentationOutputState::devicePixelRatio() const { return m_dpr; }
qreal PresentationOutputState::logicalDotsPerInch() const { return m_logicalDpi; }
qreal PresentationOutputState::refreshRate() const { return m_refreshRate; }
bool PresentationOutputState::queryValid() const { return m_state.valid; }
bool PresentationOutputState::hdrActive() const { return m_state.valid && m_state.hdrActive; }
bool PresentationOutputState::scRgbSupported() const { return true; }
bool PresentationOutputState::sceneReferred() const { return hdrActive(); }
bool PresentationOutputState::absoluteLuminanceKnown() const { return m_state.valid; }
bool PresentationOutputState::sdrWhiteKnown() const { return hdrActive(); }
float PresentationOutputState::sdrWhiteNits() const {
    return m_state.sdrWhiteNits > 0.0f ? m_state.sdrWhiteNits : scRgbReferenceWhiteNits;
}
float PresentationOutputState::minLuminanceNits() const {
    return std::max(0.0f, m_state.minLuminanceNits);
}
float PresentationOutputState::maxLuminanceNits() const {
    return std::max(0.0f, m_state.maxLuminanceNits);
}
float PresentationOutputState::currentHeadroom() const {
    return hdrActive() && maxLuminanceNits() > 0.0f
        ? maxLuminanceNits() / scRgbReferenceWhiteNits
        : 1.0f;
}
float PresentationOutputState::potentialHeadroom() const { return currentHeadroom(); }
float PresentationOutputState::referenceWhiteNits() const {
    return hdrActive() ? scRgbReferenceWhiteNits : 0.0f;
}
float PresentationOutputState::displayPeakNits() const {
    return hdrActive() ? maxLuminanceNits() : 0.0f;
}
float PresentationOutputState::sdrScale() const {
    return hdrActive() ? sdrWhiteNits() / scRgbReferenceWhiteNits : 1.0f;
}

void PresentationOutputState::refresh() {
    updateMetrics();
    if (m_monitor)
        m_monitor->refresh();
}

void PresentationOutputState::attachScreen(QScreen *screen) {
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_screen = screen;

    if (m_screen) {
        const auto changed = [this] { updateMetrics(); };
        m_connections.append(connect(m_screen, &QScreen::geometryChanged, this, changed));
        m_connections.append(connect(
            m_screen, &QScreen::logicalDotsPerInchChanged, this, changed));
        m_connections.append(connect(m_screen, &QScreen::refreshRateChanged, this, changed));
    }
    updateMetrics();
}

void PresentationOutputState::updateMetrics() {
    const QString name = m_screen ? m_screen->name() : QStringLiteral("Unavailable");
    const qreal dpr = m_window ? m_window->devicePixelRatio() : 1.0;
    const qreal dpi = m_screen ? m_screen->logicalDotsPerInch() : 96.0;
    const qreal refresh = m_screen ? m_screen->refreshRate() : 0.0;
    if (name == m_screenName
        && qFuzzyCompare(dpr, m_dpr)
        && qFuzzyCompare(dpi, m_logicalDpi)
        && qFuzzyCompare(refresh, m_refreshRate)) {
        return;
    }
    m_screenName = name;
    m_dpr = dpr;
    m_logicalDpi = dpi;
    m_refreshRate = refresh;
    emit stateChanged();
}

void PresentationOutputState::applyWindowsState(const WindowsAdvancedColorState &state) {
    m_state = state;
    emit stateChanged();
}
