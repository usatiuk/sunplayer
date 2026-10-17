#include "platform/linux/AppImageUpdater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
bool writeFile(QString const& path, QByteArray const& data, bool executable = false) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        return false;
    }
    file.close();
    return !executable || file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                              QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
}

QByteArray readFile(QString const& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

int runUpdaterChild(QCoreApplication& app) {
    bool mediaEmpty = true;
    bool started = false;
    auto updater = AppImageUpdater::create([&] { return mediaEmpty; });
    if (!updater) {
        return 10;
    }
    QObject::connect(updater.get(), &AppImageUpdater::changed, &app, [&] {
        if (updater->available() && !started) {
            started = true;
            updater->updateAndRestart();
        }
        if (!updater->error().isEmpty()) {
            app.exit(11);
        }
    });
    QTimer mediaOpen;
    if (qEnvironmentVariable("TEST_UPDATE_MODE") == QStringLiteral("cancel")) {
        QObject::connect(&mediaOpen, &QTimer::timeout, &app, [&] {
            if (!QFile::exists(qEnvironmentVariable("TEST_HELPER_STARTED"))) {
                return;
            }
            mediaEmpty = false;
            updater->cancelForMedia();
            app.exit(updater->updating() ? 12 : 0);
        });
        mediaOpen.start(10);
    }
    QTimer::singleShot(10000, &app, [&] { app.exit(13); });
    return app.exec();
}
} // namespace

class AppImageUpdaterTest final : public QObject {
    Q_OBJECT

  private slots:
    void stagesAndActivates_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("replace-and-clean-restart") << false;
        QTest::newRow("media-open-cancels") << true;
    }

    void stagesAndActivates() {
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString const appDir = directory.filePath(QStringLiteral("mounted"));
        QVERIFY(QDir().mkpath(appDir + QStringLiteral("/usr/libexec/sunplayer")));
        QString const image = directory.filePath(QStringLiteral("Installed.AppImage"));
        QByteArray const original = "#!/bin/sh\nexit 91\n";
        QVERIFY(writeFile(image, original, true));
        auto const permissions = QFile::permissions(image);
        QString const replacement = directory.filePath(QStringLiteral("replacement"));
        QByteArray const payload =
            "#!/bin/sh\n"
            "printf '%s\\n' \"${APPIMAGE-unset}\" \"${APPDIR-unset}\" \"${ARGV0-unset}\" "
            "\"${OWD-unset}\" \"${SUNPLAYER_APPIMAGE_UPDATES-unset}\" "
            "\"${LD_LIBRARY_PATH-unset}\" \"${QT_PLUGIN_PATH-unset}\" \"${QML_IMPORT_PATH-unset}\" "
            "> \"$TEST_RESTART_OUTPUT\"\n";
        QVERIFY(writeFile(replacement, payload));
        QByteArray const helper = "#!/bin/sh\n"
                                  "if [ \"$1\" = --check-for-update ]; then exit 1; fi\n"
                                  "if [ \"$TEST_UPDATE_MODE\" = cancel ]; then\n"
                                  "  printf started > \"$TEST_HELPER_STARTED\"\n"
                                  "  exec /bin/sleep 60\n"
                                  "fi\n"
                                  "exec /bin/cp \"$TEST_REPLACEMENT\" \"$2\"\n";
        QVERIFY(writeFile(appDir + QStringLiteral("/usr/libexec/sunplayer/appimageupdatetool"), helper, true));
        QString const restarted = directory.filePath(QStringLiteral("restarted"));
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("APPIMAGE"), image);
        environment.insert(QStringLiteral("APPDIR"), appDir);
        environment.insert(QStringLiteral("ARGV0"), image);
        environment.insert(QStringLiteral("OWD"), directory.path());
        environment.insert(QStringLiteral("SUNPLAYER_APPIMAGE_UPDATES"), QStringLiteral("1"));
        environment.insert(QStringLiteral("LD_LIBRARY_PATH"), appDir + QStringLiteral("/usr/lib:/expected/system"));
        environment.insert(QStringLiteral("QT_PLUGIN_PATH"), appDir + QStringLiteral("/usr/plugins"));
        environment.insert(QStringLiteral("QML_IMPORT_PATH"), appDir + QStringLiteral("/usr/qml"));
        environment.insert(QStringLiteral("TEST_REPLACEMENT"), replacement);
        environment.insert(QStringLiteral("TEST_RESTART_OUTPUT"), restarted);
        environment.insert(QStringLiteral("TEST_HELPER_STARTED"), directory.filePath(QStringLiteral("started")));
        environment.insert(QStringLiteral("TEST_UPDATE_MODE"),
                           cancel ? QStringLiteral("cancel") : QStringLiteral("update"));
        QProcess child;
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--updater-child")});
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 15000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
        QCOMPARE(readFile(image), cancel ? original : payload);
        QCOMPARE(QFile::permissions(image), permissions);
        if (cancel) {
            QVERIFY(!QFile::exists(restarted));
        } else {
            QTRY_COMPARE(readFile(restarted),
                         QByteArray("unset\nunset\nunset\nunset\nunset\n/expected/system\nunset\nunset\n"));
        }
        QVERIFY(!QFile::exists(image + QStringLiteral(".update.lock")));
        QVERIFY(QDir(directory.path())
                    .entryList({QStringLiteral(".sunplayer-update-*")}, QDir::Dirs | QDir::Hidden)
                    .isEmpty());
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--updater-child"))) {
        return runUpdaterChild(app);
    }
    AppImageUpdaterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_AppImageUpdater.moc"
