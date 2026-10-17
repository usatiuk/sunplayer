#include "platform/linux/AppImageUpdater.h"

#include <cstdio>
#include <utility>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>

namespace {
QByteArray fileHash(QString const& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result() : QByteArray{};
}
} // namespace

std::unique_ptr<AppImageUpdater> AppImageUpdater::create(std::function<bool()> mediaEmpty) {
    QString const image = QFileInfo(qEnvironmentVariable("APPIMAGE")).canonicalFilePath();
    QString const appDir = qEnvironmentVariable("APPDIR");
    if (qEnvironmentVariable("SUNPLAYER_APPIMAGE_UPDATES") != QStringLiteral("1") || image.isEmpty() ||
        appDir.isEmpty()) {
        return {};
    }
    return std::unique_ptr<AppImageUpdater>(new AppImageUpdater(image, appDir, std::move(mediaEmpty)));
}

AppImageUpdater::AppImageUpdater(QString image, QString appDir, std::function<bool()> mediaEmpty)
    : m_image(std::move(image)), m_appDir(std::move(appDir)), m_mediaEmpty(std::move(mediaEmpty)) {
    if (!QFileInfo(m_appDir + QStringLiteral("/usr/libexec/sunplayer/appimageupdatetool")).isExecutable()) {
        qFatal("Updater-enabled AppImage is missing its executable updater");
    }
    connect(&m_process, &QProcess::finished, this, &AppImageUpdater::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (updating()) {
                fail(tr("Could not start the updater."));
            } else {
                qWarning() << "AppImage update check could not start:" << m_process.errorString();
                m_operation = Operation::Idle;
            }
        }
    });
    // Drain diagnostics while the helper runs; its terminal output is not a UI API.
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { m_process.readAllStandardOutput(); });
    connect(&m_process, &QProcess::readyReadStandardError, this,
            [this] { m_diagnostics = (m_diagnostics + m_process.readAllStandardError()).right(4096); });
    QTimer::singleShot(0, this, [this] {
        m_operation = Operation::Checking;
        run({QStringLiteral("--check-for-update"), m_image});
    });
}

AppImageUpdater::~AppImageUpdater() { stop(); }

bool AppImageUpdater::updating() const { return m_operation == Operation::Updating; }

void AppImageUpdater::run(QStringList arguments) {
    m_diagnostics.clear();
    m_process.setProgram(m_appDir + QStringLiteral("/usr/libexec/sunplayer/appimageupdatetool"));
    m_process.setArguments(arguments);
    m_process.start();
}

void AppImageUpdater::updateAndRestart() {
    if (!m_available || m_operation != Operation::Idle || !m_mediaEmpty()) {
        return;
    }
    m_error.clear();
    m_lock = std::make_unique<QLockFile>(m_image + QStringLiteral(".update.lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        fail(tr("Another update is running, or this location is not writable."));
        return;
    }
    m_staging = std::make_unique<QTemporaryDir>(QFileInfo(m_image).absolutePath() +
                                                QStringLiteral("/.sunplayer-update-XXXXXX"));
    QString const candidate = m_staging->filePath(QStringLiteral("SunPlayer.AppImage"));
    if (!m_staging->isValid() || !QFile::copy(m_image, candidate)) {
        fail(tr("Could not stage the update beside the installed AppImage."));
        return;
    }
    m_originalHash = fileHash(candidate);
    if (m_originalHash.isEmpty()) {
        fail(tr("Could not read the installed AppImage."));
        return;
    }
    m_operation = Operation::Updating;
    emit changed();
    run({QStringLiteral("--overwrite"), candidate});
}

void AppImageUpdater::finished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_operation == Operation::Checking) {
        m_operation = Operation::Idle;
        m_available = exitStatus == QProcess::NormalExit && exitCode == 1;
        if (exitStatus != QProcess::NormalExit || exitCode > 1) {
            qWarning() << "AppImage update check failed:" << exitCode << m_diagnostics;
        }
        emit changed();
        return;
    }
    if (!updating()) {
        return;
    }
    if (!m_mediaEmpty()) {
        cancelForMedia();
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        fail(tr("The update failed. Your installed AppImage is unchanged."));
        return;
    }
    QString const candidate = m_staging->filePath(QStringLiteral("SunPlayer.AppImage"));
    QByteArray const updatedHash = fileHash(candidate);
    if (updatedHash.isEmpty() || updatedHash == m_originalHash ||
        !QFile::setPermissions(candidate, QFile::permissions(m_image))) {
        fail(tr("The updater did not produce a replacement AppImage."));
        return;
    }
    // Both files are on the same filesystem. POSIX rename replaces the path
    // atomically while the running image's existing mount remains alive.
    if (::rename(QFile::encodeName(candidate).constData(), QFile::encodeName(m_image).constData()) != 0) {
        fail(tr("Could not replace the installed AppImage."));
        return;
    }
    clearStaging();
    m_operation = Operation::Idle;
    m_available = false;
    QProcess launcher;
    auto environment = QProcessEnvironment::systemEnvironment();
    for (QString const& name :
         {QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("QML_IMPORT_PATH")}) {
        QStringList paths = environment.value(name).split(QLatin1Char(':'), Qt::SkipEmptyParts);
        paths.removeIf(
            [this](QString const& path) { return path == m_appDir || path.startsWith(m_appDir + QLatin1Char('/')); });
        if (paths.isEmpty()) {
            environment.remove(name);
        } else {
            environment.insert(name, paths.join(QLatin1Char(':')));
        }
    }
    for (char const* name : {"APPIMAGE", "APPDIR", "ARGV0", "OWD", "SUNPLAYER_APPIMAGE_UPDATES"}) {
        environment.remove(QLatin1String(name));
    }
    launcher.setProcessEnvironment(environment);
    launcher.setProgram(m_image);
    if (!launcher.startDetached()) {
        fail(tr("The update is installed. Quit and reopen SunPlayer to use it."));
        return;
    }
    QCoreApplication::quit();
}

void AppImageUpdater::clearStaging() {
    m_staging.reset();
    m_lock.reset();
    m_originalHash.clear();
}

void AppImageUpdater::fail(QString message) {
    qWarning() << "AppImage update failed:" << message << m_diagnostics;
    m_operation = Operation::Idle;
    clearStaging();
    m_error = std::move(message);
    emit changed();
}

void AppImageUpdater::stop() {
    m_operation = Operation::Idle;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        if (!m_process.waitForFinished(1000)) {
            qFatal("AppImage updater did not exit after SIGKILL");
        }
    }
    clearStaging();
}

void AppImageUpdater::cancelForMedia() {
    if (!updating()) {
        return;
    }
    stop();
    m_error.clear();
    emit changed();
}
