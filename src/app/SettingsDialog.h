#pragma once

#include <QColor>
#include <QDialog>

#include "video/DecodedVideoSource.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QWidget;
class QVBoxLayout;
class SubtitleSettings;

class SettingsDialog final : public QDialog {
    Q_OBJECT

  public:
    enum Page {
        PlaybackPage = 0,
        SubtitlesPage = 1,
        ApplicationPage = 2,
    };

    explicit SettingsDialog(SubtitleSettings& subtitleSettings);

    struct PlaybackState {
        qreal volume = 1.0;
        bool blankingAvailable = false;
        bool blankingEnabled = false;
        bool preferHdr10Plus = DecodedVideoSource::defaultPreferHdr10Plus;
        int sourceHdrReferenceWhiteNits = DecodedVideoSource::defaultSourceHdrReferenceWhiteNits;
        bool absolutePqEnabled = false;
        bool absolutePqAvailable = false;
        VideoToneMappingSettings toneMapping;
        bool hdrPresentationActive = false;
    };

    void showPage(int page);
    void addApplicationSection(QWidget& section);
    void setJellyfinHidden(bool hidden);
    void setPlaybackState(PlaybackState const& state);

  signals:
    void volumeEdited(qreal volume);
    void preferHdr10PlusEdited(bool enabled);
    void absolutePqEnabledEdited(bool enabled);
    void toneMappingSettingsEdited(VideoToneMappingSettings const& settings);
    void sourceHdrReferenceWhiteNitsEdited(int nits);
    void blankOtherDisplaysEdited(bool enabled);
    void jellyfinHiddenEdited(bool hidden);

  private:
    void refreshSubtitles();
    void chooseTextColor();
    void chooseBackgroundColor();
    void chooseEdgeColor();
    void updateColorButton(QPushButton& button, QColor const& color);

    SubtitleSettings& m_subtitleSettings;
    QTabWidget* m_tabs = nullptr;
    QVBoxLayout* m_applicationLayout = nullptr;
    QCheckBox* m_hideJellyfin = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QSpinBox* m_volumeSpin = nullptr;
    QCheckBox* m_blankOtherDisplays = nullptr;
    QCheckBox* m_preferHdr10Plus = nullptr;
    QCheckBox* m_absolutePq = nullptr;
    QComboBox* m_toneMapping = nullptr;
    QLabel* m_toneMappingLabel = nullptr;
    QCheckBox* m_peakDetection = nullptr;
    VideoToneMappingSettings m_toneMappingSettings;
    bool m_hdrPresentationActive = false;
    QLabel* m_absolutePqStatus = nullptr;
    QWidget* m_sourceHdrReferenceWhitePresets = nullptr;
    QSlider* m_sourceHdrReferenceWhiteSlider = nullptr;
    QSpinBox* m_sourceHdrReferenceWhiteSpin = nullptr;
    QComboBox* m_appearanceMode = nullptr;
    QWidget* m_customAppearanceControls = nullptr;
    QPushButton* m_textColor = nullptr;
    QSpinBox* m_textOpacity = nullptr;
    QCheckBox* m_backgroundEnabled = nullptr;
    QPushButton* m_backgroundColor = nullptr;
    QSpinBox* m_backgroundOpacity = nullptr;
    QComboBox* m_backgroundPreset = nullptr;
    QComboBox* m_edgeStyle = nullptr;
    QPushButton* m_edgeColor = nullptr;
    QSpinBox* m_edgeOpacity = nullptr;
    QComboBox* m_sizeMode = nullptr;
    QSpinBox* m_scale = nullptr;
    QComboBox* m_positionMode = nullptr;
    QSlider* m_verticalPosition = nullptr;
    QSpinBox* m_verticalPositionSpin = nullptr;
    QSlider* m_brightness = nullptr;
    QSpinBox* m_brightnessSpin = nullptr;
    QSlider* m_overallOpacity = nullptr;
    QSpinBox* m_overallOpacitySpin = nullptr;
    QLabel* m_warning = nullptr;
};
