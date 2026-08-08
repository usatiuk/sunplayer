#include "video/DecodedVideoSource.h"

#include <cmath>
#include <utility>

#include <QThread>

#include "media/DecodedVideoFrame.h"
#include "video/LibplaceboDecodedVideoProducer.h"

DecodedVideoSource::DecodedVideoSource(std::shared_ptr<DecodedVideoFrame const> frame, VideoTargetReadback readback,
                                       QObject* parent)
    : RenderedVideoSource(parent), m_frame(std::move(frame)), m_readback(readback) {}

std::shared_ptr<DecodedVideoFrame const> const& DecodedVideoSource::currentFrame() const {
    Q_ASSERT(QThread::currentThread() == thread());
    return m_frame;
}

void DecodedVideoSource::setFrameSelector(DecodedVideoFrameSelector* selector) {
    Q_ASSERT(QThread::currentThread() == thread());
    m_selector = selector;
}

void DecodedVideoSource::requestFrameSelection() {
    Q_ASSERT(QThread::currentThread() == thread());
    emit updateRequested();
}

void DecodedVideoSource::setFrame(std::shared_ptr<DecodedVideoFrame const> frame) {
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(frame);
    if (frame == m_frame) {
        return;
    }
    Q_ASSERT(!m_frame || frame->identity() != m_frame->identity());
    m_frame = std::move(frame);
    advanceContentRevision();
    emit frameChanged();
    emit updateRequested();
}

void DecodedVideoSource::clearFrame() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_frame) {
        return;
    }
    m_frame.reset();
    advanceContentRevision();
    // A producer may retain a mapped AVFrame while the viewport is hidden.
    // Recreate it at the next render boundary so clearing a session releases
    // that retained media promptly.
    advanceProducerConfigurationRevision();
    emit frameChanged();
    emit updateRequested();
}

void DecodedVideoSource::advanceContentRevision() {
    ++m_contentRevision;
    if (m_contentRevision == 0) {
        ++m_contentRevision;
    }
}

void DecodedVideoSource::advanceProducerConfigurationRevision() {
    ++m_producerConfigurationRevision;
    if (m_producerConfigurationRevision == 0) {
        ++m_producerConfigurationRevision;
    }
}

void DecodedVideoSource::prepareForPresentation(std::chrono::steady_clock::time_point now) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_selector) {
        return;
    }
    std::shared_ptr<DecodedVideoFrame const> selected = m_selector->selectFrameForPresentation(now);
    if (selected && selected != m_frame) {
        setFrame(std::move(selected));
    }
}

std::uint64_t DecodedVideoSource::contentRevision() const { return m_contentRevision; }

std::uint64_t DecodedVideoSource::producerConfigurationRevision() const { return m_producerConfigurationRevision; }

std::optional<double> DecodedVideoSource::displayAspectRatio() const {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_frame) {
        return std::nullopt;
    }

    VideoFrameGeometry const& geometry = m_frame->geometry();
    double width = static_cast<double>(geometry.visibleSize.width());
    double const height = static_cast<double>(geometry.visibleSize.height());
    if (geometry.sampleAspectRatioKnown) {
        width *= static_cast<double>(geometry.sampleAspectRatio.numerator) /
                 static_cast<double>(geometry.sampleAspectRatio.denominator);
    }
    double aspectRatio = width / height;

    // libplacebo consumes the mapped frame's discrete display rotation. Match
    // its resulting display geometry for quarter-turn orientations.
    double rotation = std::fmod(std::abs(geometry.rotationDegrees), 180.0);
    if (rotation > 45.0 && rotation < 135.0) {
        aspectRatio = 1.0 / aspectRatio;
    }

    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0) {
        return std::nullopt;
    }
    return aspectRatio;
}

bool DecodedVideoSource::wantsContinuousFrames() const {
    return m_selector && m_selector->wantsContinuousVideoFrames();
}

std::unique_ptr<RenderedVideoProducer> DecodedVideoSource::createProducer(GraphicsDeviceDomain& graphicsDevice) const {
    return std::make_unique<LibplaceboDecodedVideoProducer>(graphicsDevice, *this, m_readback);
}

bool DecodedVideoSource::reportPresentationFailure(VideoFailure const& failure) {
    Q_ASSERT(failure.isValid());
    emit presentationFailed(failure);
    return true;
}
