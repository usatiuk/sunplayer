#include <memory>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "diagnostics/ApplicationLog.h"
#include "diagnostics/LogCategories.h"

namespace {
void discardMessage(
        QtMsgType,
        const QMessageLogContext &,
        const QString &) {}
}

Q_LOGGING_CATEGORY(
    sunroomTestLog,
    "sunroom.test",
    QtInfoMsg)

class ApplicationLogTest final : public QObject {
    Q_OBJECT

public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(
            SEM_FAILCRITICALERRORS
            | SEM_NOGPFAULTERRORBOX
            | SEM_NOOPENFILEERRORBOX);
#endif
    }

private slots:
    void writesCategorizedInfoRecord();
    void boundsSessionFile();
    void boundsProducerQueue();
    void concurrentFlushesShareOneWatermark();
    void preservesFatalRecordUnderQueuePressure();
    void customFileDoesNotPruneSiblingLogs();
    void rejectsWindowsRemoteCustomFile();
    void acceptsWindowsExtendedLocalPath();
    void rejectsWindowsMappedRemoteCustomFile();
    void honorsQtDebugCategoryRules();
};

void ApplicationLogTest::writesCategorizedInfoRecord() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("session.log"));
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = path,
                .maximumFileBytes = 4096,
                .retainedFileCount = 2,
            },
            &error);
    QVERIFY2(logging, qPrintable(error));

    qCInfo(sunroomLogApplication).noquote()
        << "event=test.record value=42";
    logging->flush();
    logging.reset();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.contains("level=info"));
    QVERIFY(contents.contains(
        "category=sunroom.application"));
    QVERIFY(contents.contains(
        "event=test.record value=42"));
}

void ApplicationLogTest::boundsSessionFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("bounded.log"));
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = path,
                .maximumFileBytes = 256,
                .retainedFileCount = 2,
            },
            &error);
    QVERIFY2(logging, qPrintable(error));

    qCInfo(sunroomLogApplication).noquote()
        << "event=test.oversized"
        << QString(1024, u'x');
    logging->flush();
    logging.reset();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.size() <= 256);
    QVERIFY(contents.contains("event=log.truncated"));
}

void ApplicationLogTest::boundsProducerQueue() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("queue.log"));
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = path,
                .maximumFileBytes = 4096,
                .retainedFileCount = 2,
                .maximumQueuedBytes = 256,
                .maximumQueuedRecords = 1,
            },
            &error);
    QVERIFY2(logging, qPrintable(error));

    qCInfo(sunroomLogApplication).noquote()
        << "event=test.oversized_queue_record"
        << QString(1024, u'x');
    logging->flush();
    logging.reset();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.contains(
        "event=log.records_dropped"));
    QVERIFY(contents.contains("count=1"));
}

void ApplicationLogTest::
concurrentFlushesShareOneWatermark() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("flush.log"));
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = path,
                .maximumFileBytes = 4096,
                .retainedFileCount = 2,
                .maximumQueuedRecords = 1,
            },
            &error);
    QVERIFY2(logging, qPrintable(error));

    qCInfo(sunroomLogApplication).noquote()
        << "event=test.concurrent_flush";
    std::vector<std::jthread> flushers;
    for (int index = 0; index < 32; ++index) {
        flushers.emplace_back(
            [log = logging.get()] {
                for (int pass = 0;
                        pass < 8;
                        ++pass) {
                    log->flush();
                }
            });
    }
    flushers.clear();
    logging.reset();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.contains(
        "event=test.concurrent_flush"));
}

void ApplicationLogTest::
preservesFatalRecordUnderQueuePressure() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("fatal.log"));

    const QtMessageHandler originalHandler =
        qInstallMessageHandler(discardMessage);
    const auto restoreHandler = qScopeGuard(
        [originalHandler] {
            qInstallMessageHandler(originalHandler);
        });
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = path,
                .maximumFileBytes = 4096,
                .retainedFileCount = 2,
                .maximumQueuedBytes = 256,
                .maximumQueuedRecords = 1,
            },
            &error);
    QVERIFY2(logging, qPrintable(error));

    qCInfo(sunroomLogApplication).noquote()
        << "event=test.queue_pressure"
        << QString(1024, u'x');
    const QtMessageHandler applicationHandler =
        qInstallMessageHandler(discardMessage);
    QVERIFY(applicationHandler);
    qInstallMessageHandler(applicationHandler);
    const QString fatalMessage =
        QStringLiteral(
            "event=test.fatal_preserved ")
        + QString(1024, u'y');
    applicationHandler(
        QtFatalMsg,
        QMessageLogContext(
            __FILE__,
            __LINE__,
            Q_FUNC_INFO,
            "sunroom.test"),
        fatalMessage);
    logging->flush();
    logging.reset();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.contains("level=fatal"));
    QVERIFY(contents.contains(
        "event=test.fatal_preserved"));
    QVERIFY(contents.contains(
        "event=log.fatal_record_truncated"));
}

void ApplicationLogTest::
customFileDoesNotPruneSiblingLogs() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString siblingPath =
        directory.filePath(
            QStringLiteral("sunroom-unrelated.log"));
    QFile sibling(siblingPath);
    QVERIFY(sibling.open(
        QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(sibling.write("keep"), 4);
    sibling.close();

    const QString selectedPath =
        directory.filePath(QStringLiteral("selected.log"));
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = selectedPath,
                .maximumFileBytes = 4096,
                .retainedFileCount = 1,
            },
            &error);
    QVERIFY2(logging, qPrintable(error));
    qCInfo(sunroomLogApplication)
        << "event=test.custom_file";
    logging.reset();

    QVERIFY(QFileInfo::exists(siblingPath));
}

void ApplicationLogTest::
rejectsWindowsRemoteCustomFile() {
#ifdef Q_OS_WIN
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = QStringLiteral(
                    R"(\\sunroom.invalid\share\session.log)"),
            },
            &error);
    QVERIFY(!logging);
    QVERIFY(error.contains(
        QStringLiteral("local file path")));
#else
    QSKIP(
        "Remote custom log paths are currently "
        "classified by the Windows adapter");
#endif
}

void ApplicationLogTest::
acceptsWindowsExtendedLocalPath() {
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString ordinaryPath =
        directory.filePath(
            QStringLiteral("extended.log"));
    const QString extendedPath =
        QStringLiteral("\\\\?\\")
        + QDir::toNativeSeparators(
            ordinaryPath);
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {.filePath = extendedPath},
            &error);
    QVERIFY2(logging, qPrintable(error));
    qCInfo(sunroomLogApplication)
        << "event=test.extended_local_path";
    logging->flush();
    logging.reset();
    QVERIFY(QFileInfo::exists(ordinaryPath));
#else
    QSKIP("Windows extended paths are platform-specific");
#endif
}

void ApplicationLogTest::
rejectsWindowsMappedRemoteCustomFile() {
#ifdef Q_OS_WIN
    for (wchar_t drive = L'A';
            drive <= L'Z';
            ++drive) {
        const std::wstring root{
            drive, L':', L'\\'};
        if (GetDriveTypeW(root.c_str())
                != DRIVE_REMOTE) {
            continue;
        }
        QString error;
        std::unique_ptr<ApplicationLog> logging =
            ApplicationLog::install(
                {
                    .filePath =
                        QString::fromWCharArray(
                            root.c_str())
                        + QStringLiteral(
                            "sunroom-log-policy-test.log"),
                },
                &error);
        QVERIFY(!logging);
        QVERIFY(error.contains(
            QStringLiteral("local file path")));
        return;
    }
    QSKIP("No mapped remote drive is present");
#else
    QSKIP("Windows mapped drives are platform-specific");
#endif
}

void ApplicationLogTest::
honorsQtDebugCategoryRules() {
    QLoggingCategory::setFilterRules(
        QStringLiteral("sunroom.test.debug=true"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("debug.log"));
    QString error;
    std::unique_ptr<ApplicationLog> logging =
        ApplicationLog::install(
            {
                .filePath = path,
                .maximumFileBytes = 4096,
                .retainedFileCount = 2,
            },
            &error);
    QVERIFY2(logging, qPrintable(error));

    qCDebug(sunroomTestLog).noquote()
        << "event=test.qt_debug_rule";
    logging->flush();
    logging.reset();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.contains("level=debug"));
    QVERIFY(contents.contains(
        "event=test.qt_debug_rule"));
}

QTEST_GUILESS_MAIN(ApplicationLogTest)

#include "tst_ApplicationLog.moc"
