#pragma once

#include <cstdint>

#include <QColor>
#include <QtGlobal>

struct SubtitleAppearanceValues {
    enum class AppearanceMode { Authored, Custom };
    enum class EdgeStyle { None, Outline, Shadow };
    enum class SizeMode { Authored, Custom };
    enum class PositionMode { Authored, Custom };

    AppearanceMode appearanceMode = AppearanceMode::Authored;
    QColor textColor = Qt::white;
    qreal textOpacity = 1.0;
    bool backgroundEnabled = true;
    QColor backgroundColor = Qt::black;
    qreal backgroundOpacity = 0.8;
    EdgeStyle edgeStyle = EdgeStyle::None;
    QColor edgeColor = Qt::black;
    qreal edgeOpacity = 1.0;
    SizeMode sizeMode = SizeMode::Authored;
    qreal scale = 1.0;
    PositionMode positionMode = PositionMode::Authored;
    qreal verticalPosition = 0.0;
    qreal overallOpacity = 1.0;

    bool operator==(SubtitleAppearanceValues const&) const = default;
};

struct SubtitleAppearanceSnapshot : SubtitleAppearanceValues {
    std::uint64_t rasterRevision = 0;
};

using SubtitleAppearanceFields = quint32;

namespace SubtitleAppearanceField {
constexpr SubtitleAppearanceFields AppearanceMode = 1U << 0U;
constexpr SubtitleAppearanceFields TextColor = 1U << 1U;
constexpr SubtitleAppearanceFields TextOpacity = 1U << 2U;
constexpr SubtitleAppearanceFields BackgroundEnabled = 1U << 3U;
constexpr SubtitleAppearanceFields BackgroundColor = 1U << 4U;
constexpr SubtitleAppearanceFields BackgroundOpacity = 1U << 5U;
constexpr SubtitleAppearanceFields EdgeStyle = 1U << 6U;
constexpr SubtitleAppearanceFields EdgeColor = 1U << 7U;
constexpr SubtitleAppearanceFields EdgeOpacity = 1U << 8U;
constexpr SubtitleAppearanceFields SizeMode = 1U << 9U;
constexpr SubtitleAppearanceFields Scale = 1U << 10U;
constexpr SubtitleAppearanceFields PositionMode = 1U << 11U;
constexpr SubtitleAppearanceFields VerticalPosition = 1U << 12U;
constexpr SubtitleAppearanceFields OverallOpacity = 1U << 13U;
constexpr SubtitleAppearanceFields All = (1U << 14U) - 1U;
} // namespace SubtitleAppearanceField
