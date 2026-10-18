#include "jellyfin/TokenStore.h"

#include <QScopeGuard>
#include <QTest>
#include <QUuid>

class WindowsTokenStoreTest final : public QObject {
    Q_OBJECT
  private slots:
    void roundTripAndLifetime();
};

void WindowsTokenStoreTest::roundTripAndLifetime() {
    QString const entry = QStringLiteral("sunplayer-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    TokenStore store(entry);
    auto cleanup = qScopeGuard([&] {
        bool finished = false;
        QString failure;
        store.remove([&](QString error) {
            failure = error;
            finished = true;
        });
        if (!QTest::qWaitFor([&] { return finished; }, 5000) || !failure.isEmpty()) {
            qFatal("Could not clean up the isolated token-store test entry: %s", qPrintable(failure));
        }
    });

    bool finished = false;
    QString token;
    QString failure;
    auto read = [&](TokenStore& source) {
        finished = false;
        source.read([&](QString value, QString error) {
            token = value;
            failure = error;
            finished = true;
        });
    };
    auto complete = [&](QString error) {
        failure = error;
        finished = true;
    };

    read(store);
    QVERIFY(!finished);
    QTRY_VERIFY(finished);
    QVERIFY(token.isEmpty());
    QVERIFY(failure.contains(QStringLiteral("0x80070490")));

    finished = false;
    store.write(QStringLiteral("first-test-token"), complete);
    QVERIFY(!finished);
    QTRY_VERIFY(finished);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    TokenStore reopened(entry);
    read(reopened);
    QTRY_VERIFY(finished);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(token, QStringLiteral("first-test-token"));

    finished = false;
    store.write(QStringLiteral("replacement-test-token"), complete);
    QTRY_VERIFY(finished);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    read(reopened);
    QTRY_VERIFY(finished);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(token, QStringLiteral("replacement-test-token"));

    for (int attempt = 0; attempt < 2; ++attempt) {
        finished = false;
        store.remove(complete);
        QTRY_VERIFY(finished);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }
    read(reopened);
    QTRY_VERIFY(finished);
    QVERIFY(token.isEmpty());
    QVERIFY(failure.contains(QStringLiteral("0x80070490")));

    bool cancelledCallback = false;
    auto* cancelled = new TokenStore(entry);
    cancelled->write(QStringLiteral("cancelled-test-token"), [&](QString) { cancelledCallback = true; });
    delete cancelled;
    read(reopened);
    QTRY_VERIFY(finished);
    QVERIFY(!cancelledCallback);
    QVERIFY(token.isEmpty());
    QVERIFY(failure.contains(QStringLiteral("0x80070490")));
}

QTEST_GUILESS_MAIN(WindowsTokenStoreTest)
#include "tst_WindowsTokenStore.moc"
