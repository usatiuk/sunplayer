#include <array>
#include <limits>
#include <utility>

#include <QSet>
#include <QTest>

#include "app/ApplicationError.h"
#include "app/SupportDiagnostics.h"

class SupportTest final : public QObject {
    Q_OBJECT

  private slots:
    void errorDescriptorsAreStableAndActionable();
    void reportExcludesUntrustedIdentifiers();
    void reportIsBoundedAndDeterministic();
    void issueUrlContainsOnlySanitizedSummary();
};

void SupportTest::errorDescriptorsAreStableAndActionable() {
    struct ExpectedError {
        ApplicationError::Code code;
        char const* stableCode;
        ApplicationErrorSubsystem subsystem;
        ApplicationErrorRecoverability recoverability;
        ApplicationErrorActions actions;
    };
    ApplicationErrorActions const retryActions = ApplicationErrorAction::Retry | ApplicationErrorAction::Restart |
                                                 ApplicationErrorAction::ReportBug | ApplicationErrorAction::Quit;
    ApplicationErrorActions const restartActions =
        ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug | ApplicationErrorAction::Quit;
    ApplicationErrorActions const terminalActions = ApplicationErrorAction::ReportBug | ApplicationErrorAction::Quit;
    auto const expectedErrors = std::to_array<ExpectedError>({
        {ApplicationError::Code::PlatformStartupFailed, "platform.startup_failed",
         ApplicationErrorSubsystem::Platform, ApplicationErrorRecoverability::RestartRequired, restartActions},
        {ApplicationError::Code::PresentationInitializationFailed, "presentation.initialization_failed",
         ApplicationErrorSubsystem::Presentation, ApplicationErrorRecoverability::Retryable, retryActions},
        {ApplicationError::Code::UiRenderingUnavailable, "presentation.ui_rendering_unavailable",
         ApplicationErrorSubsystem::Presentation, ApplicationErrorRecoverability::Retryable, retryActions},
        {ApplicationError::Code::CompositorUnavailable, "graphics.compositor_unavailable",
         ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable, retryActions},
        {ApplicationError::Code::DiagnosticVideoUnavailable, "graphics.diagnostic_video_unavailable",
         ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable, retryActions},
        {ApplicationError::Code::SwapChainUnavailable, "graphics.swap_chain_unavailable",
         ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable, retryActions},
        {ApplicationError::Code::GraphicsDeviceRecoveryExhausted, "graphics.device_recovery_exhausted",
         ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::RestartRequired, restartActions},
        {ApplicationError::Code::FrameSubmissionFailed, "graphics.frame_submission_failed",
         ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable, retryActions},
        {ApplicationError::Code::GraphicsCleanupFailed, "graphics.cleanup_failed",
         ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::RestartRequired, restartActions},
        {ApplicationError::Code::RestartFailed, "application.restart_failed",
         ApplicationErrorSubsystem::Application, ApplicationErrorRecoverability::NotRecoverable, terminalActions},
    });
    QSet<QString> stableCodes;
    for (ExpectedError const& expected : expectedErrors) {
        ApplicationError const candidate(expected.code, QStringLiteral("User message"));
        QVERIFY(candidate.isValid());
        QCOMPARE(candidate.stableCode(), QString::fromLatin1(expected.stableCode));
        QCOMPARE(candidate.subsystem(), expected.subsystem);
        QCOMPARE(candidate.recoverability(), expected.recoverability);
        QCOMPARE(candidate.suggestedActions(), expected.actions);
        QVERIFY(!stableCodes.contains(candidate.stableCode()));
        stableCodes.insert(candidate.stableCode());
    }

    ApplicationError const error(ApplicationError::Code::PresentationInitializationFailed,
                                 QStringLiteral("Could not initialize presentation"), QStringLiteral("driver detail"));
    QVERIFY(error.isValid());
    QCOMPARE(error.stableCode(), QStringLiteral("presentation.initialization_failed"));
    QCOMPARE(error.subsystem(), ApplicationErrorSubsystem::Presentation);
    QCOMPARE(error.recoverability(), ApplicationErrorRecoverability::Retryable);
    QVERIFY(error.suggestedActions().testFlag(ApplicationErrorAction::Retry));
    QVERIFY(error.suggestedActions().testFlag(ApplicationErrorAction::Restart));
    QVERIFY(error.suggestedActions().testFlag(ApplicationErrorAction::ReportBug));
    QVERIFY(error.suggestedActions().testFlag(ApplicationErrorAction::Quit));
    QCOMPARE(error.userMessage(), QStringLiteral("Could not initialize presentation"));
    QCOMPARE(error.technicalDetail(), QStringLiteral("driver detail"));
}

void SupportTest::reportExcludesUntrustedIdentifiers() {
    SupportSnapshot snapshot{
        .version = QStringLiteral("0.1.0"),
        .buildId = QStringLiteral("abcdef123456"),
        .qtVersion = QStringLiteral("6.11.1"),
        .operatingSystem = QStringLiteral("Windows 11"),
        .cpuArchitecture = QStringLiteral("x86 64"),
        .graphicsApi = QStringLiteral("D3D11"),
        .swapChainFormat = QStringLiteral("scRGB / extended linear sRGB"),
        .videoSurfaceProducer = QStringLiteral("C:\\Users\\alice\\private.dll"),
        .videoColorPolicy = QStringLiteral("https://evil.example/movie?id=secret"),
        .displayColorMode = QStringLiteral("HDR"),
        .currentMedia =
            SupportMediaSnapshot{
                .state = SupportMediaState::Error,
                .decoder = QStringLiteral("decoder\nC:\\Users\\alice"),
                .audioBackend = QStringLiteral("host:alice"),
            },
        .applicationErrorCode = QStringLiteral("graphics.frame_submission_failed"),
        .applicationErrorSubsystem = QStringLiteral("graphics"),
    };
    QString const report = SupportDiagnostics::detailedReport(snapshot);
    QVERIFY(!report.contains(QStringLiteral("alice"), Qt::CaseInsensitive));
    QVERIFY(!report.contains(QStringLiteral("evil.example")));
    QVERIFY(!report.contains(QStringLiteral("C:\\")));
    QVERIFY(report.contains(QStringLiteral("unavailable")));
    QVERIFY(report.contains(QStringLiteral("Swap-chain format: scRGB / extended linear sRGB")));
    QVERIFY(report.contains(QStringLiteral("graphics.frame_submission_failed")));
}

void SupportTest::reportIsBoundedAndDeterministic() {
    SupportSnapshot snapshot;
    snapshot.version = QString(20'000, u'A');
    snapshot.buildId = QStringLiteral("build");
    snapshot.qtVersion = QStringLiteral("6.11.1");
    snapshot.operatingSystem = QString::fromUtf8("Příliš žluťoučký kůň 🐴");
    snapshot.cpuArchitecture = QStringLiteral("x86 64");
    snapshot.videoColorPolicy = QString::fromUtf8("PQ · metadata-first / spline");
    snapshot.sdrWhiteKnown = true;
    snapshot.sdrWhiteNits = std::numeric_limits<float>::infinity();
    QString const first = SupportDiagnostics::detailedReport(snapshot);
    QString const second = SupportDiagnostics::detailedReport(snapshot);
    QCOMPARE(first, second);
    QVERIFY(first.size() <= 8 * 1024);
    QVERIFY(first.contains(QStringLiteral("Version: AAAAA")));
    QVERIFY(first.contains(u'…'));
    QVERIFY(first.contains(QString::fromUtf8("Operating system: Příliš žluťoučký kůň 🐴")));
    QVERIFY(first.contains(QString::fromUtf8("Video color policy: PQ · metadata-first / spline")));
    QVERIFY(first.contains(QStringLiteral("SDR white nits: unavailable")));
}

void SupportTest::issueUrlContainsOnlySanitizedSummary() {
    SupportSnapshot snapshot{
        .version = QStringLiteral("0.1.0"),
        .buildId = QStringLiteral("build-id"),
        .operatingSystem = QStringLiteral("Windows 11"),
        .graphicsApi = QStringLiteral("D3D11"),
        .applicationErrorCode = QStringLiteral("graphics.swap_chain_unavailable"),
    };
    QUrl const url =
        SupportDiagnostics::issueUrl(QUrl(QStringLiteral("https://github.com/example/project/issues/new")), snapshot);
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(url.host(), QStringLiteral("github.com"));
    QVERIFY(url.query().contains(QStringLiteral("graphics.swap_chain_unavailable")));
    QVERIFY(url.toEncoded().size() < 4 * 1024);
}

QTEST_GUILESS_MAIN(SupportTest)
#include "tst_Support.moc"
