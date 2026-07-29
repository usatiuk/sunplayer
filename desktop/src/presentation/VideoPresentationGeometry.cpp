#include "presentation/VideoPresentationGeometry.h"

#include <algorithm>
#include <cmath>

QRect aspectFitVideoRect(
        const QRect &viewport,
        std::optional<double> displayAspectRatio) {
    if (viewport.isEmpty()
            || !displayAspectRatio
            || !std::isfinite(*displayAspectRatio)
            || *displayAspectRatio <= 0.0) {
        return viewport;
    }

    const double viewportAspectRatio =
        static_cast<double>(viewport.width())
        / static_cast<double>(viewport.height());
    QSize fittedSize;
    if (*displayAspectRatio >= viewportAspectRatio) {
        fittedSize.setWidth(viewport.width());
        fittedSize.setHeight(std::clamp(
            qRound(
                static_cast<double>(viewport.width())
                / *displayAspectRatio),
            1,
            viewport.height()));
    } else {
        fittedSize.setHeight(viewport.height());
        fittedSize.setWidth(std::clamp(
            qRound(
                static_cast<double>(viewport.height())
                * *displayAspectRatio),
            1,
            viewport.width()));
    }

    return {
        viewport.x()
            + (viewport.width() - fittedSize.width()) / 2,
        viewport.y()
            + (viewport.height() - fittedSize.height()) / 2,
        fittedSize.width(),
        fittedSize.height(),
    };
}
