#include <QtTest>

#include "audio/AudioOutputLedger.h"

class AudioOutputLedgerTest final : public QObject {
    Q_OBJECT

  private slots:
    void freezesMediaAcrossHoldSilence();
    void reportsWhenAnOldMappingWasOverwritten();
};

void AudioOutputLedgerTest::freezesMediaAcrossHoldSilence() {
    AudioOutputLedger ledger(8);
    ledger.record(100, 20);
    ledger.record(50, 0);

    QVERIFY(ledger.positionForOutputFrame(0) == std::optional(AudioOutputPosition{0, false}));
    QVERIFY(ledger.positionForOutputFrame(50) == std::optional(AudioOutputPosition{50, false}));
    QVERIFY(ledger.positionForOutputFrame(100) == std::optional(AudioOutputPosition{100, true}));
    QVERIFY(ledger.positionForOutputFrame(110) == std::optional(AudioOutputPosition{100, true}));
    QVERIFY(ledger.positionForOutputFrame(120) == std::optional(AudioOutputPosition{100, false}));
    QVERIFY(ledger.positionForOutputFrame(130) == std::optional(AudioOutputPosition{110, false}));
    QVERIFY(ledger.positionForOutputFrame(170) == std::optional(AudioOutputPosition{150, false}));
    QCOMPARE(ledger.outputFrames(), 170U);
    QCOMPARE(ledger.mediaFrames(), 150U);
}

void AudioOutputLedgerTest::reportsWhenAnOldMappingWasOverwritten() {
    AudioOutputLedger ledger(2);
    ledger.record(10, 0);
    ledger.record(10, 0);
    ledger.record(10, 0);

    QVERIFY(!ledger.positionForOutputFrame(5));
    QVERIFY(ledger.positionForOutputFrame(15) == std::optional(AudioOutputPosition{15, false}));
    QVERIFY(ledger.positionForOutputFrame(25) == std::optional(AudioOutputPosition{25, false}));
    QVERIFY(!ledger.positionForOutputFrame(31));

    ledger.reset();
    QVERIFY(ledger.positionForOutputFrame(0) == std::optional(AudioOutputPosition{0, false}));
    QVERIFY(!ledger.positionForOutputFrame(1));
}

QTEST_APPLESS_MAIN(AudioOutputLedgerTest)
#include "tst_AudioOutputLedger.moc"
