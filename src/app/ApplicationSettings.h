#pragma once

#include <optional>

#include <QSettings>
#include <QString>

class ApplicationSettings final {
  public:
    struct Values {
        std::optional<qreal> volume;
        std::optional<bool> blankOtherDisplaysInFullscreen;
    };

    ApplicationSettings();
    explicit ApplicationSettings(QString const& filePath);

    Values load();
    void setVolume(qreal volume);
    void setBlankOtherDisplaysInFullscreen(bool enabled);
    void sync();

  private:
    void reportInvalidValue(QString const& key);
    void reportStatus();
    void reportFault(QString const& reason, QString const& key = {});

    QSettings m_settings;
    bool m_faultReported = false;
};
