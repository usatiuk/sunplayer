#include "app/SupportController.h"

#include <utility>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QTabWidget>
#include <QVBoxLayout>

#include "diagnostics/LogCategories.h"
#include "playback/MediaSession.h"
#include "presentation/PresentationOutputState.h"

#ifdef Q_OS_WIN
#include "platform/windows/WindowsDesktopIntegration.h"
#endif

#ifndef SUNPLAYER_VERSION
#define SUNPLAYER_VERSION "unknown"
#endif
#ifndef SUNPLAYER_BUILD_ID
#define SUNPLAYER_BUILD_ID "unknown"
#endif
#ifndef SUNPLAYER_SOURCE_URL
#define SUNPLAYER_SOURCE_URL "https://github.com/usatiuk/sunplayer"
#endif
#ifndef SUNPLAYER_ISSUES_URL
#define SUNPLAYER_ISSUES_URL "https://github.com/usatiuk/sunplayer/issues/new"
#endif
namespace {
SupportMediaState mediaState(MediaSession::State state) {
    switch (state) {
    case MediaSession::State::Empty:
        return SupportMediaState::Empty;
    case MediaSession::State::Opening:
        return SupportMediaState::Opening;
    case MediaSession::State::Ready:
        return SupportMediaState::Ready;
    case MediaSession::State::Error:
        return SupportMediaState::Error;
    }
    Q_UNREACHABLE();
}

void parentNativeDialog(QWidget& dialog, QWindow* parentWindow) {
    if (!parentWindow) {
        return;
    }
    dialog.winId();
    if (QWindow* const window = dialog.windowHandle()) {
        window->setTransientParent(parentWindow);
    }
}
} // namespace

SupportController::SupportController(bool debugLoggingEnabled, QObject* parent)
    : QObject(parent), m_debugLoggingEnabled(debugLoggingEnabled),
      m_privacyPolicy(loadPackagedText(QStringLiteral("PRIVACY.md"))),
      m_thirdPartyNotices(loadPackagedText(QStringLiteral("ThirdPartyNotices.txt"))) {
    if (m_privacyPolicy.isEmpty()) {
        m_privacyPolicy = QStringLiteral("The privacy policy is not included in this build.");
    }
    if (m_thirdPartyNotices.isEmpty()) {
        m_thirdPartyNotices = QStringLiteral("Third-party notices are not included in this build.");
    }
#ifdef Q_OS_WIN
    WindowsDesktopIntegration::IsolationState const isolation = WindowsDesktopIntegration::isolationState();
    m_windowsAppContainerProcess = isolation.appContainer;
    m_windowsAppSiloProcess = isolation.appSilo;
    if (m_windowsAppContainerProcess == true && m_windowsAppSiloProcess == false) {
        qCWarning(sunplayerLogPlatform) << "The process has an AppContainer token but not an appSilo token.";
    }
#endif
}

SupportController::~SupportController() { delete m_aboutDialog; }

bool SupportController::windowsAppIsolationWarningVisible() const {
    return m_windowsAppContainerProcess == true && m_windowsAppSiloProcess == false;
}

void SupportController::attach(MediaSession& mediaSession, PresentationOutputState& outputState) {
    Q_ASSERT(!m_mediaSession);
    Q_ASSERT(!m_outputState);
    m_mediaSession = &mediaSession;
    m_outputState = &outputState;
    connect(&mediaSession, &MediaSession::sessionChanged, this, &SupportController::updateMediaSnapshot);
    connect(&mediaSession, &MediaSession::playbackMetricsChanged, this, &SupportController::updateMediaSnapshot);
    connect(&mediaSession, &MediaSession::audioDiagnosticsChanged, this, &SupportController::updateMediaSnapshot);
    updateMediaSnapshot();
}

void SupportController::setParentWindow(QWindow* parentWindow) { m_parentWindow = parentWindow; }

void SupportController::setApplicationError(std::optional<ApplicationError> error) {
    m_error = std::move(error);
}

QString SupportController::version() const { return QString::fromLatin1(SUNPLAYER_VERSION); }
QString SupportController::buildId() const { return QString::fromLatin1(SUNPLAYER_BUILD_ID); }
QString SupportController::diagnosticInformation() const { return SupportDiagnostics::detailedReport(snapshot()); }

void SupportController::copyDiagnosticInformation() {
    QGuiApplication::clipboard()->setText(diagnosticInformation());
    setActionStatus(tr("Diagnostic information copied."));
}

void SupportController::reportBug() {
    copyDiagnosticInformation();
    openUrl(SupportDiagnostics::issueUrl(QUrl(QStringLiteral(SUNPLAYER_ISSUES_URL)), snapshot()),
            tr("Diagnostic information copied; the bug report was requested from your system browser."),
            tr("Diagnostic information was copied, but the system browser could not be opened."));
}

void SupportController::showAbout() {
    if (m_aboutDialog) {
        m_aboutDialog->raise();
        m_aboutDialog->activateWindow();
        return;
    }

    auto* const dialog = new QDialog;
    m_aboutDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QString const displayName = QGuiApplication::applicationDisplayName();
    dialog->setWindowTitle(tr("About %1").arg(displayName));
    dialog->setWindowIcon(QGuiApplication::windowIcon());
    dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(720, 580);

    auto* const layout = new QVBoxLayout(dialog);

    auto* const title = new QLabel(tr("%1 %2").arg(displayName, version()), dialog);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* const build = new QLabel(tr("Build %1").arg(buildId()), dialog);
    build->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(build);

    auto* const tabs = new QTabWidget(dialog);
    auto* const about =
        new QLabel(tr("SunPlayer is a GPL-3.0-or-later HDR video player.\n\n"
                      "Report a bug from the player menu to copy privacy-safe diagnostics and open a new issue."),
                   tabs);
    about->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    about->setWordWrap(true);
    about->setMargin(12);
    tabs->addTab(about, tr("About"));

    auto makeTextPage = [tabs](QString const& text) {
        auto* const editor = new QPlainTextEdit(tabs);
        editor->setReadOnly(true);
        editor->setPlainText(text);
        return editor;
    };
    tabs->addTab(makeTextPage(m_thirdPartyNotices), tr("Third-party licenses"));
    tabs->addTab(makeTextPage(m_privacyPolicy), tr("Privacy"));
    layout->addWidget(tabs, 1);

    auto* const status = new QLabel(dialog);
    status->setWordWrap(true);
    status->setVisible(false);
    layout->addWidget(status);

    auto const refreshStatus = [this, status] {
        status->setText(m_actionStatus);
        status->setVisible(!m_actionStatus.isEmpty());
    };
    refreshStatus();
    connect(this, &SupportController::actionStatusChanged, dialog, refreshStatus);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto* const sourceButton = buttons->addButton(tr("Source code"), QDialogButtonBox::ActionRole);
    auto* const hdrLabButton = buttons->addButton(tr("Open HDR Lab"), QDialogButtonBox::ActionRole);
    auto* const copyButton = buttons->addButton(tr("Copy diagnostic information"), QDialogButtonBox::ActionRole);
    connect(sourceButton, &QPushButton::clicked, this, &SupportController::openSourceCode);
    connect(hdrLabButton, &QPushButton::clicked, dialog, [this, dialog] {
        dialog->accept();
        emit hdrLabRequested();
    });
    connect(copyButton, &QPushButton::clicked, this, &SupportController::copyDiagnosticInformation);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    parentNativeDialog(*dialog, m_parentWindow);
    dialog->open();
}

void SupportController::openSourceCode() {
    openUrl(QUrl(QStringLiteral(SUNPLAYER_SOURCE_URL)), tr("The source-code page was requested from your system browser."),
            tr("The source-code page could not be opened in the system browser."));
}
QString SupportController::loadPackagedText(QString const& fileName) {
    QStringList const candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/sunplayer/%1").arg(fileName)),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/%1").arg(fileName)),
        QDir(QCoreApplication::applicationDirPath()).filePath(fileName),
    };
    for (QString const& path : candidates) {
        QFile file(QDir::cleanPath(path));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString::fromUtf8(file.readAll());
        }
    }
    QFile resource(QStringLiteral(":/legal/%1").arg(fileName));
    if (resource.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(resource.readAll());
    }
    return {};
}

void SupportController::updateMediaSnapshot() {
    if (!m_mediaSession) {
        return;
    }
    if (m_mediaSession->state() == MediaSession::State::Empty) {
        m_currentMedia.reset();
        return;
    }
    SupportMediaSnapshot media{
        .state = mediaState(m_mediaSession->state()),
        .hasVideoFrame = m_mediaSession->hasFrame(),
        .videoHdr = m_mediaSession->videoHdr(),
        .hasAudioOutput = m_mediaSession->hasAudioOutput(),
        .durationMilliseconds = m_mediaSession->durationMilliseconds(),
        .decodedVideoFrames = m_mediaSession->decodedFrameCount(),
        .droppedVideoFrames = m_mediaSession->droppedFrameCount(),
        .decoder = m_mediaSession->decoderName(),
        .decodePath = m_mediaSession->decodePath(),
        .audioBackend = m_mediaSession->audioBackend(),
    };
    m_currentMedia = media;
    m_lastMedia = std::move(media);
}

SupportSnapshot SupportController::snapshot() const {
    SupportSnapshot result{
        .version = version(),
        .buildId = buildId(),
        .qtVersion = QString::fromLatin1(qVersion()),
        .operatingSystem = QSysInfo::prettyProductName(),
        .cpuArchitecture = QSysInfo::currentCpuArchitecture(),
        .debugLoggingEnabled = m_debugLoggingEnabled,
        .currentMedia = m_currentMedia,
        .lastMedia = m_lastMedia,
    };
    result.windowsAppContainerProcess = m_windowsAppContainerProcess;
    result.windowsAppSiloProcess = m_windowsAppSiloProcess;
    if (m_outputState) {
        result.graphicsApi = m_outputState->graphicsApi();
        result.swapChainFormat = m_outputState->swapChainFormat();
        result.videoSurfaceProducer = m_outputState->videoSurfaceProducer();
        result.videoColorPolicy = m_outputState->videoColorPolicy();
        result.displayColorMode = m_outputState->displayColorMode();
        result.displayHdrEnabled = m_outputState->displayHdrEnabled();
        result.hdrPresentationActive = m_outputState->hdrPresentationActive();
        result.sdrWhiteKnown = m_outputState->sdrWhiteKnown();
        result.sdrWhiteNits = m_outputState->sdrWhiteNits();
        result.effectiveTargetHeadroom = m_outputState->effectiveTargetHeadroom();
    }
    if (m_error) {
        result.applicationErrorCode = m_error->stableCode();
        result.applicationErrorSubsystem = m_error->subsystemName();
    }
    return result;
}

void SupportController::setActionStatus(QString status) {
    if (m_actionStatus == status) {
        return;
    }
    m_actionStatus = std::move(status);
    emit actionStatusChanged();
}

void SupportController::openUrl(QUrl const& url, QString const& successMessage, QString const& failureMessage) {
#ifdef Q_OS_WIN
    WindowsDesktopIntegration::openExternalUrl(*this, url, [this, successMessage, failureMessage](bool opened) {
        finishOpenUrl(opened, successMessage, failureMessage);
    });
#else
    finishOpenUrl(QDesktopServices::openUrl(url), successMessage, failureMessage);
#endif
}

void SupportController::finishOpenUrl(bool opened, QString const& successMessage, QString const& failureMessage) {
    if (opened) {
        setActionStatus(successMessage);
        return;
    }
    setActionStatus(failureMessage);
    QMessageBox dialog(QMessageBox::Warning, tr("Could not open browser"), failureMessage, QMessageBox::Ok);
    dialog.setWindowModality(Qt::WindowModal);
    parentNativeDialog(dialog, m_parentWindow);
    dialog.exec();
}
