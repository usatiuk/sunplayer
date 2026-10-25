#include "platform/macos/MacDisplayGamut.h"

#include <limits>

#include <lcms2.h>

#include <QScopeGuard>

namespace {
std::optional<ColorChromaticity> chromaticity(cmsCIEXYZ xyz) {
    // ICC 16.16 rounding can put recovered P3 red a few millionths below Z=0.
    for (double* component : {&xyz.X, &xyz.Y, &xyz.Z}) {
        if (*component < 0.0 && *component >= -4.0 / 65536.0) {
            *component = 0.0;
        }
    }
    double const sum = xyz.X + xyz.Y + xyz.Z;
    if (!std::isfinite(sum) || sum <= 0.0) {
        return std::nullopt;
    }
    cmsCIExyY xyY;
    cmsXYZ2xyY(&xyY, &xyz);
    return ColorChromaticity{static_cast<float>(xyY.x), static_cast<float>(xyY.y)};
}
} // namespace

std::optional<ColorPrimaries> macDisplayGamut(QByteArray const& profileData) {
    if (profileData.isEmpty() || profileData.size() > std::numeric_limits<cmsUInt32Number>::max()) {
        return std::nullopt;
    }
    cmsContext const context = cmsCreateContext(nullptr, nullptr);
    Q_CHECK_PTR(context);
    auto releaseContext = qScopeGuard([&] { cmsDeleteContext(context); });
    cmsHPROFILE const profile =
        cmsOpenProfileFromMemTHR(context, profileData.constData(), static_cast<cmsUInt32Number>(profileData.size()));
    if (!profile) {
        return std::nullopt;
    }
    auto releaseProfile = qScopeGuard([&] { cmsCloseProfile(profile); });
    if (cmsGetColorSpace(profile) != cmsSigRgbData || !cmsIsMatrixShaper(profile)) {
        return std::nullopt;
    }

    // Like libplacebo's ICC characterization, use an unadapted observer to
    // recover native RGB/white, rather than the profile's D50 connection space.
    cmsSetAdaptationStateTHR(context, 0.0);
    cmsHPROFILE const xyzProfile = cmsCreateXYZProfileTHR(context);
    Q_CHECK_PTR(xyzProfile);
    auto releaseXyzProfile = qScopeGuard([&] { cmsCloseProfile(xyzProfile); });
    cmsHTRANSFORM const transform =
        cmsCreateTransformTHR(context, profile, TYPE_RGB_DBL, xyzProfile, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC,
                              cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE);
    if (!transform) {
        return std::nullopt;
    }
    auto releaseTransform = qScopeGuard([&] { cmsDeleteTransform(transform); });
    double const rgb[4][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
    cmsCIEXYZ xyz[4];
    cmsDoTransform(transform, rgb, xyz, 4);
    auto const red = chromaticity(xyz[0]);
    auto const green = chromaticity(xyz[1]);
    auto const blue = chromaticity(xyz[2]);
    auto const white = chromaticity(xyz[3]);
    if (!red || !green || !blue || !white) {
        return std::nullopt;
    }
    ColorPrimaries const primaries{*red, *green, *blue, *white};
    return primaries.isValid() ? std::optional{primaries} : std::nullopt;
}
