#include "app/ApplicationSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

#include <QMetaType>
#include <QRegularExpression>
#include <QString>
#include <QVariant>

#include "diagnostics/LogCategories.h"

namespace {

constexpr auto volumeKey = "playback/volume";
constexpr auto blankOtherDisplaysKey = "fullscreen/blankOtherDisplays";
constexpr auto subtitleAppearancePrefix = "subtitles/appearance";
constexpr auto appearanceModeKey = "subtitles/appearance/mode";
constexpr auto textColorKey = "subtitles/appearance/textColor";
constexpr auto textOpacityKey = "subtitles/appearance/textOpacity";
constexpr auto backgroundEnabledKey = "subtitles/appearance/backgroundEnabled";
constexpr auto backgroundColorKey = "subtitles/appearance/backgroundColor";
constexpr auto backgroundOpacityKey = "subtitles/appearance/backgroundOpacity";
constexpr auto edgeStyleKey = "subtitles/appearance/edgeStyle";
constexpr auto edgeColorKey = "subtitles/appearance/edgeColor";
constexpr auto edgeOpacityKey = "subtitles/appearance/edgeOpacity";
constexpr auto sizeModeKey = "subtitles/appearance/sizeMode";
constexpr auto scaleKey = "subtitles/appearance/scale";
constexpr auto positionModeKey = "subtitles/appearance/positionMode";
constexpr auto verticalPositionKey = "subtitles/appearance/verticalPosition";
constexpr auto overallOpacityKey = "subtitles/appearance/overallOpacity";

constexpr std::array<char const*, 14> subtitleAppearanceKeys{
    appearanceModeKey,   textColorKey,         textOpacityKey, backgroundEnabledKey,
    backgroundColorKey,  backgroundOpacityKey, edgeStyleKey,   edgeColorKey,
    edgeOpacityKey,      sizeModeKey,          scaleKey,       positionModeKey,
    verticalPositionKey, overallOpacityKey,
};

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

std::optional<qreal> parseNumber(QVariant const& value, qreal minimum, qreal maximum) {
    if (value.metaType().id() == QMetaType::Bool) {
        return std::nullopt;
    }
    bool converted = false;
    qreal const number = value.toDouble(&converted);
    if (!converted || !std::isfinite(number) || number < minimum || number > maximum) {
        return std::nullopt;
    }
    return number;
}

std::optional<QColor> parseRgb(QVariant const& value) {
    if (value.metaType().id() != QMetaType::QString) {
        return std::nullopt;
    }
    QString const text = value.toString();
    static QRegularExpression const pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!pattern.match(text).hasMatch()) {
        return std::nullopt;
    }
    QColor const color(text);
    return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
}

QString rgbName(QColor const& color) { return color.name(QColor::HexRgb).toUpper(); }

template <typename T>
std::optional<T> parseChoice(QVariant const& value, std::initializer_list<std::pair<QStringView, T>> choices) {
    if (value.metaType().id() != QMetaType::QString) {
        return std::nullopt;
    }
    QString const text = value.toString();
    for (auto const& [name, choice] : choices) {
        if (text == name) {
            return choice;
        }
    }
    return std::nullopt;
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

    bool const hasSubtitleAppearance =
        std::any_of(subtitleAppearanceKeys.cbegin(), subtitleAppearanceKeys.cend(),
                    [this](char const* key) { return m_settings.contains(QLatin1StringView(key)); });
    if (hasSubtitleAppearance) {
        SubtitleAppearanceValues appearance;
        auto invalid = [this](char const* key) { reportInvalidValue(QString::fromLatin1(key)); };
        auto load = [this, &invalid]<typename T>(char const* key, auto parser, T& destination) {
            if (!m_settings.contains(QLatin1StringView(key))) {
                return;
            }
            auto const parsed = parser(m_settings.value(QLatin1StringView(key)));
            if (parsed) {
                destination = *parsed;
            } else {
                invalid(key);
            }
        };
        load(
            appearanceModeKey,
            [](QVariant const& value) {
                return parseChoice<SubtitleAppearanceValues::AppearanceMode>(
                    value, {{u"authored", SubtitleAppearanceValues::AppearanceMode::Authored},
                            {u"custom", SubtitleAppearanceValues::AppearanceMode::Custom}});
            },
            appearance.appearanceMode);
        load(textColorKey, parseRgb, appearance.textColor);
        load(
            textOpacityKey, [](QVariant const& value) { return parseNumber(value, 0.0, 1.0); }, appearance.textOpacity);
        load(backgroundEnabledKey, parseBoolean, appearance.backgroundEnabled);
        load(backgroundColorKey, parseRgb, appearance.backgroundColor);
        load(
            backgroundOpacityKey, [](QVariant const& value) { return parseNumber(value, 0.0, 1.0); },
            appearance.backgroundOpacity);
        load(
            edgeStyleKey,
            [](QVariant const& value) {
                return parseChoice<SubtitleAppearanceValues::EdgeStyle>(
                    value, {{u"none", SubtitleAppearanceValues::EdgeStyle::None},
                            {u"outline", SubtitleAppearanceValues::EdgeStyle::Outline},
                            {u"shadow", SubtitleAppearanceValues::EdgeStyle::Shadow}});
            },
            appearance.edgeStyle);
        load(edgeColorKey, parseRgb, appearance.edgeColor);
        load(
            edgeOpacityKey, [](QVariant const& value) { return parseNumber(value, 0.0, 1.0); }, appearance.edgeOpacity);
        load(
            sizeModeKey,
            [](QVariant const& value) {
                return parseChoice<SubtitleAppearanceValues::SizeMode>(
                    value, {{u"authored", SubtitleAppearanceValues::SizeMode::Authored},
                            {u"custom", SubtitleAppearanceValues::SizeMode::Custom}});
            },
            appearance.sizeMode);
        load(scaleKey, [](QVariant const& value) { return parseNumber(value, 0.5, 2.0); }, appearance.scale);
        load(
            positionModeKey,
            [](QVariant const& value) {
                return parseChoice<SubtitleAppearanceValues::PositionMode>(
                    value, {{u"authored", SubtitleAppearanceValues::PositionMode::Authored},
                            {u"custom", SubtitleAppearanceValues::PositionMode::Custom}});
            },
            appearance.positionMode);
        load(
            verticalPositionKey, [](QVariant const& value) { return parseNumber(value, 0.0, 1.0); },
            appearance.verticalPosition);
        load(
            overallOpacityKey, [](QVariant const& value) { return parseNumber(value, 0.0, 1.0); },
            appearance.overallOpacity);
        values.subtitleAppearance = appearance;
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

void ApplicationSettings::setSubtitleAppearance(SubtitleAppearanceValues const& values,
                                                SubtitleAppearanceFields dirtyFields) {
    auto write = [this, dirtyFields](SubtitleAppearanceFields field, char const* key, QVariant const& value) {
        if ((dirtyFields & field) != 0) {
            m_settings.setValue(QLatin1StringView(key), value);
        }
    };
    write(SubtitleAppearanceField::AppearanceMode, appearanceModeKey,
          values.appearanceMode == SubtitleAppearanceValues::AppearanceMode::Custom ? QStringLiteral("custom")
                                                                                    : QStringLiteral("authored"));
    write(SubtitleAppearanceField::TextColor, textColorKey, rgbName(values.textColor));
    write(SubtitleAppearanceField::TextOpacity, textOpacityKey, values.textOpacity);
    write(SubtitleAppearanceField::BackgroundEnabled, backgroundEnabledKey, values.backgroundEnabled);
    write(SubtitleAppearanceField::BackgroundColor, backgroundColorKey, rgbName(values.backgroundColor));
    write(SubtitleAppearanceField::BackgroundOpacity, backgroundOpacityKey, values.backgroundOpacity);
    QString const edgeStyle =
        values.edgeStyle == SubtitleAppearanceValues::EdgeStyle::Outline  ? QStringLiteral("outline")
        : values.edgeStyle == SubtitleAppearanceValues::EdgeStyle::Shadow ? QStringLiteral("shadow")
                                                                          : QStringLiteral("none");
    write(SubtitleAppearanceField::EdgeStyle, edgeStyleKey, edgeStyle);
    write(SubtitleAppearanceField::EdgeColor, edgeColorKey, rgbName(values.edgeColor));
    write(SubtitleAppearanceField::EdgeOpacity, edgeOpacityKey, values.edgeOpacity);
    write(SubtitleAppearanceField::SizeMode, sizeModeKey,
          values.sizeMode == SubtitleAppearanceValues::SizeMode::Custom ? QStringLiteral("custom")
                                                                        : QStringLiteral("authored"));
    write(SubtitleAppearanceField::Scale, scaleKey, values.scale);
    write(SubtitleAppearanceField::PositionMode, positionModeKey,
          values.positionMode == SubtitleAppearanceValues::PositionMode::Custom ? QStringLiteral("custom")
                                                                                : QStringLiteral("authored"));
    write(SubtitleAppearanceField::VerticalPosition, verticalPositionKey, values.verticalPosition);
    write(SubtitleAppearanceField::OverallOpacity, overallOpacityKey, values.overallOpacity);
    reportStatus();
}

void ApplicationSettings::removeSubtitleAppearance() {
    m_settings.remove(QLatin1StringView(subtitleAppearancePrefix));
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
