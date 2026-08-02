#pragma once

#include <optional>

#include <QRect>

// Fits a video's display aspect ratio inside the page-provided viewport.
// A missing or invalid aspect ratio preserves the viewport's fill behavior.
QRect aspectFitVideoRect(
    const QRect &viewport,
    std::optional<double> displayAspectRatio);
