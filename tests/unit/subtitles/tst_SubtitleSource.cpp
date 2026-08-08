#include <QtTest>

#include "subtitles/SubtitleSource.h"

namespace {
SubtitleStreamConfiguration configuration(std::uint64_t generation) {
    return {
        .playbackGeneration = generation,
        .streamIndex = 2,
        .codec = QStringLiteral("ass"),
    };
}

SubtitleEvent textEvent(std::uint64_t generation, std::int64_t start) {
    return {
        .playbackGeneration = generation,
        .startMicroseconds = start,
        .endMicroseconds = start + 1'000'000,
        .type = SubtitlePayloadType::AssText,
        .ass = QByteArrayLiteral("0,0,Default,,0,0,0,,Subtitle"),
    };
}
} // namespace

class SubtitleSourceTest final : public QObject {
    Q_OBJECT

  private slots:
    void publishesImmutableCurrentGenerationState();
    void generationReplacementRejectsStaleEvents();
    void failureIsGenerationScopedAndNonDestructive();
};

void SubtitleSourceTest::publishesImmutableCurrentGenerationState() {
    SubtitleSource source;
    source.reset(4);
    source.configure(configuration(4));
    SubtitleStateSnapshot const before = source.snapshot();
    QVERIFY(before.isEnabled());
    QVERIFY(before.events->empty());

    QVERIFY(source.append(textEvent(4, 500'000)));
    SubtitleStateSnapshot const after = source.snapshot();
    QCOMPARE(after.events->size(), 1U);
    QVERIFY(after.revision > before.revision);
    QVERIFY(before.events->empty());
}

void SubtitleSourceTest::generationReplacementRejectsStaleEvents() {
    SubtitleSource source;
    source.reset(7);
    source.configure(configuration(7));
    QVERIFY(source.append(textEvent(7, 0)));

    source.reset(8);
    QVERIFY(!source.append(textEvent(7, 1'000'000)));
    source.configure(configuration(8));
    QVERIFY(source.append(textEvent(8, 2'000'000)));
    SubtitleStateSnapshot const state = source.snapshot();
    QCOMPARE(state.playbackGeneration, 8U);
    QCOMPARE(state.events->size(), 1U);
    QCOMPARE(state.events->front().startMicroseconds, 2'000'000);
}

void SubtitleSourceTest::failureIsGenerationScopedAndNonDestructive() {
    SubtitleSource source;
    source.reset(10);
    source.configure(configuration(10));
    source.fail(9, QStringLiteral("stale"));
    QVERIFY(source.snapshot().error.isEmpty());

    source.fail(10, QStringLiteral("malformed subtitle"));
    source.fail(10, QStringLiteral("generic downstream rejection"));
    SubtitleStateSnapshot const failed = source.snapshot();
    QCOMPARE(failed.error, QStringLiteral("malformed subtitle"));
    QVERIFY(!failed.isEnabled());
    QVERIFY(!source.append(textEvent(10, 0)));
}

QTEST_APPLESS_MAIN(SubtitleSourceTest)
#include "tst_SubtitleSource.moc"
