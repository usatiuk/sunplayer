#include "settings/QSettingsStore.h"

QSettingsStore::QSettingsStore() { m_settings.setFallbacksEnabled(false); }
QSettingsStore::QSettingsStore(QString const& filePath) : m_settings(filePath, QSettings::IniFormat) {
    m_settings.setFallbacksEnabled(false);
}

SettingsStore::Status QSettingsStore::status() const {
    switch (m_settings.status()) {
    case QSettings::NoError:
        return Status::NoError;
    case QSettings::AccessError:
        return Status::AccessError;
    case QSettings::FormatError:
        return Status::FormatError;
    }
    Q_UNREACHABLE();
}

#ifndef Q_OS_WIN
std::unique_ptr<SettingsStore> createSettingsStore() { return std::make_unique<QSettingsStore>(); }
#endif

std::unique_ptr<SettingsStore> createTemporarySettingsStore(QString const& filePath) {
    return std::make_unique<QSettingsStore>(filePath);
}
