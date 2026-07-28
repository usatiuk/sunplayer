#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include <QObject>

class GraphicsDeviceDomain;
class RenderedVideoProducer;

// Device-independent source state and producer factory. The presentation
// engine observes only revisions, cadence, and device recreation through this
// contract; source-specific controls remain in the implementation.
class RenderedVideoSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~RenderedVideoSource() override = default;

    virtual void prepareForPresentation(
        std::chrono::steady_clock::time_point now) = 0;
    virtual std::uint64_t contentRevision() const = 0;
    virtual bool wantsContinuousFrames() const = 0;
    virtual std::unique_ptr<RenderedVideoProducer> createProducer(
        GraphicsDeviceDomain &graphicsDevice) const = 0;

signals:
    void updateRequested();
};
