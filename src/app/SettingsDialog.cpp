#include "app/SettingsDialog.h"

#include <vector>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpacerItem>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "subtitles/SubtitleSettings.h"

namespace {
QSpinBox* percentageSpin(QWidget* parent) {
    auto* const spin = new QSpinBox(parent);
    spin->setRange(0, 100);
    spin->setSuffix(QStringLiteral(" %"));
    spin->setAccelerated(true);
    return spin;
}

QWidget* sliderRow(QSlider*& slider, QSpinBox*& spin, QWidget* parent) {
    auto* const row = new QWidget(parent);
    auto* const layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(0, 100);
    spin = percentageSpin(row);
    layout->addWidget(slider, 1);
    layout->addWidget(spin);
    return row;
}
} // namespace

SettingsDialog::SettingsDialog(SubtitleSettings& subtitleSettings) : m_subtitleSettings(subtitleSettings) {
    setWindowTitle(tr("Settings"));
    setWindowIcon(QGuiApplication::windowIcon());
    setWindowModality(Qt::WindowModal);
    resize(760, 640);

    auto* const rootLayout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));
    rootLayout->addWidget(m_tabs, 1);

    auto* const playbackPage = new QWidget(m_tabs);
    auto* const playbackLayout = new QFormLayout(playbackPage);
    playbackLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_volumeSlider = new QSlider(Qt::Horizontal, playbackPage);
    m_volumeSlider->setObjectName(QStringLiteral("settingsVolumeSlider"));
    m_volumeSlider->setAccessibleName(tr("Volume slider"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSpin = percentageSpin(playbackPage);
    m_volumeSpin->setObjectName(QStringLiteral("settingsVolumeSpin"));
    m_volumeSpin->setAccessibleName(tr("Volume percentage"));
    auto* const volumeRow = new QWidget(playbackPage);
    auto* const volumeLayout = new QHBoxLayout(volumeRow);
    volumeLayout->setContentsMargins(0, 0, 0, 0);
    volumeLayout->addWidget(m_volumeSlider, 1);
    volumeLayout->addWidget(m_volumeSpin);
    playbackLayout->addRow(tr("Volume:"), volumeRow);
    m_blankOtherDisplays = new QCheckBox(tr("Blank other displays in fullscreen"), playbackPage);
    m_blankOtherDisplays->setObjectName(QStringLiteral("settingsBlankOtherDisplays"));
    playbackLayout->addRow(QString(), m_blankOtherDisplays);
    playbackLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    auto* const playbackScroll = new QScrollArea(m_tabs);
    playbackScroll->setFrameShape(QFrame::NoFrame);
    playbackScroll->setWidgetResizable(true);
    playbackScroll->setWidget(playbackPage);
    m_tabs->addTab(playbackScroll, tr("Playback"));

    auto* const subtitlesPage = new QWidget(m_tabs);
    auto* const subtitlesLayout = new QVBoxLayout(subtitlesPage);

    auto* const presets = new QGroupBox(tr("Presets"), subtitlesPage);
    auto* const presetLayout = new QHBoxLayout(presets);
    auto* const authoredPreset = new QPushButton(tr("As authored"), presets);
    authoredPreset->setObjectName(QStringLiteral("subtitleAuthoredPreset"));
    auto* const contrastPreset = new QPushButton(tr("High contrast"), presets);
    contrastPreset->setObjectName(QStringLiteral("subtitleHighContrastPreset"));
    auto* const largePreset = new QPushButton(tr("Large text"), presets);
    largePreset->setObjectName(QStringLiteral("subtitleLargeTextPreset"));
    presetLayout->addWidget(authoredPreset);
    presetLayout->addWidget(contrastPreset);
    presetLayout->addWidget(largePreset);
    presetLayout->addStretch();
    subtitlesLayout->addWidget(presets);

    auto* const appearance = new QGroupBox(tr("Text and background"), subtitlesPage);
    auto* const appearanceLayout = new QVBoxLayout(appearance);
    auto* const modeLayout = new QFormLayout;
    m_appearanceMode = new QComboBox(appearance);
    m_appearanceMode->setObjectName(QStringLiteral("subtitleAppearanceMode"));
    m_appearanceMode->setAccessibleName(tr("Subtitle appearance"));
    m_appearanceMode->addItems({tr("As authored"), tr("Custom")});
    modeLayout->addRow(tr("Appearance:"), m_appearanceMode);
    appearanceLayout->addLayout(modeLayout);

    m_customAppearanceControls = new QWidget(appearance);
    auto* const customLayout = new QFormLayout(m_customAppearanceControls);
    customLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* const textRow = new QWidget(m_customAppearanceControls);
    auto* const textLayout = new QHBoxLayout(textRow);
    textLayout->setContentsMargins(0, 0, 0, 0);
    m_textColor = new QPushButton(textRow);
    m_textColor->setObjectName(QStringLiteral("subtitleTextColor"));
    m_textColor->setAccessibleName(tr("Subtitle text color"));
    m_textOpacity = percentageSpin(textRow);
    m_textOpacity->setObjectName(QStringLiteral("subtitleTextOpacity"));
    m_textOpacity->setAccessibleName(tr("Subtitle text opacity"));
    auto* const whiteText = new QPushButton(tr("White"), textRow);
    whiteText->setAccessibleName(tr("Set subtitle text to white"));
    auto* const yellowText = new QPushButton(tr("Yellow"), textRow);
    yellowText->setAccessibleName(tr("Set subtitle text to yellow"));
    textLayout->addWidget(m_textColor);
    textLayout->addWidget(m_textOpacity);
    textLayout->addWidget(whiteText);
    textLayout->addWidget(yellowText);
    textLayout->addStretch();
    customLayout->addRow(tr("Text:"), textRow);

    auto* const backgroundRow = new QWidget(m_customAppearanceControls);
    auto* const backgroundLayout = new QHBoxLayout(backgroundRow);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    m_backgroundEnabled = new QCheckBox(tr("Enabled"), backgroundRow);
    m_backgroundEnabled->setObjectName(QStringLiteral("subtitleBackgroundEnabled"));
    m_backgroundEnabled->setAccessibleName(tr("Enable subtitle background"));
    m_backgroundColor = new QPushButton(backgroundRow);
    m_backgroundColor->setObjectName(QStringLiteral("subtitleBackgroundColor"));
    m_backgroundColor->setAccessibleName(tr("Subtitle background color"));
    m_backgroundOpacity = percentageSpin(backgroundRow);
    m_backgroundOpacity->setObjectName(QStringLiteral("subtitleBackgroundOpacity"));
    m_backgroundOpacity->setAccessibleName(tr("Subtitle background opacity"));
    m_backgroundPreset = new QComboBox(backgroundRow);
    m_backgroundPreset->setObjectName(QStringLiteral("subtitleBackgroundPreset"));
    m_backgroundPreset->setAccessibleName(tr("Subtitle background preset"));
    m_backgroundPreset->setPlaceholderText(tr("Custom values"));
    m_backgroundPreset->addItems({tr("None"), tr("Subtle black"), tr("Strong black")});
    m_backgroundPreset->setCurrentIndex(-1);
    backgroundLayout->addWidget(m_backgroundEnabled);
    backgroundLayout->addWidget(m_backgroundColor);
    backgroundLayout->addWidget(m_backgroundOpacity);
    backgroundLayout->addWidget(m_backgroundPreset);
    backgroundLayout->addStretch();
    customLayout->addRow(tr("Background:"), backgroundRow);

    auto* const edgeRow = new QWidget(m_customAppearanceControls);
    auto* const edgeLayout = new QHBoxLayout(edgeRow);
    edgeLayout->setContentsMargins(0, 0, 0, 0);
    m_edgeStyle = new QComboBox(edgeRow);
    m_edgeStyle->setObjectName(QStringLiteral("subtitleEdgeStyle"));
    m_edgeStyle->setAccessibleName(tr("Subtitle edge style"));
    m_edgeStyle->addItems({tr("None"), tr("Outline"), tr("Shadow")});
    m_edgeColor = new QPushButton(edgeRow);
    m_edgeColor->setObjectName(QStringLiteral("subtitleEdgeColor"));
    m_edgeColor->setAccessibleName(tr("Subtitle edge color"));
    m_edgeOpacity = percentageSpin(edgeRow);
    m_edgeOpacity->setObjectName(QStringLiteral("subtitleEdgeOpacity"));
    m_edgeOpacity->setAccessibleName(tr("Subtitle edge opacity"));
    edgeLayout->addWidget(m_edgeStyle);
    edgeLayout->addWidget(m_edgeColor);
    edgeLayout->addWidget(m_edgeOpacity);
    edgeLayout->addStretch();
    customLayout->addRow(tr("Edge:"), edgeRow);
    appearanceLayout->addWidget(m_customAppearanceControls);
    subtitlesLayout->addWidget(appearance);

    auto* const geometry = new QGroupBox(tr("Size and position"), subtitlesPage);
    auto* const geometryLayout = new QFormLayout(geometry);
    m_sizeMode = new QComboBox(geometry);
    m_sizeMode->setObjectName(QStringLiteral("subtitleSizeMode"));
    m_sizeMode->setAccessibleName(tr("Subtitle size mode"));
    m_sizeMode->addItems({tr("As authored"), tr("Custom")});
    m_scale = new QSpinBox(geometry);
    m_scale->setObjectName(QStringLiteral("subtitleScale"));
    m_scale->setAccessibleName(tr("Subtitle scale"));
    m_scale->setRange(50, 200);
    m_scale->setSingleStep(10);
    m_scale->setSuffix(QStringLiteral(" %"));
    auto* const sizeRow = new QWidget(geometry);
    auto* const sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->addWidget(m_sizeMode);
    sizeLayout->addWidget(m_scale);
    sizeLayout->addStretch();
    geometryLayout->addRow(tr("Size:"), sizeRow);

    m_positionMode = new QComboBox(geometry);
    m_positionMode->setObjectName(QStringLiteral("subtitlePositionMode"));
    m_positionMode->setAccessibleName(tr("Subtitle position mode"));
    m_positionMode->addItems({tr("As authored"), tr("Custom")});
    auto* const positionControl = sliderRow(m_verticalPosition, m_verticalPositionSpin, geometry);
    m_verticalPosition->setSingleStep(10);
    m_verticalPositionSpin->setSingleStep(10);
    m_verticalPosition->setObjectName(QStringLiteral("subtitleVerticalPosition"));
    m_verticalPosition->setAccessibleName(tr("Subtitle vertical position slider"));
    m_verticalPositionSpin->setObjectName(QStringLiteral("subtitleVerticalPositionSpin"));
    m_verticalPositionSpin->setAccessibleName(tr("Subtitle vertical position percentage"));
    auto* const positionRow = new QWidget(geometry);
    auto* const positionLayout = new QHBoxLayout(positionRow);
    positionLayout->setContentsMargins(0, 0, 0, 0);
    positionLayout->addWidget(m_positionMode);
    positionLayout->addWidget(positionControl, 1);
    geometryLayout->addRow(tr("Vertical position:"), positionRow);
    auto* const positionHint =
        new QLabel(tr("0% is bottom; 100% is top. Explicitly positioned subtitles stay authored."), geometry);
    positionHint->setWordWrap(true);
    geometryLayout->addRow(QString(), positionHint);
    subtitlesLayout->addWidget(geometry);

    auto* const opacity = new QGroupBox(tr("Overall"), subtitlesPage);
    auto* const opacityLayout = new QFormLayout(opacity);
    opacityLayout->addRow(tr("Subtitle opacity:"), sliderRow(m_overallOpacity, m_overallOpacitySpin, opacity));
    m_overallOpacity->setObjectName(QStringLiteral("subtitleOverallOpacity"));
    m_overallOpacity->setAccessibleName(tr("Overall subtitle opacity slider"));
    m_overallOpacitySpin->setObjectName(QStringLiteral("subtitleOverallOpacitySpin"));
    m_overallOpacitySpin->setAccessibleName(tr("Overall subtitle opacity percentage"));
    subtitlesLayout->addWidget(opacity);

    m_warning = new QLabel(subtitlesPage);
    m_warning->setObjectName(QStringLiteral("subtitleWarning"));
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    subtitlesLayout->addWidget(m_warning);
    subtitlesLayout->addStretch();
    auto* const restoreDefaults = new QPushButton(tr("Restore subtitle defaults"), subtitlesPage);
    restoreDefaults->setObjectName(QStringLiteral("subtitleRestoreDefaults"));
    subtitlesLayout->addWidget(restoreDefaults, 0, Qt::AlignLeft);
    auto* const subtitlesScroll = new QScrollArea(m_tabs);
    subtitlesScroll->setFrameShape(QFrame::NoFrame);
    subtitlesScroll->setWidgetResizable(true);
    subtitlesScroll->setWidget(subtitlesPage);
    m_tabs->addTab(subtitlesScroll, tr("Subtitles"));

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) { emit volumeEdited(value / 100.0); });
    connect(m_volumeSpin, &QSpinBox::valueChanged, this, [this](int value) { emit volumeEdited(value / 100.0); });
    connect(m_blankOtherDisplays, &QCheckBox::toggled, this, &SettingsDialog::blankOtherDisplaysEdited);

    connect(authoredPreset, &QPushButton::clicked, &m_subtitleSettings, &SubtitleSettings::applyAsAuthored);
    connect(contrastPreset, &QPushButton::clicked, &m_subtitleSettings, &SubtitleSettings::applyHighContrast);
    connect(largePreset, &QPushButton::clicked, &m_subtitleSettings, &SubtitleSettings::applyLargeText);
    connect(restoreDefaults, &QPushButton::clicked, &m_subtitleSettings, &SubtitleSettings::restoreDefaults);
    connect(m_appearanceMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_subtitleSettings.setAppearanceMode(index == 1 ? SubtitleSettings::CustomAppearance
                                                        : SubtitleSettings::AuthoredAppearance);
    });
    connect(m_textColor, &QPushButton::clicked, this, &SettingsDialog::chooseTextColor);
    connect(m_textOpacity, &QSpinBox::valueChanged, this,
            [this](int value) { m_subtitleSettings.setTextOpacity(value / 100.0); });
    connect(whiteText, &QPushButton::clicked, this, [this] {
        m_subtitleSettings.setTextColor(Qt::white);
        m_subtitleSettings.setTextOpacity(1.0);
    });
    connect(yellowText, &QPushButton::clicked, this, [this] {
        m_subtitleSettings.setTextColor(Qt::yellow);
        m_subtitleSettings.setTextOpacity(1.0);
    });
    connect(m_backgroundEnabled, &QCheckBox::toggled, &m_subtitleSettings, &SubtitleSettings::setBackgroundEnabled);
    connect(m_backgroundColor, &QPushButton::clicked, this, &SettingsDialog::chooseBackgroundColor);
    connect(m_backgroundOpacity, &QSpinBox::valueChanged, this,
            [this](int value) { m_subtitleSettings.setBackgroundOpacity(value / 100.0); });
    connect(m_backgroundPreset, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == 0) {
            m_subtitleSettings.setBackgroundEnabled(false);
        } else if (index == 1 || index == 2) {
            m_subtitleSettings.setBackgroundEnabled(true);
            m_subtitleSettings.setBackgroundColor(Qt::black);
            m_subtitleSettings.setBackgroundOpacity(index == 1 ? 0.55 : 0.9);
        }
    });
    connect(m_edgeStyle, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_subtitleSettings.setEdgeStyle(static_cast<SubtitleSettings::EdgeStyle>(index)); });
    connect(m_edgeColor, &QPushButton::clicked, this, &SettingsDialog::chooseEdgeColor);
    connect(m_edgeOpacity, &QSpinBox::valueChanged, this,
            [this](int value) { m_subtitleSettings.setEdgeOpacity(value / 100.0); });
    connect(m_sizeMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_subtitleSettings.setSizeMode(index == 1 ? SubtitleSettings::CustomSize : SubtitleSettings::AuthoredSize);
    });
    connect(m_scale, &QSpinBox::valueChanged, this, [this](int value) { m_subtitleSettings.setScale(value / 100.0); });
    connect(m_positionMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_subtitleSettings.setPositionMode(index == 1 ? SubtitleSettings::CustomPosition
                                                      : SubtitleSettings::AuthoredPosition);
    });
    connect(m_verticalPosition, &QSlider::valueChanged, this,
            [this](int value) { m_subtitleSettings.setVerticalPosition(value / 100.0); });
    connect(m_verticalPositionSpin, &QSpinBox::valueChanged, this,
            [this](int value) { m_subtitleSettings.setVerticalPosition(value / 100.0); });
    connect(m_overallOpacity, &QSlider::valueChanged, this,
            [this](int value) { m_subtitleSettings.setOverallOpacity(value / 100.0); });
    connect(m_overallOpacitySpin, &QSpinBox::valueChanged, this,
            [this](int value) { m_subtitleSettings.setOverallOpacity(value / 100.0); });
    connect(&m_subtitleSettings, &SubtitleSettings::settingsChanged, this, &SettingsDialog::refreshSubtitles);

    setPlaybackState(1.0, false, false);
    refreshSubtitles();
}

void SettingsDialog::showPage(int page) {
    m_tabs->setCurrentIndex(page == SubtitlesPage ? SubtitlesPage : PlaybackPage);
}

void SettingsDialog::setPlaybackState(qreal volume, bool blankingAvailable, bool blankingEnabled) {
    QSignalBlocker const sliderBlocker(m_volumeSlider);
    QSignalBlocker const spinBlocker(m_volumeSpin);
    QSignalBlocker const blankingBlocker(m_blankOtherDisplays);
    int const volumePercent = qRound(qBound(0.0, volume, 1.0) * 100.0);
    m_volumeSlider->setValue(volumePercent);
    m_volumeSpin->setValue(volumePercent);
    m_blankOtherDisplays->setVisible(blankingAvailable);
    m_blankOtherDisplays->setChecked(blankingEnabled);
}

void SettingsDialog::refreshSubtitles() {
    QList<QObject*> const controls{
        m_appearanceMode,    m_textOpacity,        m_backgroundEnabled,
        m_backgroundOpacity, m_backgroundPreset,   m_edgeStyle,
        m_edgeOpacity,       m_sizeMode,           m_scale,
        m_positionMode,      m_verticalPosition,   m_verticalPositionSpin,
        m_overallOpacity,    m_overallOpacitySpin,
    };
    std::vector<QSignalBlocker> blockers;
    blockers.reserve(controls.size());
    for (QObject* control : controls) {
        blockers.emplace_back(control);
    }

    m_appearanceMode->setCurrentIndex(m_subtitleSettings.appearanceMode() == SubtitleSettings::CustomAppearance);
    m_customAppearanceControls->setEnabled(m_subtitleSettings.appearanceMode() == SubtitleSettings::CustomAppearance);
    updateColorButton(*m_textColor, m_subtitleSettings.textColor());
    m_textOpacity->setValue(qRound(m_subtitleSettings.textOpacity() * 100.0));
    m_backgroundEnabled->setChecked(m_subtitleSettings.backgroundEnabled());
    updateColorButton(*m_backgroundColor, m_subtitleSettings.backgroundColor());
    m_backgroundColor->setEnabled(m_subtitleSettings.backgroundEnabled());
    m_backgroundOpacity->setEnabled(m_subtitleSettings.backgroundEnabled());
    m_backgroundOpacity->setValue(qRound(m_subtitleSettings.backgroundOpacity() * 100.0));
    int backgroundPreset = -1;
    if (!m_subtitleSettings.backgroundEnabled()) {
        backgroundPreset = 0;
    } else if (m_subtitleSettings.backgroundColor() == QColor(Qt::black) &&
               qFuzzyCompare(m_subtitleSettings.backgroundOpacity(), 0.55)) {
        backgroundPreset = 1;
    } else if (m_subtitleSettings.backgroundColor() == QColor(Qt::black) &&
               qFuzzyCompare(m_subtitleSettings.backgroundOpacity(), 0.9)) {
        backgroundPreset = 2;
    }
    m_backgroundPreset->setCurrentIndex(backgroundPreset);
    m_edgeStyle->setCurrentIndex(static_cast<int>(m_subtitleSettings.edgeStyle()));
    updateColorButton(*m_edgeColor, m_subtitleSettings.edgeColor());
    bool const edgeEnabled = m_subtitleSettings.edgeStyle() != SubtitleSettings::NoEdge;
    m_edgeColor->setEnabled(edgeEnabled);
    m_edgeOpacity->setEnabled(edgeEnabled);
    m_edgeOpacity->setValue(qRound(m_subtitleSettings.edgeOpacity() * 100.0));
    m_sizeMode->setCurrentIndex(m_subtitleSettings.sizeMode() == SubtitleSettings::CustomSize);
    m_scale->setEnabled(m_subtitleSettings.sizeMode() == SubtitleSettings::CustomSize);
    m_scale->setValue(qRound(m_subtitleSettings.scale() * 100.0));
    m_positionMode->setCurrentIndex(m_subtitleSettings.positionMode() == SubtitleSettings::CustomPosition);
    bool const positionEnabled = m_subtitleSettings.positionMode() == SubtitleSettings::CustomPosition;
    m_verticalPosition->setEnabled(positionEnabled);
    m_verticalPositionSpin->setEnabled(positionEnabled);
    int const position = qRound(m_subtitleSettings.verticalPosition() * 100.0);
    m_verticalPosition->setValue(position);
    m_verticalPositionSpin->setValue(position);
    int const opacity = qRound(m_subtitleSettings.overallOpacity() * 100.0);
    m_overallOpacity->setValue(opacity);
    m_overallOpacitySpin->setValue(opacity);

    QString warning;
    if (m_subtitleSettings.overallOpacity() == 0.0) {
        warning = tr("Subtitles are hidden by overall opacity.");
    } else if (m_subtitleSettings.appearanceMode() == SubtitleSettings::CustomAppearance &&
               m_subtitleSettings.textOpacity() == 0.0 &&
               (m_subtitleSettings.edgeStyle() == SubtitleSettings::NoEdge ||
                m_subtitleSettings.edgeOpacity() == 0.0)) {
        warning = tr("Subtitle text is transparent.");
    }
    m_warning->setText(warning);
    m_warning->setVisible(!warning.isEmpty());
}

void SettingsDialog::chooseTextColor() {
    QColor const original = m_subtitleSettings.textColor();
    QColorDialog dialog(original, this);
    dialog.setWindowTitle(tr("Subtitle text color"));
    connect(&dialog, &QColorDialog::currentColorChanged, &m_subtitleSettings, &SubtitleSettings::setTextColor);
    if (dialog.exec() != QDialog::Accepted) {
        m_subtitleSettings.setTextColor(original);
    }
}

void SettingsDialog::chooseBackgroundColor() {
    QColor const original = m_subtitleSettings.backgroundColor();
    QColorDialog dialog(original, this);
    dialog.setWindowTitle(tr("Subtitle background color"));
    connect(&dialog, &QColorDialog::currentColorChanged, &m_subtitleSettings, &SubtitleSettings::setBackgroundColor);
    if (dialog.exec() != QDialog::Accepted) {
        m_subtitleSettings.setBackgroundColor(original);
    }
}

void SettingsDialog::chooseEdgeColor() {
    QColor const original = m_subtitleSettings.edgeColor();
    QColorDialog dialog(original, this);
    dialog.setWindowTitle(tr("Subtitle edge color"));
    connect(&dialog, &QColorDialog::currentColorChanged, &m_subtitleSettings, &SubtitleSettings::setEdgeColor);
    if (dialog.exec() != QDialog::Accepted) {
        m_subtitleSettings.setEdgeColor(original);
    }
}

void SettingsDialog::updateColorButton(QPushButton& button, QColor const& color) {
    QString const hex = color.name(QColor::HexRgb).toUpper();
    button.setText(hex);
    button.setAccessibleDescription(tr("Current color %1").arg(hex));
    QColor const foreground = color.lightnessF() < 0.5 ? Qt::white : Qt::black;
    button.setStyleSheet(QStringLiteral("QPushButton { background-color: %1; color: %2; }")
                             .arg(color.name(QColor::HexRgb), foreground.name(QColor::HexRgb)));
}
