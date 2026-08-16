#include <algorithm>
#include <limits>

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "app/ApplicationSettings.h"

namespace {

QStringList* capturedMessages = nullptr;

void captureMessage(QtMsgType, QMessageLogContext const&, QString const& message) {
    if (capturedMessages) {
        capturedMessages->append(message);
    }
}

class MessageCapture final {
  public:
    MessageCapture() : m_previousHandler(qInstallMessageHandler(captureMessage)) { capturedMessages = &m_messages; }

    ~MessageCapture() {
        capturedMessages = nullptr;
        qInstallMessageHandler(m_previousHandler);
    }

    qsizetype settingsFaultCount() const {
        return std::count_if(m_messages.cbegin(), m_messages.cend(), [](QString const& message) {
            return message.contains(QStringLiteral("event=application.settings_fault"));
        });
    }

    bool hasSettingsFault(QString const& reason) const {
        return std::any_of(m_messages.cbegin(), m_messages.cend(), [&reason](QString const& message) {
            return message.contains(QStringLiteral("event=application.settings_fault")) &&
                   message.contains(QStringLiteral("reason=") + reason);
        });
    }

  private:
    QStringList m_messages;
    QtMessageHandler m_previousHandler;
};

QString settingsPath(QTemporaryDir const& directory, QString const& name = QStringLiteral("settings.ini")) {
    return directory.filePath(name);
}

void writeValue(QString const& path, QString const& key, QVariant const& value) {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(key, value);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
}

} // namespace

class ApplicationSettingsTest final : public QObject {
    Q_OBJECT

  private slots:
    void defaultsAndRoundTrip();
    void rejectsInvalidValues();
    void acceptsPortableBooleanRepresentations();
    void rejectsPartiallyParsedFile();
    void reportsAccessFailureOnce();
};

void ApplicationSettingsTest::defaultsAndRoundTrip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString const path = settingsPath(directory);

    writeValue(path, QStringLiteral("future/value"), 17);
    {
        ApplicationSettings settings(path);
        ApplicationSettings::Values const defaults = settings.load();
        QVERIFY(!defaults.volume);
        QVERIFY(!defaults.blankOtherDisplaysInFullscreen);

        settings.setVolume(0.35);
        settings.setBlankOtherDisplaysInFullscreen(true);
        settings.sync();
    }

    {
        ApplicationSettings settings(path);
        ApplicationSettings::Values const restored = settings.load();
        QVERIFY(restored.volume);
        QCOMPARE(*restored.volume, 0.35);
        QVERIFY(restored.blankOtherDisplaysInFullscreen);
        QCOMPARE(*restored.blankOtherDisplaysInFullscreen, true);

        settings.setVolume(0.7);
        settings.sync();
    }

    QSettings stored(path, QSettings::IniFormat);
    QCOMPARE(stored.value(QStringLiteral("playback/volume")).toDouble(), 0.7);
    QCOMPARE(stored.value(QStringLiteral("fullscreen/blankOtherDisplays")).toBool(), true);
    QCOMPARE(stored.value(QStringLiteral("future/value")).toInt(), 17);
}

void ApplicationSettingsTest::rejectsInvalidValues() {
    QList<QVariant> const invalidVolumes{
        true, QStringLiteral("not-a-number"),          -0.01,
        1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
    };

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (qsizetype index = 0; index < invalidVolumes.size(); ++index) {
        QString const path = settingsPath(directory, QStringLiteral("invalid-volume-%1.ini").arg(index));
        writeValue(path, QStringLiteral("playback/volume"), invalidVolumes.at(index));

        MessageCapture messages;
        ApplicationSettings settings(path);
        QVERIFY(!settings.load().volume);
        QCOMPARE(messages.settingsFaultCount(), 1);
        QVERIFY(messages.hasSettingsFault(QStringLiteral("invalid_value")));
    }

    QString const booleanPath = settingsPath(directory, QStringLiteral("invalid-boolean.ini"));
    writeValue(booleanPath, QStringLiteral("fullscreen/blankOtherDisplays"), QStringLiteral("yes"));
    MessageCapture messages;
    ApplicationSettings settings(booleanPath);
    QVERIFY(!settings.load().blankOtherDisplaysInFullscreen);
    QCOMPARE(messages.settingsFaultCount(), 1);
    QVERIFY(messages.hasSettingsFault(QStringLiteral("invalid_value")));
}

void ApplicationSettingsTest::acceptsPortableBooleanRepresentations() {
    QList<QPair<QVariant, bool>> const representations{
        {false, false},
        {true, true},
        {0, false},
        {1, true},
        {QStringLiteral("false"), false},
        {QStringLiteral("true"), true},
        {QStringLiteral("0"), false},
        {QStringLiteral("1"), true},
    };

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (qsizetype index = 0; index < representations.size(); ++index) {
        QString const path = settingsPath(directory, QStringLiteral("boolean-%1.ini").arg(index));
        writeValue(path, QStringLiteral("fullscreen/blankOtherDisplays"), representations.at(index).first);

        ApplicationSettings settings(path);
        ApplicationSettings::Values const values = settings.load();
        QVERIFY(values.blankOtherDisplaysInFullscreen);
        QCOMPARE(*values.blankOtherDisplaysInFullscreen, representations.at(index).second);
    }
}

void ApplicationSettingsTest::rejectsPartiallyParsedFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString const path = settingsPath(directory);

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray const malformedSettings("[playback]\nvolume=0.25\n[broken\n");
    QCOMPARE(file.write(malformedSettings), malformedSettings.size());
    file.close();

    MessageCapture messages;
    ApplicationSettings settings(path);
    ApplicationSettings::Values const values = settings.load();
    QVERIFY(!values.volume);
    QVERIFY(!values.blankOtherDisplaysInFullscreen);
    QCOMPARE(messages.settingsFaultCount(), 1);
    QVERIFY(messages.hasSettingsFault(QStringLiteral("format_error")));
}

void ApplicationSettingsTest::reportsAccessFailureOnce() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString const blockingPath = directory.filePath(QStringLiteral("not-a-directory"));
    QFile blocker(blockingPath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    MessageCapture messages;
    ApplicationSettings settings(blockingPath + QStringLiteral("/settings.ini"));
    ApplicationSettings::Values const values = settings.load();
    QVERIFY(!values.volume);

    settings.setVolume(0.4);
    settings.sync();

    settings.setBlankOtherDisplaysInFullscreen(true);
    settings.sync();
    QCOMPARE(messages.settingsFaultCount(), 1);
    QVERIFY(messages.hasSettingsFault(QStringLiteral("access_error")));
}

QTEST_MAIN(ApplicationSettingsTest)

#include "tst_ApplicationSettings.moc"
