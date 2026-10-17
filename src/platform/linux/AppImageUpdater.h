#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QProcess>

class QLockFile;
class QTemporaryDir;

// The optional capability exists only in updater-enabled AppImage launches.
class AppImageUpdater final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool updating READ updating NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

  public:
    static std::unique_ptr<AppImageUpdater> create(std::function<bool()> mediaEmpty);
    ~AppImageUpdater() override;

    bool available() const { return m_available; }
    bool updating() const;
    QString error() const { return m_error; }
    Q_INVOKABLE void updateAndRestart();
    void cancelForMedia();

  signals:
    void changed();

  private:
    enum class Operation { Idle, Checking, Updating };
    AppImageUpdater(QString image, QString appDir, std::function<bool()> mediaEmpty);
    void run(QStringList arguments);
    void finished(int exitCode, QProcess::ExitStatus exitStatus);
    void fail(QString message);
    void stop();
    void clearStaging();

    QString const m_image;
    QString const m_appDir;
    std::function<bool()> const m_mediaEmpty;
    QProcess m_process;
    Operation m_operation = Operation::Idle;
    bool m_available = false;
    QString m_error;
    std::unique_ptr<QLockFile> m_lock;
    std::unique_ptr<QTemporaryDir> m_staging;
    QByteArray m_originalHash;
    QByteArray m_diagnostics;
};
