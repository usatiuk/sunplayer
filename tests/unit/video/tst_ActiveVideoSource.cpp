#include <chrono>
#include <memory>
#include <optional>

#include <QtTest>

#include "video/ActiveVideoSource.h"
#include "video/RenderedVideoProducer.h"

class TestVideoSource final : public RenderedVideoSource {
public:
    explicit TestVideoSource(double aspectRatio)
        : m_aspectRatio(aspectRatio) {}

    void prepareForPresentation(
            std::chrono::steady_clock::time_point) override {
        if (m_advanceOnPrepare)
            advance(m_contentRevision);
        if (m_aspectRatioOnPrepare) {
            m_aspectRatio = *m_aspectRatioOnPrepare;
            m_aspectRatioOnPrepare.reset();
            advance(m_producerRevision);
        }
    }

    std::uint64_t contentRevision() const override {
        return m_contentRevision;
    }

    std::uint64_t producerConfigurationRevision() const override {
        return m_producerRevision;
    }

    std::optional<double> displayAspectRatio() const override {
        return m_aspectRatio;
    }

    bool wantsContinuousFrames() const override {
        return m_continuous;
    }

    std::unique_ptr<RenderedVideoProducer> createProducer(
            GraphicsDeviceDomain &) const override {
        return {};
    }

    bool reportPresentationFailure(
            const VideoFailure &failure) override {
        m_lastFailure = failure.reason;
        return m_handlesFailures;
    }

    void publishContent() {
        advance(m_contentRevision);
        emit updateRequested();
    }

    void changeProducer() {
        advance(m_producerRevision);
        emit updateRequested();
    }

    void setAdvanceOnPrepare(bool value) {
        m_advanceOnPrepare = value;
    }

    void setAspectRatioOnPrepare(double value) {
        m_aspectRatioOnPrepare = value;
    }

    void setContinuous(bool value) {
        m_continuous = value;
    }

    void setHandlesFailures(bool value) {
        m_handlesFailures = value;
    }

    QString lastFailure() const {
        return m_lastFailure;
    }

private:
    static void advance(std::uint64_t &revision) {
        ++revision;
        if (revision == 0)
            ++revision;
    }

    double m_aspectRatio;
    std::uint64_t m_contentRevision = 1;
    std::uint64_t m_producerRevision = 1;
    bool m_advanceOnPrepare = false;
    bool m_continuous = false;
    bool m_handlesFailures = false;
    std::optional<double> m_aspectRatioOnPrepare;
    QString m_lastFailure;
};

class ActiveVideoSourceTest final : public QObject {
    Q_OBJECT

private slots:
    void forwardsOnlyActiveUpdates();
    void switchesDelegatesAtOneProducerRevision();
    void observesPrepareAndFailurePolicy();
};

void ActiveVideoSourceTest::forwardsOnlyActiveUpdates() {
    TestVideoSource player(16.0 / 9.0);
    TestVideoSource diagnostics(4.0 / 3.0);
    ActiveVideoSource source(player, diagnostics);
    QSignalSpy updates(
        &source, &RenderedVideoSource::updateRequested);

    const std::uint64_t initialContent =
        source.contentRevision();
    diagnostics.publishContent();
    QCOMPARE(source.contentRevision(), initialContent);
    QCOMPARE(updates.count(), 0);

    player.publishContent();
    QCOMPARE(source.contentRevision(), initialContent + 1);
    QCOMPARE(updates.count(), 1);

    const std::uint64_t initialProducer =
        source.producerConfigurationRevision();
    player.changeProducer();
    QCOMPARE(
        source.producerConfigurationRevision(),
        initialProducer + 1);
    QCOMPARE(updates.count(), 2);
}

void ActiveVideoSourceTest::
switchesDelegatesAtOneProducerRevision() {
    TestVideoSource player(16.0 / 9.0);
    TestVideoSource diagnostics(4.0 / 3.0);
    diagnostics.setContinuous(true);
    ActiveVideoSource source(player, diagnostics);

    const std::uint64_t content =
        source.contentRevision();
    const std::uint64_t producer =
        source.producerConfigurationRevision();
    source.setRoute(
        ActiveVideoSource::Route::Diagnostics);

    QCOMPARE(
        source.route(),
        ActiveVideoSource::Route::Diagnostics);
    QCOMPARE(source.contentRevision(), content + 1);
    QCOMPARE(
        source.producerConfigurationRevision(),
        producer + 1);
    QVERIFY(source.displayAspectRatio());
    QCOMPARE(*source.displayAspectRatio(), 4.0 / 3.0);
    QVERIFY(source.wantsContinuousFrames());
}

void ActiveVideoSourceTest::
observesPrepareAndFailurePolicy() {
    TestVideoSource player(16.0 / 9.0);
    TestVideoSource diagnostics(4.0 / 3.0);
    player.setAdvanceOnPrepare(true);
    player.setAspectRatioOnPrepare(9.0 / 16.0);
    player.setHandlesFailures(true);
    ActiveVideoSource source(player, diagnostics);

    const std::uint64_t revision =
        source.contentRevision();
    const std::uint64_t producerRevision =
        source.producerConfigurationRevision();
    source.prepareForPresentation(
        std::chrono::steady_clock::now());
    QCOMPARE(source.contentRevision(), revision + 1);
    QCOMPARE(
        source.producerConfigurationRevision(),
        producerRevision + 1);
    QVERIFY(source.displayAspectRatio());
    QCOMPARE(*source.displayAspectRatio(), 9.0 / 16.0);

    QVERIFY(source.reportPresentationFailure(
        {
            .kind = VideoFailureKind::General,
            .reason = QStringLiteral("cannot import frame"),
        }));
    QCOMPARE(
        player.lastFailure(),
        QStringLiteral("cannot import frame"));
    QVERIFY(diagnostics.lastFailure().isEmpty());
}

QTEST_APPLESS_MAIN(ActiveVideoSourceTest)
#include "tst_ActiveVideoSource.moc"
