#pragma once

#include "settings/SettingsStore.h"

#include <QSettings>

class QSettingsStore final : public SettingsStore {
  public:
    QSettingsStore();
    explicit QSettingsStore(QString const& filePath);

    bool contains(QAnyStringView key) const override { return m_settings.contains(key); }
    QVariant value(QAnyStringView key, QVariant const& fallback = {}) const override {
        return m_settings.value(key, fallback);
    }
    void setValue(QAnyStringView key, QVariant const& value) override { m_settings.setValue(key, value); }
    void remove(QAnyStringView key) override { m_settings.remove(key); }
    void sync() override { m_settings.sync(); }
    Status status() const override;
    char const* backendName() const override { return "qsettings"; }

  private:
    QSettings m_settings;
};
