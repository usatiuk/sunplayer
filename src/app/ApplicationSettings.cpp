#include "app/ApplicationSettings.h"

#include <cmath>

#include <QMetaType>
#include <QString>
#include <QVariant>

#include "diagnostics/LogCategories.h"

namespace {

constexpr auto volumeKey = "playback/volume";
constexpr auto blankOtherDisplaysKey = "fullscreen/blankOtherDisplays";

std::optional<qreal> parseVolume(QVariant const& value) {
    if (value.metaType().id() == QMetaType::Bool) {
        return std::nullopt;
    }

    bool converted = false;
    qreal const volume = value.toDouble(&converted);
    if (!converted || !std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
        return std::nullopt;
    }
    return volume;
}

std::optional<bool> parseBoolean(QVariant const& value) {
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: {
        bool converted = false;
        qlonglong const numeric = value.toLongLong(&converted);
        if (converted && (numeric == 0 || numeric == 1)) {
            return numeric == 1;
        }
        return std::nullopt;
    }
    case QMetaType::QString: {
        QString const text = value.toString();
        if (text == QStringLiteral("true") || text == QStringLiteral("1")) {
            return true;
        }
        if (text == QStringLiteral("false") || text == QStringLiteral("0")) {
            return false;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

} // namespace

ApplicationSettings::ApplicationSettings() : m_settings() { m_settings.setFallbacksEnabled(false); }

ApplicationSettings::ApplicationSettings(QString const& filePath) : m_settings(filePath, QSettings::IniFormat) {
    m_settings.setFallbacksEnabled(false);
}

ApplicationSettings::Values ApplicationSettings::load() {
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        reportStatus();
        return {};
    }

    Values values;
    if (m_settings.contains(QLatin1StringView(volumeKey))) {
        values.volume = parseVolume(m_settings.value(QLatin1StringView(volumeKey)));
        if (!values.volume) {
            reportInvalidValue(QString::fromLatin1(volumeKey));
        }
    }
    if (m_settings.contains(QLatin1StringView(blankOtherDisplaysKey))) {
        values.blankOtherDisplaysInFullscreen =
            parseBoolean(m_settings.value(QLatin1StringView(blankOtherDisplaysKey)));
        if (!values.blankOtherDisplaysInFullscreen) {
            reportInvalidValue(QString::fromLatin1(blankOtherDisplaysKey));
        }
    }

    if (m_settings.status() != QSettings::NoError) {
        reportStatus();
        return {};
    }
    return values;
}

void ApplicationSettings::setVolume(qreal volume) {
    Q_ASSERT(std::isfinite(volume) && volume >= 0.0 && volume <= 1.0);
    m_settings.setValue(QLatin1StringView(volumeKey), volume);
    reportStatus();
}

void ApplicationSettings::setBlankOtherDisplaysInFullscreen(bool enabled) {
    m_settings.setValue(QLatin1StringView(blankOtherDisplaysKey), enabled);
    reportStatus();
}

void ApplicationSettings::sync() {
    m_settings.sync();
    reportStatus();
}

void ApplicationSettings::reportInvalidValue(QString const& key) { reportFault(QStringLiteral("invalid_value"), key); }

void ApplicationSettings::reportStatus() {
    switch (m_settings.status()) {
    case QSettings::NoError:
        return;
    case QSettings::AccessError:
        reportFault(QStringLiteral("access_error"));
        return;
    case QSettings::FormatError:
        reportFault(QStringLiteral("format_error"));
        return;
    }
    Q_UNREACHABLE();
}

void ApplicationSettings::reportFault(QString const& reason, QString const& key) {
    if (m_faultReported) {
        return;
    }
    m_faultReported = true;

    if (key.isEmpty()) {
        qCWarning(sunplayerLogApplication).noquote() << "event=application.settings_fault" << "reason=" + reason;
    } else {
        qCWarning(sunplayerLogApplication).noquote()
            << "event=application.settings_fault" << "reason=" + reason << "key=" + key;
    }
}
