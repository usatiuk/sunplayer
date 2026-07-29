#include "video/DecodedVideoSource.h"

#include <utility>

#include <QThread>

#include "media/DecodedVideoFrame.h"
#include "video/LibplaceboDecodedVideoProducer.h"

DecodedVideoSource::DecodedVideoSource(
        std::shared_ptr<const DecodedVideoFrame> frame,
        VideoTargetReadback readback,
        QObject *parent)
    : RenderedVideoSource(parent),
      m_frame(std::move(frame)),
      m_readback(readback) {
    Q_ASSERT(m_frame);
}

const std::shared_ptr<const DecodedVideoFrame> &
DecodedVideoSource::currentFrame() const {
    Q_ASSERT(QThread::currentThread() == thread());
    return m_frame;
}

void DecodedVideoSource::setFrame(
        std::shared_ptr<const DecodedVideoFrame> frame) {
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(frame);
    if (frame == m_frame)
        return;
    Q_ASSERT(
        !m_frame
        || frame->identity() != m_frame->identity());
    m_frame = std::move(frame);
    ++m_contentRevision;
    if (m_contentRevision == 0)
        ++m_contentRevision;
    emit updateRequested();
}

void DecodedVideoSource::prepareForPresentation(
        std::chrono::steady_clock::time_point) {}

std::uint64_t DecodedVideoSource::contentRevision() const {
    return m_contentRevision;
}

bool DecodedVideoSource::wantsContinuousFrames() const {
    return false;
}

std::unique_ptr<RenderedVideoProducer>
DecodedVideoSource::createProducer(
        GraphicsDeviceDomain &graphicsDevice) const {
    return std::make_unique<
        LibplaceboDecodedVideoProducer>(
            graphicsDevice, *this, m_readback);
}
