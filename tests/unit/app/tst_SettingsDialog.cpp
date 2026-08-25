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

class SettingsDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void selectsRequestedPageAndMirrorsPlayback();
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
    dialog.setPlaybackState(0.35, true, true);
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
