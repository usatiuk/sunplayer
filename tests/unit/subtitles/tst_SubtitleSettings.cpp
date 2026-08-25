#include <QSignalSpy>
#include <QTest>

#include "subtitles/SubtitleSettings.h"

class SubtitleSettingsTest final : public QObject {
    Q_OBJECT

  private slots:
    void settersReportPreciseDirtyFields();
    void overallOpacityDoesNotInvalidateRaster();
    void presetsAreAtomic();
    void restoreDefaultsRemovesPersistenceEvenWhenUnchanged();
};

void SubtitleSettingsTest::settersReportPreciseDirtyFields() {
    SubtitleSettings settings;
    SubtitleAppearanceFields dirtyFields = 0;
    int persistenceCount = 0;
    connect(&settings, &SubtitleSettings::persistenceChanged, this,
            [&dirtyFields, &persistenceCount](SubtitleAppearanceFields fields) {
                dirtyFields = fields;
                ++persistenceCount;
            });

    settings.setTextColor(QColor(QStringLiteral("#12AB34")));
    QCOMPARE(persistenceCount, 1);
    QCOMPARE(dirtyFields, SubtitleAppearanceField::TextColor);
    QCOMPARE(settings.textColor(), QColor(QStringLiteral("#12AB34")));

    settings.setScale(2.0);
    QCOMPARE(persistenceCount, 2);
    QCOMPARE(dirtyFields, SubtitleAppearanceField::Scale);
    QCOMPARE(settings.scale(), 2.0);

    settings.setScale(2.01);
    QCOMPARE(persistenceCount, 2);
    QCOMPARE(settings.scale(), 2.0);
}

void SubtitleSettingsTest::overallOpacityDoesNotInvalidateRaster() {
    SubtitleSettings settings;
    qulonglong const initialRevision = settings.rasterRevision();
    settings.setOverallOpacity(0.25);
    QCOMPARE(settings.rasterRevision(), initialRevision);

    settings.setTextOpacity(0.5);
    QVERIFY(settings.rasterRevision() > initialRevision);
}

void SubtitleSettingsTest::presetsAreAtomic() {
    SubtitleSettings settings;
    QSignalSpy settingsChanges(&settings, &SubtitleSettings::settingsChanged);
    SubtitleAppearanceFields dirtyFields = 0;
    int persistenceCount = 0;
    connect(&settings, &SubtitleSettings::persistenceChanged, this,
            [&dirtyFields, &persistenceCount](SubtitleAppearanceFields fields) {
                dirtyFields = fields;
                ++persistenceCount;
            });

    settings.applyHighContrast();
    QCOMPARE(settingsChanges.count(), 1);
    QCOMPARE(persistenceCount, 1);
    QVERIFY((dirtyFields & SubtitleAppearanceField::AppearanceMode) != 0);
    QCOMPARE(settings.appearanceMode(), SubtitleSettings::CustomAppearance);
    QCOMPARE(settings.backgroundOpacity(), 0.8);

    settingsChanges.clear();
    persistenceCount = 0;
    settings.applyLargeText();
    QCOMPARE(settingsChanges.count(), 1);
    QCOMPARE(persistenceCount, 1);
    QCOMPARE(settings.sizeMode(), SubtitleSettings::CustomSize);
    QCOMPARE(settings.scale(), 1.5);
}

void SubtitleSettingsTest::restoreDefaultsRemovesPersistenceEvenWhenUnchanged() {
    SubtitleSettings settings;
    QSignalSpy resets(&settings, &SubtitleSettings::persistenceResetRequested);
    QSignalSpy settingsChanges(&settings, &SubtitleSettings::settingsChanged);
    int persistenceCount = 0;
    connect(&settings, &SubtitleSettings::persistenceChanged, this,
            [&persistenceCount](SubtitleAppearanceFields) { ++persistenceCount; });

    settings.restoreDefaults();
    QCOMPARE(resets.count(), 1);
    QCOMPARE(settingsChanges.count(), 0);
    QCOMPARE(persistenceCount, 0);

    settings.setTextOpacity(0.25);
    persistenceCount = 0;
    qulonglong const changedRevision = settings.rasterRevision();
    settings.restoreDefaults();
    QCOMPARE(resets.count(), 2);
    QCOMPARE(settingsChanges.count(), 2);
    QCOMPARE(persistenceCount, 0);
    QVERIFY(settings.rasterRevision() > changedRevision);
}

QTEST_MAIN(SubtitleSettingsTest)

#include "tst_SubtitleSettings.moc"
