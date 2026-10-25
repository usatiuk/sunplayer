#pragma once

#include <optional>

#include <QByteArray>

#include "platform/ColorPrimaries.h"

// Characterization only: ColorSync still owns the final display transform.
std::optional<ColorPrimaries> macDisplayGamut(QByteArray const& profileData);
