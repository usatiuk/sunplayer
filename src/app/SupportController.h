#pragma once

#include <optional>

#include <QDialog>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include "app/ApplicationError.h"
#include "app/SupportDiagnostics.h"

class MediaSession;
class PresentationOutputState;

class SupportController final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SupportController is owned by the application")

  public:
    explicit SupportController(bool debugLoggingEnabled, QObject* parent = nullptr);
    ~SupportController() override;

    void attach(MediaSession& mediaSession, PresentationOutputState& outputState);
    void setParentWindow(QWindow* parentWindow);
    void setApplicationError(std::optional<ApplicationError> error);

    Q_INVOKABLE void reportBug();
    Q_INVOKABLE void showAbout();

  signals:
    void actionStatusChanged();
    void hdrLabRequested();

  private:
    static QString loadPackagedText(QString const& fileName);
    QString version() const;
    QString buildId() const;
    QString diagnosticInformation() const;
    void copyDiagnosticInformation();
    void openSourceCode();
    void updateMediaSnapshot();
    SupportSnapshot snapshot() const;
    void setActionStatus(QString status);
    bool openUrl(QUrl const& url, QString const& successMessage, QString const& failureMessage);

    bool const m_debugLoggingEnabled;
    QPointer<QWindow> m_parentWindow;
    QPointer<QDialog> m_aboutDialog;
    QPointer<MediaSession> m_mediaSession;
    QPointer<PresentationOutputState> m_outputState;
    std::optional<SupportMediaSnapshot> m_currentMedia;
    std::optional<SupportMediaSnapshot> m_lastMedia;
    std::optional<ApplicationError> m_error;
    QString m_privacyPolicy;
    QString m_thirdPartyNotices;
    QString m_actionStatus;
};
