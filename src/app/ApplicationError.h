#pragma once

#include <QFlags>
#include <QMetaType>
#include <QString>

enum class ApplicationErrorSubsystem {
    Application,
    Platform,
    Presentation,
    Graphics,
};

enum class ApplicationErrorRecoverability {
    Retryable,
    RestartRequired,
    NotRecoverable,
};

enum class ApplicationErrorAction {
    None = 0,
    Retry = 1 << 0,
    Restart = 1 << 1,
    ReportBug = 1 << 2,
    Quit = 1 << 3,
};
Q_DECLARE_FLAGS(ApplicationErrorActions, ApplicationErrorAction)
Q_DECLARE_OPERATORS_FOR_FLAGS(ApplicationErrorActions)

class ApplicationError final {
  public:
    enum class Code {
        None,
        PlatformStartupFailed,
        PresentationInitializationFailed,
        UiRenderingUnavailable,
        CompositorUnavailable,
        DiagnosticVideoUnavailable,
        SwapChainUnavailable,
        GraphicsDeviceRecoveryExhausted,
        FrameSubmissionFailed,
        GraphicsCleanupFailed,
        RestartFailed,
    };

    ApplicationError() = default;
    ApplicationError(Code code, QString userMessage, QString technicalDetail = {});

    bool isValid() const;
    Code code() const;
    QString stableCode() const;
    ApplicationErrorSubsystem subsystem() const;
    QString subsystemName() const;
    QString userMessage() const;
    QString technicalDetail() const;
    ApplicationErrorRecoverability recoverability() const;
    ApplicationErrorActions suggestedActions() const;

  private:
    Code m_code = Code::None;
    QString m_userMessage;
    QString m_technicalDetail;
};

Q_DECLARE_METATYPE(ApplicationError)
