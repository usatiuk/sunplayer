#pragma once

#include <optional>

#include <QSettings>
#include <QString>

#include "subtitles/SubtitleAppearance.h"

class ApplicationSettings final {
  public:
    struct Values {
        std::optional<qreal> volume;
        std::optional<bool> preferHdr10Plus;
        std::optional<bool> absolutePqEnabled;
        std::optional<int> sourceHdrReferenceWhiteNits;
        std::optional<bool> blankOtherDisplaysInFullscreen;
        std::optional<SubtitleAppearanceValues> subtitleAppearance;
    };

    ApplicationSettings();
    explicit ApplicationSettings(QString const& filePath);

    Values load();
    void setVolume(qreal volume);
    void setPreferHdr10Plus(bool enabled);
    void setAbsolutePqEnabled(bool enabled);
    void setSourceHdrReferenceWhiteNits(int nits);
    void setBlankOtherDisplaysInFullscreen(bool enabled);
    void setSubtitleAppearance(SubtitleAppearanceValues const& values, SubtitleAppearanceFields dirtyFields);
    void removeSubtitleAppearance();
    void sync();

  private:
    void reportInvalidValue(QString const& key);
    void reportStatus();
    void reportFault(QString const& reason, QString const& key = {});

    QSettings m_settings;
    bool m_faultReported = false;
};
