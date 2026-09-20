#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

#include "app/SettingsDialog.h"
#include "subtitles/SubtitleSettings.h"
#include "video/DecodedVideoSource.h"

class SettingsDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void selectsRequestedPageAndMirrorsPlayback();
    void editsHdrReferenceWhite();
    void absolutePqAvailabilityPreservesPreference();
    void editsSubtitleSettingsLive();
    void previewsAndRollsBackTextColor();
    void appliesPresetsAndRestoresDefaults();
};

void SettingsDialogTest::selectsRequestedPageAndMirrorsPlayback() {
    SubtitleSettings settings;
    SettingsDialog dialog(settings);
    auto* const tabs = dialog.findChild<QTabWidget*>(QStringLiteral("settingsTabs"));
    auto* const volume = dialog.findChild<QSlider*>(QStringLiteral("settingsVolumeSlider"));
    auto* const blanking = dialog.findChild<QCheckBox*>(QStringLiteral("settingsBlankOtherDisplays"));
    QVERIFY(tabs);
    QVERIFY(volume);
    QVERIFY(blanking);
    QCOMPARE(dialog.windowModality(), Qt::WindowModal);

    dialog.showPage(SettingsDialog::SubtitlesPage);
    QCOMPARE(tabs->currentIndex(), SettingsDialog::SubtitlesPage);
    dialog.showPage(99);
    QCOMPARE(tabs->currentIndex(), SettingsDialog::PlaybackPage);

    QSignalSpy volumeEdits(&dialog, &SettingsDialog::volumeEdited);
    QSignalSpy blankingEdits(&dialog, &SettingsDialog::blankOtherDisplaysEdited);
    auto* const hdr10Plus = dialog.findChild<QCheckBox*>(QStringLiteral("settingsPreferHdr10Plus"));
    QVERIFY(hdr10Plus);
    QSignalSpy preferenceEdits(&dialog, &SettingsDialog::preferHdr10PlusEdited);
    dialog.setPlaybackState({.volume = 0.35, .blankingAvailable = true, .blankingEnabled = true});
    QVERIFY(!hdr10Plus->isChecked());
    QCOMPARE(preferenceEdits.count(), 0);
    hdr10Plus->setChecked(true);
    QCOMPARE(preferenceEdits.count(), 1);
    QCOMPARE(preferenceEdits.takeFirst().at(0).toBool(), true);
    QCOMPARE(volume->value(), 35);
    QVERIFY(!blanking->isHidden());
    QVERIFY(blanking->isChecked());
    QCOMPARE(volumeEdits.count(), 0);
    QCOMPARE(blankingEdits.count(), 0);

    volume->setValue(42);
    QCOMPARE(volumeEdits.count(), 1);
    QCOMPARE(volumeEdits.takeFirst().at(0).toReal(), 0.42);
    blanking->setChecked(false);
    QCOMPARE(blankingEdits.count(), 1);
    QCOMPARE(blankingEdits.takeFirst().at(0).toBool(), false);
}

void SettingsDialogTest::editsHdrReferenceWhite() {
    SubtitleSettings settings;
    SettingsDialog dialog(settings);
    auto* const slider = dialog.findChild<QSlider*>(QStringLiteral("settingsSourceHdrReferenceWhiteSlider"));
    auto* const spin = dialog.findChild<QSpinBox*>(QStringLiteral("settingsSourceHdrReferenceWhiteSpin"));
    auto* const low = dialog.findChild<QPushButton*>(QStringLiteral("settingsSourceHdrReferenceWhitePreset100"));
    auto* const high = dialog.findChild<QPushButton*>(QStringLiteral("settingsSourceHdrReferenceWhitePreset203"));
    QVERIFY(slider);
    QVERIFY(spin);
    QVERIFY(low);
    QVERIFY(high);
    QCOMPARE(slider->minimum(), DecodedVideoSource::minimumSourceHdrReferenceWhiteNits);
    QCOMPARE(slider->maximum(), DecodedVideoSource::maximumSourceHdrReferenceWhiteNits);
    QCOMPARE(spin->minimum(), slider->minimum());
    QCOMPARE(spin->maximum(), slider->maximum());
    QCOMPARE(slider->value(), DecodedVideoSource::defaultSourceHdrReferenceWhiteNits);
    QCOMPARE(spin->value(), slider->value());
    QSignalSpy edits(&dialog, &SettingsDialog::sourceHdrReferenceWhiteNitsEdited);
    auto const subtitleBrightness = settings.brightness();
    slider->setValue(145);
    QCOMPARE(spin->value(), 145);
    QCOMPARE(edits.count(), 1);
    QCOMPARE(edits.takeFirst().at(0).toInt(), 145);
    spin->setValue(170);
    QCOMPARE(slider->value(), 170);
    QCOMPARE(edits.count(), 1);
    QCOMPARE(edits.takeFirst().at(0).toInt(), 170);
    low->click();
    QCOMPARE(slider->value(), DecodedVideoSource::minimumSourceHdrReferenceWhiteNits);
    QCOMPARE(spin->value(), slider->value());
    QCOMPARE(edits.count(), 1);
    edits.clear();
    high->click();
    QCOMPARE(slider->value(), DecodedVideoSource::maximumSourceHdrReferenceWhiteNits);
    QCOMPARE(spin->value(), slider->value());
    QCOMPARE(edits.count(), 1);
    edits.clear();
    dialog.setPlaybackState({.volume = 0.5, .sourceHdrReferenceWhiteNits = 150});
    QCOMPARE(slider->value(), 150);
    QCOMPARE(spin->value(), 150);
    QCOMPARE(edits.count(), 0);
    QCOMPARE(settings.brightness(), subtitleBrightness);
}

void SettingsDialogTest::absolutePqAvailabilityPreservesPreference() {
    SubtitleSettings settings;
    SettingsDialog dialog(settings);
    auto* const absolute = dialog.findChild<QCheckBox*>(QStringLiteral("settingsAbsolutePq"));
    auto* const status = dialog.findChild<QLabel*>(QStringLiteral("settingsAbsolutePqStatus"));
    auto* const slider = dialog.findChild<QSlider*>(QStringLiteral("settingsSourceHdrReferenceWhiteSlider"));
    auto* const spin = dialog.findChild<QSpinBox*>(QStringLiteral("settingsSourceHdrReferenceWhiteSpin"));
    auto* const low = dialog.findChild<QPushButton*>(QStringLiteral("settingsSourceHdrReferenceWhitePreset100"));
    auto* const high = dialog.findChild<QPushButton*>(QStringLiteral("settingsSourceHdrReferenceWhitePreset203"));
    QVERIFY(absolute && status && slider && spin && low && high);
    QVERIFY(!absolute->isChecked());
    QVERIFY(!absolute->isEnabled());
    QSignalSpy edits(&dialog, &SettingsDialog::absolutePqEnabledEdited);
    QSignalSpy whiteEdits(&dialog, &SettingsDialog::sourceHdrReferenceWhiteNitsEdited);
    SettingsDialog::PlaybackState state{.sourceHdrReferenceWhiteNits = 150};
    for (bool selected : {false, true}) {
        state.absolutePqEnabled = selected;
        for (bool available : {true, false, true}) {
            state.absolutePqAvailable = available;
            dialog.setPlaybackState(state);
            QCOMPARE(absolute->isChecked(), selected);
            QCOMPARE(absolute->isEnabled(), available);
            QCOMPARE(status->isHidden(), !(selected && !available));
            bool const adaptive = !(selected && available);
            QCOMPARE(slider->isEnabled(), adaptive);
            QCOMPARE(spin->isEnabled(), adaptive);
            QCOMPARE(low->isEnabled(), adaptive);
            QCOMPARE(high->isEnabled(), adaptive);
            QCOMPARE(slider->value(), 150);
            QCOMPARE(spin->value(), 150);
        }
    }
    QCOMPARE(edits.count(), 0);
    QCOMPARE(whiteEdits.count(), 0);
    state.absolutePqAvailable = false;
    dialog.setPlaybackState(state);
    absolute->click();
    QCOMPARE(edits.count(), 0);
    QVERIFY(absolute->isChecked());
    state.absolutePqAvailable = true;
    dialog.setPlaybackState(state);
    QVERIFY(absolute->isChecked());
    QVERIFY(!slider->isEnabled());
    absolute->click();
    QCOMPARE(edits.count(), 1);
    QCOMPARE(edits.takeFirst().at(0).toBool(), false);
}

void SettingsDialogTest::editsSubtitleSettingsLive() {
    SubtitleSettings settings;
    SettingsDialog dialog(settings);
    auto* const appearance = dialog.findChild<QComboBox*>(QStringLiteral("subtitleAppearanceMode"));
    auto* const textOpacity = dialog.findChild<QSpinBox*>(QStringLiteral("subtitleTextOpacity"));
    auto* const backgroundOpacity = dialog.findChild<QSpinBox*>(QStringLiteral("subtitleBackgroundOpacity"));
    auto* const edgeStyle = dialog.findChild<QComboBox*>(QStringLiteral("subtitleEdgeStyle"));
    auto* const edgeOpacity = dialog.findChild<QSpinBox*>(QStringLiteral("subtitleEdgeOpacity"));
    auto* const sizeMode = dialog.findChild<QComboBox*>(QStringLiteral("subtitleSizeMode"));
    auto* const scale = dialog.findChild<QSpinBox*>(QStringLiteral("subtitleScale"));
    auto* const positionMode = dialog.findChild<QComboBox*>(QStringLiteral("subtitlePositionMode"));
    auto* const positionSlider = dialog.findChild<QSlider*>(QStringLiteral("subtitleVerticalPosition"));
    auto* const position = dialog.findChild<QSpinBox*>(QStringLiteral("subtitleVerticalPositionSpin"));
    auto* const overallOpacity = dialog.findChild<QSpinBox*>(QStringLiteral("subtitleOverallOpacitySpin"));
    auto* const brightness = dialog.findChild<QSlider*>(QStringLiteral("subtitleBrightness"));
    auto* const brightnessSpin = dialog.findChild<QSpinBox*>(QStringLiteral("subtitleBrightnessSpin"));
    QVERIFY(brightness);
    QVERIFY(brightnessSpin);
    QCOMPARE(brightness->minimum(), 0);
    QCOMPARE(brightness->maximum(), 100);
    QCOMPARE(brightness->value(), 80);
    QCOMPARE(brightnessSpin->value(), 80);
    auto const rasterRevision = settings.rasterRevision();
    brightness->setValue(25);
    QCOMPARE(settings.brightness(), 0.25);
    QCOMPARE(brightnessSpin->value(), 25);
    brightnessSpin->setValue(50);
    QCOMPARE(settings.brightness(), 0.5);
    QCOMPARE(brightness->value(), 50);
    QCOMPARE(settings.rasterRevision(), rasterRevision);
    auto* const warning = dialog.findChild<QLabel*>(QStringLiteral("subtitleWarning"));
    QVERIFY(appearance);
    QVERIFY(textOpacity);
    QVERIFY(backgroundOpacity);
    QVERIFY(edgeStyle);
    QVERIFY(edgeOpacity);
    QVERIFY(sizeMode);
    QVERIFY(scale);
    QVERIFY(positionMode);
    QVERIFY(positionSlider);
    QVERIFY(position);
    QVERIFY(overallOpacity);
    QVERIFY(warning);

    appearance->setCurrentIndex(1);
    textOpacity->setValue(40);
    backgroundOpacity->setValue(60);
    edgeStyle->setCurrentIndex(2);
    edgeOpacity->setValue(70);
    sizeMode->setCurrentIndex(1);
    scale->setValue(180);
    positionMode->setCurrentIndex(1);
    QCOMPARE(positionSlider->singleStep(), 10);
    QCOMPARE(position->singleStep(), 10);
    position->setValue(75);
    overallOpacity->setValue(0);

    QCOMPARE(settings.appearanceMode(), SubtitleSettings::CustomAppearance);
    QCOMPARE(settings.textOpacity(), 0.4);
    QCOMPARE(settings.backgroundOpacity(), 0.6);
    QCOMPARE(settings.edgeStyle(), SubtitleSettings::Shadow);
    QCOMPARE(settings.edgeOpacity(), 0.7);
    QCOMPARE(settings.sizeMode(), SubtitleSettings::CustomSize);
    QCOMPARE(settings.scale(), 1.8);
    QCOMPARE(settings.positionMode(), SubtitleSettings::CustomPosition);
    QCOMPARE(settings.verticalPosition(), 0.75);
    QCOMPARE(settings.overallOpacity(), 0.0);
    QVERIFY(!warning->isHidden());
    QCOMPARE(warning->text(), QStringLiteral("Subtitles are hidden by overall opacity."));
}

void SettingsDialogTest::previewsAndRollsBackTextColor() {
    SubtitleSettings settings;
    settings.setAppearanceMode(SubtitleSettings::CustomAppearance);
    SettingsDialog dialog(settings);
    auto* const textColor = dialog.findChild<QPushButton*>(QStringLiteral("subtitleTextColor"));
    QVERIFY(textColor);

    QColor const original = settings.textColor();
    QColor const preview(QStringLiteral("#123456"));
    QTimer::singleShot(0, &dialog, [&] {
        auto* const picker = dialog.findChild<QColorDialog*>();
        QVERIFY(picker);
        if (!picker) {
            return;
        }
        picker->setCurrentColor(preview);
        QCOMPARE(settings.textColor(), preview);
        picker->reject();
    });
    textColor->click();
    QCOMPARE(settings.textColor(), original);

    QColor const accepted(QStringLiteral("#ABCDEF"));
    QTimer::singleShot(0, &dialog, [&] {
        auto* const picker = dialog.findChild<QColorDialog*>();
        QVERIFY(picker);
        if (!picker) {
            return;
        }
        picker->setCurrentColor(accepted);
        QCOMPARE(settings.textColor(), accepted);
        picker->accept();
    });
    textColor->click();
    QCOMPARE(settings.textColor(), accepted);
}

void SettingsDialogTest::appliesPresetsAndRestoresDefaults() {
    SubtitleSettings settings;
    SettingsDialog dialog(settings);
    auto* const contrast = dialog.findChild<QPushButton*>(QStringLiteral("subtitleHighContrastPreset"));
    auto* const restore = dialog.findChild<QPushButton*>(QStringLiteral("subtitleRestoreDefaults"));
    QVERIFY(contrast);
    QVERIFY(restore);

    contrast->click();
    QCOMPARE(settings.appearanceMode(), SubtitleSettings::CustomAppearance);
    QVERIFY(settings.backgroundEnabled());
    QCOMPARE(settings.backgroundColor(), QColor(Qt::black));

    restore->click();
    QCOMPARE(settings.appearanceMode(), SubtitleSettings::AuthoredAppearance);
    QCOMPARE(settings.sizeMode(), SubtitleSettings::AuthoredSize);
    QCOMPARE(settings.overallOpacity(), 1.0);
}

QTEST_MAIN(SettingsDialogTest)
#include "tst_SettingsDialog.moc"
