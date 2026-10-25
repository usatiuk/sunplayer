#import <AppKit/AppKit.h>

#include <lcms2.h>

#include <QScopeGuard>
#include <QtTest>

#include "platform/macos/MacDisplayGamut.h"
#include "presentation/PresentationTarget.h"

namespace {
void comparePrimaries(ColorPrimaries const& actual, ColorPrimaries const& expected) {
    auto compare = [](ColorChromaticity a, ColorChromaticity b) {
        QVERIFY(std::abs(a.x - b.x) < 0.0001f);
        QVERIFY(std::abs(a.y - b.y) < 0.0001f);
    };
    QVERIFY(actual.isValid());
    compare(actual.red, expected.red);
    compare(actual.green, expected.green);
    compare(actual.blue, expected.blue);
    compare(actual.white, expected.white);
}
} // namespace

class MacDisplayGamutTest final : public QObject {
    Q_OBJECT
  private slots:
    void nativeProfiles();
    void customProfileAndSharedTarget_data();
    void customProfileAndSharedTarget();
    void invalidProfiles();
};

void MacDisplayGamutTest::nativeProfiles() {
    auto const srgb = macDisplayGamut(QByteArray::fromNSData([NSColorSpace sRGBColorSpace].ICCProfileData));
    QVERIFY(srgb);
    comparePrimaries(*srgb, srgbColorPrimaries);

    auto const p3 = macDisplayGamut(QByteArray::fromNSData([NSColorSpace displayP3ColorSpace].ICCProfileData));
    QVERIFY(p3);
    comparePrimaries(*p3, {{0.680f, 0.320f}, {0.265f, 0.690f}, {0.150f, 0.060f}, {0.3127f, 0.3290f}});
}

void MacDisplayGamutTest::customProfileAndSharedTarget_data() {
    QTest::addColumn<double>("version");
    QTest::newRow("v2") << 2.1;
    QTest::newRow("v4") << 4.3;
}

void MacDisplayGamutTest::customProfileAndSharedTarget() {
    QFETCH(double, version);
    // Nonstandard primaries and D60 white: named-gamut substitution or reading
    // the profile's D50 connection-space white fails this regression.
    cmsCIExyY const white{0.32168, 0.33767, 1};
    cmsCIExyYTRIPLE const primaries{{0.67, 0.32, 1}, {0.21, 0.71, 1}, {0.14, 0.07, 1}};
    cmsToneCurve* const gamma = cmsBuildGamma(nullptr, 2.2);
    QVERIFY(gamma);
    auto releaseGamma = qScopeGuard([&] { cmsFreeToneCurve(gamma); });
    cmsToneCurve* curves[]{gamma, gamma, gamma};
    cmsHPROFILE const profile = cmsCreateRGBProfile(&white, &primaries, curves);
    QVERIFY(profile);
    auto releaseProfile = qScopeGuard([&] { cmsCloseProfile(profile); });
    cmsSetProfileVersion(profile, version);
    cmsUInt32Number size = 0;
    QVERIFY(cmsSaveProfileToMem(profile, nullptr, &size));
    QByteArray data(size, '\0');
    QVERIFY(cmsSaveProfileToMem(profile, data.data(), &size));
    auto const gamut = macDisplayGamut(data);
    QVERIFY(gamut);
    comparePrimaries(*gamut, {{0.67f, 0.32f}, {0.21f, 0.71f}, {0.14f, 0.07f}, {0.32168f, 0.33767f}});

    DisplayState display;
    display.valid = true;
    display.colorMode = DisplayColorMode::HighDynamicRange;
    display.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
    display.currentHeadroom = 2;
    display.targetPrimariesKnown = true;
    display.targetPrimaries = *gamut;
    PresentationBackendState backend;
    backend.hdrPresentationActive = true;
    auto const target = calculatePresentationTarget(display, backend);
    QVERIFY(target.color.targetPrimariesKnown);
    QCOMPARE(target.color.targetPrimaries, *gamut);
    QCOMPARE(target.color.targetPeakHeadroom, 2.0f);
    QCOMPARE(target.sdrScale, 1.0f);
}

void MacDisplayGamutTest::invalidProfiles() {
    QVERIFY(!macDisplayGamut({}));
    QVERIFY(!macDisplayGamut("invalid ICC profile"));
    QVERIFY(!macDisplayGamut(QByteArray::fromNSData([NSColorSpace displayP3ColorSpace].ICCProfileData).left(100)));
    auto const gray = QByteArray::fromNSData([NSColorSpace genericGrayColorSpace].ICCProfileData);
    QVERIFY(!gray.isEmpty());
    QVERIFY(!macDisplayGamut(gray));
}

QTEST_GUILESS_MAIN(MacDisplayGamutTest)
#include "tst_MacDisplayGamut.moc"
