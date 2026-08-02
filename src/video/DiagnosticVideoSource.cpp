#include "video/DiagnosticVideoSource.h"

#include <cmath>

#include "graphics/GraphicsDeviceDomain.h"
#include "video/DiagnosticVideoProducer.h"
#include "video/LibplaceboDiagnosticVideoProducer.h"

namespace {
constexpr QSize defaultDiagnosticInputFrameSize{640, 360};
}

DiagnosticVideoSource::DiagnosticVideoSource(
        VideoTargetReadback readback, QObject *parent)
    : DiagnosticVideoSource(
        VideoProducerApi::Libplacebo,
        readback,
        defaultDiagnosticInputFrameSize,
        parent) {}

DiagnosticVideoSource::DiagnosticVideoSource(
        VideoProducerApi producerApi,
        VideoTargetReadback readback,
        QObject *parent)
    : DiagnosticVideoSource(
        producerApi,
        readback,
        defaultDiagnosticInputFrameSize,
        parent) {}

DiagnosticVideoSource::DiagnosticVideoSource(
        VideoProducerApi producerApi,
        VideoTargetReadback readback,
        const QSize &inputFrameSize,
        QObject *parent)
    : RenderedVideoSource(parent),
      m_producerApi(producerApi),
      m_readback(readback),
      m_inputFrameSize(inputFrameSize) {
    Q_ASSERT(!m_inputFrameSize.isEmpty());
}

float DiagnosticVideoSource::sourcePeakHeadroom() const {
    return m_sourcePeakHeadroom;
}

bool DiagnosticVideoSource::toneMappingEnabled() const {
    return m_toneMappingEnabled;
}

bool DiagnosticVideoSource::animatePattern() const {
    return m_animatePattern;
}

bool DiagnosticVideoSource::useLibplacebo() const {
    return m_producerApi == VideoProducerApi::Libplacebo;
}

float DiagnosticVideoSource::phase() const {
    return m_phase;
}

QSize DiagnosticVideoSource::inputFrameSize() const {
    return m_inputFrameSize;
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

void DiagnosticVideoSource::setUseLibplacebo(bool value) {
    const VideoProducerApi producerApi = value
        ? VideoProducerApi::Libplacebo
        : VideoProducerApi::Qrhi;
    if (producerApi == m_producerApi)
        return;
    m_producerApi = producerApi;
    ++m_producerConfigurationRevision;
    if (m_producerConfigurationRevision == 0)
        ++m_producerConfigurationRevision;
    emit rendererChanged();
    emit updateRequested();
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

std::uint64_t
DiagnosticVideoSource::producerConfigurationRevision() const {
    return m_producerConfigurationRevision;
}

std::optional<double>
DiagnosticVideoSource::displayAspectRatio() const {
    return static_cast<double>(m_inputFrameSize.width())
        / static_cast<double>(m_inputFrameSize.height());
}

bool DiagnosticVideoSource::wantsContinuousFrames() const {
    return m_animatePattern;
}

std::unique_ptr<RenderedVideoProducer>
DiagnosticVideoSource::createProducer(
        GraphicsDeviceDomain &graphicsDevice) const {
    switch (m_producerApi) {
    case VideoProducerApi::Qrhi:
        return std::make_unique<DiagnosticVideoProducer>(
            graphicsDevice, *this, m_readback);
    case VideoProducerApi::Libplacebo:
        return std::make_unique<
            LibplaceboDiagnosticVideoProducer>(
                graphicsDevice, *this, m_readback);
    }
    return {};
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
