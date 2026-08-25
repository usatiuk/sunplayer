#pragma once

#include <QColor>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QWidget;
class SubtitleSettings;

class SettingsDialog final : public QDialog {
    Q_OBJECT

  public:
    enum Page {
        PlaybackPage = 0,
        SubtitlesPage = 1,
    };

    explicit SettingsDialog(SubtitleSettings& subtitleSettings);

    void showPage(int page);
    void setPlaybackState(qreal volume, bool blankingAvailable, bool blankingEnabled);

  signals:
    void volumeEdited(qreal volume);
    void blankOtherDisplaysEdited(bool enabled);

  private:
    void refreshSubtitles();
    void chooseTextColor();
    void chooseBackgroundColor();
    void chooseEdgeColor();
    void updateColorButton(QPushButton& button, QColor const& color);

    SubtitleSettings& m_subtitleSettings;
    QTabWidget* m_tabs = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QSpinBox* m_volumeSpin = nullptr;
    QCheckBox* m_blankOtherDisplays = nullptr;
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
    QSlider* m_overallOpacity = nullptr;
    QSpinBox* m_overallOpacitySpin = nullptr;
    QLabel* m_warning = nullptr;
};
