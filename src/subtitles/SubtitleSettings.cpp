#include "subtitles/SubtitleSettings.h"

#include <cmath>
#include <utility>

namespace {
bool validUnit(qreal value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; }
bool validScale(qreal value) { return std::isfinite(value) && value >= 0.5 && value <= 2.0; }
QColor opaque(QColor value) {
    value.setAlpha(255);
    return value;
}
} // namespace

SubtitleSettings::SubtitleSettings(QObject* parent) : QObject(parent) {}

SubtitleSettings::AppearanceMode SubtitleSettings::appearanceMode() const {
    return m_values.appearanceMode == SubtitleAppearanceValues::AppearanceMode::Custom ? CustomAppearance
                                                                                       : AuthoredAppearance;
}
QColor SubtitleSettings::textColor() const { return m_values.textColor; }
qreal SubtitleSettings::textOpacity() const { return m_values.textOpacity; }
bool SubtitleSettings::backgroundEnabled() const { return m_values.backgroundEnabled; }
QColor SubtitleSettings::backgroundColor() const { return m_values.backgroundColor; }
qreal SubtitleSettings::backgroundOpacity() const { return m_values.backgroundOpacity; }
SubtitleSettings::EdgeStyle SubtitleSettings::edgeStyle() const {
    switch (m_values.edgeStyle) {
    case SubtitleAppearanceValues::EdgeStyle::Outline:
        return Outline;
    case SubtitleAppearanceValues::EdgeStyle::Shadow:
        return Shadow;
    case SubtitleAppearanceValues::EdgeStyle::None:
        return NoEdge;
    }
    Q_UNREACHABLE();
}
QColor SubtitleSettings::edgeColor() const { return m_values.edgeColor; }
qreal SubtitleSettings::edgeOpacity() const { return m_values.edgeOpacity; }
SubtitleSettings::SizeMode SubtitleSettings::sizeMode() const {
    return m_values.sizeMode == SubtitleAppearanceValues::SizeMode::Custom ? CustomSize : AuthoredSize;
}
qreal SubtitleSettings::scale() const { return m_values.scale; }
SubtitleSettings::PositionMode SubtitleSettings::positionMode() const {
    return m_values.positionMode == SubtitleAppearanceValues::PositionMode::Custom ? CustomPosition : AuthoredPosition;
}
qreal SubtitleSettings::verticalPosition() const { return m_values.verticalPosition; }
qreal SubtitleSettings::overallOpacity() const { return m_values.overallOpacity; }
qulonglong SubtitleSettings::rasterRevision() const { return m_rasterRevision; }

void SubtitleSettings::setAppearanceMode(AppearanceMode value) {
    auto const mapped = value == CustomAppearance ? SubtitleAppearanceValues::AppearanceMode::Custom
                                                  : SubtitleAppearanceValues::AppearanceMode::Authored;
    if (mapped != m_values.appearanceMode) {
        m_values.appearanceMode = mapped;
        changed(SubtitleAppearanceField::AppearanceMode, true);
    }
}
void SubtitleSettings::setTextColor(QColor value) {
    value = opaque(value);
    if (value.isValid() && value != m_values.textColor) {
        m_values.textColor = value;
        changed(SubtitleAppearanceField::TextColor, true);
    }
}
void SubtitleSettings::setTextOpacity(qreal value) {
    if (validUnit(value) && !qFuzzyCompare(value, m_values.textOpacity)) {
        m_values.textOpacity = value;
        changed(SubtitleAppearanceField::TextOpacity, true);
    }
}
void SubtitleSettings::setBackgroundEnabled(bool value) {
    if (value != m_values.backgroundEnabled) {
        m_values.backgroundEnabled = value;
        changed(SubtitleAppearanceField::BackgroundEnabled, true);
    }
}
void SubtitleSettings::setBackgroundColor(QColor value) {
    value = opaque(value);
    if (value.isValid() && value != m_values.backgroundColor) {
        m_values.backgroundColor = value;
        changed(SubtitleAppearanceField::BackgroundColor, true);
    }
}
void SubtitleSettings::setBackgroundOpacity(qreal value) {
    if (validUnit(value) && !qFuzzyCompare(value, m_values.backgroundOpacity)) {
        m_values.backgroundOpacity = value;
        changed(SubtitleAppearanceField::BackgroundOpacity, true);
    }
}
void SubtitleSettings::setEdgeStyle(EdgeStyle value) {
    auto const mapped = value == Outline  ? SubtitleAppearanceValues::EdgeStyle::Outline
                        : value == Shadow ? SubtitleAppearanceValues::EdgeStyle::Shadow
                                          : SubtitleAppearanceValues::EdgeStyle::None;
    if (mapped != m_values.edgeStyle) {
        m_values.edgeStyle = mapped;
        changed(SubtitleAppearanceField::EdgeStyle, true);
    }
}
void SubtitleSettings::setEdgeColor(QColor value) {
    value = opaque(value);
    if (value.isValid() && value != m_values.edgeColor) {
        m_values.edgeColor = value;
        changed(SubtitleAppearanceField::EdgeColor, true);
    }
}
void SubtitleSettings::setEdgeOpacity(qreal value) {
    if (validUnit(value) && !qFuzzyCompare(value, m_values.edgeOpacity)) {
        m_values.edgeOpacity = value;
        changed(SubtitleAppearanceField::EdgeOpacity, true);
    }
}
void SubtitleSettings::setSizeMode(SizeMode value) {
    auto const mapped =
        value == CustomSize ? SubtitleAppearanceValues::SizeMode::Custom : SubtitleAppearanceValues::SizeMode::Authored;
    if (mapped != m_values.sizeMode) {
        m_values.sizeMode = mapped;
        changed(SubtitleAppearanceField::SizeMode, true);
    }
}
void SubtitleSettings::setScale(qreal value) {
    if (validScale(value) && !qFuzzyCompare(value, m_values.scale)) {
        m_values.scale = value;
        changed(SubtitleAppearanceField::Scale, true);
    }
}
void SubtitleSettings::setPositionMode(PositionMode value) {
    auto const mapped = value == CustomPosition ? SubtitleAppearanceValues::PositionMode::Custom
                                                : SubtitleAppearanceValues::PositionMode::Authored;
    if (mapped != m_values.positionMode) {
        m_values.positionMode = mapped;
        changed(SubtitleAppearanceField::PositionMode, true);
    }
}
void SubtitleSettings::setVerticalPosition(qreal value) {
    if (validUnit(value) && !qFuzzyCompare(value, m_values.verticalPosition)) {
        m_values.verticalPosition = value;
        changed(SubtitleAppearanceField::VerticalPosition, true);
    }
}
void SubtitleSettings::setOverallOpacity(qreal value) {
    if (validUnit(value) && !qFuzzyCompare(value, m_values.overallOpacity)) {
        m_values.overallOpacity = value;
        changed(SubtitleAppearanceField::OverallOpacity, false);
    }
}

SubtitleAppearanceValues SubtitleSettings::values() const { return m_values; }
SubtitleAppearanceSnapshot SubtitleSettings::snapshot() const {
    SubtitleAppearanceSnapshot result;
    static_cast<SubtitleAppearanceValues&>(result) = m_values;
    result.rasterRevision = m_rasterRevision;
    return result;
}

void SubtitleSettings::restore(SubtitleAppearanceValues const& values) { apply(values, false, false); }

void SubtitleSettings::applyAsAuthored() {
    auto values = m_values;
    values.appearanceMode = SubtitleAppearanceValues::AppearanceMode::Authored;
    values.sizeMode = SubtitleAppearanceValues::SizeMode::Authored;
    values.positionMode = SubtitleAppearanceValues::PositionMode::Authored;
    values.overallOpacity = 1.0;
    apply(values, true, false);
}
void SubtitleSettings::applyHighContrast() {
    auto values = m_values;
    values.appearanceMode = SubtitleAppearanceValues::AppearanceMode::Custom;
    values.textColor = Qt::white;
    values.textOpacity = 1.0;
    values.backgroundEnabled = true;
    values.backgroundColor = Qt::black;
    values.backgroundOpacity = 0.8;
    values.edgeStyle = SubtitleAppearanceValues::EdgeStyle::None;
    values.edgeColor = Qt::black;
    values.edgeOpacity = 1.0;
    values.sizeMode = SubtitleAppearanceValues::SizeMode::Authored;
    values.positionMode = SubtitleAppearanceValues::PositionMode::Authored;
    values.overallOpacity = 1.0;
    apply(values, true, false);
}
void SubtitleSettings::applyLargeText() {
    auto values = m_values;
    values.appearanceMode = SubtitleAppearanceValues::AppearanceMode::Authored;
    values.sizeMode = SubtitleAppearanceValues::SizeMode::Custom;
    values.scale = 1.5;
    values.positionMode = SubtitleAppearanceValues::PositionMode::Authored;
    values.overallOpacity = 1.0;
    apply(values, true, false);
}
void SubtitleSettings::restoreDefaults() { apply({}, false, true); }

void SubtitleSettings::apply(SubtitleAppearanceValues values, bool persist, bool reset) {
    if (values == m_values) {
        if (reset) {
            emit persistenceResetRequested();
        }
        return;
    }
    SubtitleAppearanceFields dirtyFields = 0;
    auto mark = [&dirtyFields](bool changed, SubtitleAppearanceFields field) {
        if (changed) {
            dirtyFields |= field;
        }
    };
    mark(values.appearanceMode != m_values.appearanceMode, SubtitleAppearanceField::AppearanceMode);
    mark(values.textColor != m_values.textColor, SubtitleAppearanceField::TextColor);
    mark(!qFuzzyCompare(values.textOpacity, m_values.textOpacity), SubtitleAppearanceField::TextOpacity);
    mark(values.backgroundEnabled != m_values.backgroundEnabled, SubtitleAppearanceField::BackgroundEnabled);
    mark(values.backgroundColor != m_values.backgroundColor, SubtitleAppearanceField::BackgroundColor);
    mark(!qFuzzyCompare(values.backgroundOpacity, m_values.backgroundOpacity),
         SubtitleAppearanceField::BackgroundOpacity);
    mark(values.edgeStyle != m_values.edgeStyle, SubtitleAppearanceField::EdgeStyle);
    mark(values.edgeColor != m_values.edgeColor, SubtitleAppearanceField::EdgeColor);
    mark(!qFuzzyCompare(values.edgeOpacity, m_values.edgeOpacity), SubtitleAppearanceField::EdgeOpacity);
    mark(values.sizeMode != m_values.sizeMode, SubtitleAppearanceField::SizeMode);
    mark(!qFuzzyCompare(values.scale, m_values.scale), SubtitleAppearanceField::Scale);
    mark(values.positionMode != m_values.positionMode, SubtitleAppearanceField::PositionMode);
    mark(!qFuzzyCompare(values.verticalPosition, m_values.verticalPosition), SubtitleAppearanceField::VerticalPosition);
    mark(!qFuzzyCompare(values.overallOpacity, m_values.overallOpacity), SubtitleAppearanceField::OverallOpacity);
    bool const rasterChanged = (dirtyFields & ~SubtitleAppearanceField::OverallOpacity) != 0;
    m_values = std::move(values);
    if (rasterChanged && ++m_rasterRevision == 0) {
        ++m_rasterRevision;
    }
    emit settingsChanged();
    if (reset) {
        emit persistenceResetRequested();
    } else if (persist) {
        emit persistenceChanged(dirtyFields);
    }
}

void SubtitleSettings::changed(SubtitleAppearanceFields dirtyField, bool rasterAffecting) {
    if (rasterAffecting && ++m_rasterRevision == 0) {
        ++m_rasterRevision;
    }
    emit settingsChanged();
    emit persistenceChanged(dirtyField);
}
