#include "video/DiagnosticVideoSource.h"

#include <cmath>

#include "graphics/GraphicsDeviceDomain.h"
#include "video/DiagnosticVideoProducer.h"

DiagnosticVideoSource::DiagnosticVideoSource(
        VideoTargetReadback readback, QObject *parent)
    : RenderedVideoSource(parent), m_readback(readback) {}

float DiagnosticVideoSource::sourcePeakHeadroom() const {
    return m_sourcePeakHeadroom;
}

bool DiagnosticVideoSource::toneMappingEnabled() const {
    return m_toneMappingEnabled;
}

bool DiagnosticVideoSource::animatePattern() const {
    return m_animatePattern;
}

float DiagnosticVideoSource::phase() const {
    return m_phase;
}

void DiagnosticVideoSource::setSourcePeakHeadroom(float value) {
    Q_ASSERT(std::isfinite(value) && value >= 1.0f);
    if (qFuzzyCompare(value, m_sourcePeakHeadroom))
        return;
    m_sourcePeakHeadroom = value;
    markContentChanged();
}

void DiagnosticVideoSource::setToneMappingEnabled(bool value) {
    if (value == m_toneMappingEnabled)
        return;
    m_toneMappingEnabled = value;
    markContentChanged();
}

void DiagnosticVideoSource::setAnimatePattern(bool value) {
    if (value == m_animatePattern)
        return;
    m_animatePattern = value;
    m_lastAnimationFrame.reset();
    markContentChanged();
}

void DiagnosticVideoSource::prepareForPresentation(
        std::chrono::steady_clock::time_point now) {
    if (!m_animatePattern) {
        m_lastAnimationFrame.reset();
        return;
    }

    if (m_lastAnimationFrame) {
        const float delta =
            std::chrono::duration<float>(
                now - *m_lastAnimationFrame).count()
            / 8.0f;
        m_phase = std::fmod(m_phase + delta, 1.0f);
        advanceRevision();
    }
    m_lastAnimationFrame = now;
}

std::uint64_t DiagnosticVideoSource::contentRevision() const {
    return m_contentRevision;
}

bool DiagnosticVideoSource::wantsContinuousFrames() const {
    return m_animatePattern;
}

std::unique_ptr<RenderedVideoProducer>
DiagnosticVideoSource::createProducer(
        GraphicsDeviceDomain &graphicsDevice) const {
    return std::make_unique<DiagnosticVideoProducer>(
        graphicsDevice, *this, m_readback);
}

void DiagnosticVideoSource::markContentChanged() {
    advanceRevision();
    emit settingsChanged();
    emit updateRequested();
}

void DiagnosticVideoSource::advanceRevision() {
    ++m_contentRevision;
    if (m_contentRevision == 0)
        ++m_contentRevision;
}
