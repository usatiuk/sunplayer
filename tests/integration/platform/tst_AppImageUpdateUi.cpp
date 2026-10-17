#include "platform/linux/AppImageUpdateUi.h"
#include "platform/linux/AppImageUpdater.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

class AppImageUpdateUiTest final : public QObject {
    Q_OBJECT

  private slots:
    void init() {
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        QString const appDir = m_directory->filePath(QStringLiteral("mounted"));
        QVERIFY(QDir().mkpath(appDir + QStringLiteral("/usr/libexec/sunplayer")));
        QFile helper(appDir + QStringLiteral("/usr/libexec/sunplayer/appimageupdatetool"));
        QVERIFY(helper.open(QIODevice::WriteOnly));
        QVERIFY(helper.write("#!/bin/sh\nprintf check >> \"$TEST_UPDATE_CHECK_LOG\"\nexit 1\n") > 0);
        helper.close();
        QVERIFY(helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QFile image(m_directory->filePath(QStringLiteral("Installed.AppImage")));
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.close();
        m_environment = QProcessEnvironment::systemEnvironment();
        qputenv("APPIMAGE", image.fileName().toUtf8());
        qputenv("APPDIR", appDir.toUtf8());
        qputenv("SUNPLAYER_APPIMAGE_UPDATES", "1");
        qputenv("TEST_UPDATE_CHECK_LOG", m_directory->filePath(QStringLiteral("checks")).toUtf8());
        m_mediaEmpty = true;
        m_updater = AppImageUpdater::create([this] { return m_mediaEmpty; });
        QVERIFY(m_updater);
    }

    void cleanup() {
        m_ui.reset();
        m_updater.reset();
        m_directory.reset();
        for (auto const* name : {"APPIMAGE", "APPDIR", "SUNPLAYER_APPIMAGE_UPDATES", "TEST_UPDATE_CHECK_LOG"}) {
            if (m_environment.contains(QLatin1String(name))) {
                qputenv(name, m_environment.value(QLatin1String(name)).toUtf8());
            } else {
                qunsetenv(name);
            }
        }
    }

    void consent_data() {
        QTest::addColumn<bool>("yes");
        QTest::newRow("yes-checks-and-persists") << true;
        QTest::newRow("no-skips-and-persists") << false;
    }

    void noCheckWithoutConsent_data() {
        QTest::addColumn<bool>("answeredNo");
        QTest::newRow("unanswered") << false;
        QTest::newRow("saved-no") << true;
    }

    void noCheckWithoutConsent() {
        QFETCH(bool, answeredNo);
        if (answeredNo) {
            QSettings(settingsFile(), QSettings::IniFormat)
                .setValue(QStringLiteral("linux/appimage/checkOnStartup"), false);
        }
        createUi();
        m_ui->reconcile();
        // Observe the negative asynchronous contract, including queued startup
        // work, rather than merely testing availability before a helper exits.
        QTest::qWait(100);
        QVERIFY(!QFile::exists(m_directory->filePath(QStringLiteral("checks"))));
        QVERIFY(!m_updater->available());
    }

    void consent() {
        QFETCH(bool, yes);
        createUi();
        m_ui->reconcile();
        auto* prompt = consentDialog();
        QVERIFY(prompt);
        QTRY_VERIFY(prompt->isVisible());
        QVERIFY(!m_updater->available());
        auto* const buttons = prompt->findChild<QDialogButtonBox*>();
        QVERIFY(buttons);
        buttons->button(yes ? QDialogButtonBox::Yes : QDialogButtonBox::No)->click();
        QTRY_VERIFY(!consentDialog());
        QCOMPARE(QSettings(settingsFile(), QSettings::IniFormat).value(QStringLiteral("linux/appimage/checkOnStartup")),
                 QVariant(yes));
        if (yes) {
            QTRY_VERIFY(m_updater->available());
            m_updater->dismiss();
            QVERIFY(!m_updater->available());
            m_ui->reconcile();
            QVERIFY(!m_updater->available());
        }
        m_ui.reset();
        createUi();
        m_ui->reconcile();
        QVERIFY(!consentDialog());
        if (yes) {
            QTRY_VERIFY(m_updater->available());
        } else {
            QVERIFY(!m_updater->available());
        }
    }

    void defersAndLeavesClosedQuestionUnanswered() {
        m_mediaEmpty = false;
        createUi();
        m_ui->reconcile();
        QVERIFY(!consentDialog());
        QVERIFY(!m_updater->available());
        m_mediaEmpty = true;
        m_ui->reconcile();
        QVERIFY(consentDialog());
        consentDialog()->close();
        QTRY_VERIFY(!consentDialog());
        QVERIFY(
            !QSettings(settingsFile(), QSettings::IniFormat).contains(QStringLiteral("linux/appimage/checkOnStartup")));
        m_ui->reconcile();
        QVERIFY(!consentDialog());
        m_ui.reset();
        createUi();
        m_ui->reconcile();
        QVERIFY(consentDialog());
        m_mediaEmpty = false;
        m_ui->reconcile();
        QTRY_VERIFY(!consentDialog());
        QVERIFY(!m_updater->available());
    }

    void settingsApplyImmediately() {
        QSettings(settingsFile(), QSettings::IniFormat)
            .setValue(QStringLiteral("linux/appimage/checkOnStartup"), false);
        createUi();
        std::unique_ptr<QWidget> page(m_ui->createSettingsPage());
        auto* const checkbox = page->findChild<QCheckBox*>(QStringLiteral("appImageCheckOnStartup"));
        QVERIFY(checkbox);
        QVERIFY(!checkbox->isChecked());
        m_ui->reconcile();
        QVERIFY(!consentDialog());
        checkbox->setChecked(true);
        QTRY_VERIFY(m_updater->available());
        m_updater->dismiss();
        checkbox->setChecked(false);
        QCOMPARE(QSettings(settingsFile(), QSettings::IniFormat).value(QStringLiteral("linux/appimage/checkOnStartup")),
                 QVariant(false));
        m_ui.reset();
        createUi();
        m_ui->reconcile();
        QVERIFY(!consentDialog());
        QVERIFY(!m_updater->available());
    }

  private:
    QString settingsFile() const { return m_directory->filePath(QStringLiteral("settings.ini")); }
    void createUi() {
        m_ui =
            std::make_unique<AppImageUpdateUi>(*m_updater, m_window, [this] { return m_mediaEmpty; }, settingsFile());
    }
    static QDialog* consentDialog() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("appImageUpdateConsent")) {
                return qobject_cast<QDialog*>(widget);
            }
        }
        return nullptr;
    }
    QWindow m_window;
    QProcessEnvironment m_environment;
    bool m_mediaEmpty = true;
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<AppImageUpdater> m_updater;
    std::unique_ptr<AppImageUpdateUi> m_ui;
};

QTEST_MAIN(AppImageUpdateUiTest)
#include "tst_AppImageUpdateUi.moc"
