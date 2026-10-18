#pragma once

#include <optional>

#include "settings/SettingsStore.h"
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
    SettingsStore& store() { return *m_settings; }
    bool isTemporaryStore() const { return m_temporaryStore; }
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

    std::unique_ptr<SettingsStore> m_settings;
    bool m_temporaryStore = false;
    bool m_faultReported = false;
};
