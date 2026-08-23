#include "app/ApplicationError.h"

#include <array>
#include <utility>

namespace {
struct ErrorDescriptor {
    ApplicationError::Code code;
    char const* stableCode;
    ApplicationErrorSubsystem subsystem;
    ApplicationErrorRecoverability recoverability;
    ApplicationErrorActions actions;
};

constexpr auto descriptors = std::to_array<ErrorDescriptor>({
    {ApplicationError::Code::None, "none", ApplicationErrorSubsystem::Application,
     ApplicationErrorRecoverability::NotRecoverable, ApplicationErrorAction::None},
    {ApplicationError::Code::PlatformStartupFailed, "platform.startup_failed", ApplicationErrorSubsystem::Platform,
     ApplicationErrorRecoverability::RestartRequired,
     ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug | ApplicationErrorAction::Quit},
    {ApplicationError::Code::PresentationInitializationFailed, "presentation.initialization_failed",
     ApplicationErrorSubsystem::Presentation, ApplicationErrorRecoverability::Retryable,
     ApplicationErrorAction::Retry | ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug |
         ApplicationErrorAction::Quit},
    {ApplicationError::Code::UiRenderingUnavailable, "presentation.ui_rendering_unavailable",
     ApplicationErrorSubsystem::Presentation, ApplicationErrorRecoverability::Retryable,
     ApplicationErrorAction::Retry | ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug |
         ApplicationErrorAction::Quit},
    {ApplicationError::Code::CompositorUnavailable, "graphics.compositor_unavailable",
     ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable,
     ApplicationErrorAction::Retry | ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug |
         ApplicationErrorAction::Quit},
    {ApplicationError::Code::DiagnosticVideoUnavailable, "graphics.diagnostic_video_unavailable",
     ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable,
     ApplicationErrorAction::Retry | ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug |
         ApplicationErrorAction::Quit},
    {ApplicationError::Code::SwapChainUnavailable, "graphics.swap_chain_unavailable",
     ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable,
     ApplicationErrorAction::Retry | ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug |
         ApplicationErrorAction::Quit},
    {ApplicationError::Code::GraphicsDeviceRecoveryExhausted, "graphics.device_recovery_exhausted",
     ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::RestartRequired,
     ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug | ApplicationErrorAction::Quit},
    {ApplicationError::Code::FrameSubmissionFailed, "graphics.frame_submission_failed",
     ApplicationErrorSubsystem::Graphics, ApplicationErrorRecoverability::Retryable,
     ApplicationErrorAction::Retry | ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug |
         ApplicationErrorAction::Quit},
    {ApplicationError::Code::GraphicsCleanupFailed, "graphics.cleanup_failed", ApplicationErrorSubsystem::Graphics,
     ApplicationErrorRecoverability::RestartRequired,
     ApplicationErrorAction::Restart | ApplicationErrorAction::ReportBug | ApplicationErrorAction::Quit},
    {ApplicationError::Code::RestartFailed, "application.restart_failed", ApplicationErrorSubsystem::Application,
     ApplicationErrorRecoverability::NotRecoverable, ApplicationErrorAction::ReportBug | ApplicationErrorAction::Quit},
});

ErrorDescriptor const& descriptor(ApplicationError::Code code) {
    for (ErrorDescriptor const& candidate : descriptors) {
        if (candidate.code == code) {
            return candidate;
        }
    }
    Q_UNREACHABLE();
}
} // namespace

ApplicationError::ApplicationError(Code code, QString userMessage, QString technicalDetail)
    : m_code(code), m_userMessage(std::move(userMessage)), m_technicalDetail(std::move(technicalDetail)) {
    Q_ASSERT(code != Code::None);
    Q_ASSERT(!m_userMessage.trimmed().isEmpty());
}

bool ApplicationError::isValid() const { return m_code != Code::None; }
ApplicationError::Code ApplicationError::code() const { return m_code; }
QString ApplicationError::stableCode() const { return QString::fromLatin1(descriptor(m_code).stableCode); }
ApplicationErrorSubsystem ApplicationError::subsystem() const { return descriptor(m_code).subsystem; }

QString ApplicationError::subsystemName() const {
    switch (subsystem()) {
    case ApplicationErrorSubsystem::Application:
        return QStringLiteral("application");
    case ApplicationErrorSubsystem::Platform:
        return QStringLiteral("platform");
    case ApplicationErrorSubsystem::Presentation:
        return QStringLiteral("presentation");
    case ApplicationErrorSubsystem::Graphics:
        return QStringLiteral("graphics");
    }
    Q_UNREACHABLE();
}

QString ApplicationError::userMessage() const { return m_userMessage; }
QString ApplicationError::technicalDetail() const { return m_technicalDetail; }
ApplicationErrorRecoverability ApplicationError::recoverability() const { return descriptor(m_code).recoverability; }
ApplicationErrorActions ApplicationError::suggestedActions() const { return descriptor(m_code).actions; }
