#include <QtTest>

#include "presentation/PresentationTarget.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
ColorPrimaries displayP3Primaries() {
    return {
        .red = {0.680f, 0.320f},
        .green = {0.265f, 0.690f},
        .blue = {0.150f, 0.060f},
        .white = {0.3127f, 0.3290f},
    };
}
} // namespace

class PresentationTargetTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void calculation_data();
    void calculation();
    void automaticPhysicalTargetEligibility();
};

void PresentationTargetTest::calculation_data() {
    QTest::addColumn<DisplayState>("display");
    QTest::addColumn<PresentationBackendState>("backend");
    QTest::addColumn<PresentationTarget>("expected");

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::HighDynamicRange;
        display.sdrWhiteNits = 240.0f;
        display.minLuminanceNits = 0.01f;
        display.maxLuminanceNits = 1200.0f;

        PresentationBackendState backend;
        backend.sdrWhiteKnown = true;
        backend.luminanceKnown = true;
        backend.maxLuminanceNits = 1000.0f;
        backend.currentHeadroom = 12.5f;
        backend.potentialHeadroom = 15.0f;

        QTest::newRow("sdr-output-ignores-hdr-telemetry") << display << backend << PresentationTarget{};
    }

    {
        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = true;
        backend.sdrWhiteKnown = true;
        backend.luminanceKnown = true;
        backend.sdrWhiteNits = 200.0f;
        backend.minLuminanceNits = 0.01f;
        backend.maxLuminanceNits = 1000.0f;
        backend.currentHeadroom = 2.0f;
        backend.potentialHeadroom = 4.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sceneReferred = true;
        expected.sdrWhiteKnown = true;
        expected.luminanceKnown = true;
        expected.sdrWhiteNits = 200.0f;
        expected.minLuminanceNits = 0.01f;
        expected.maxLuminanceNits = 1000.0f;
        expected.currentHeadroom = 12.5f;
        expected.potentialHeadroom = 12.5f;
        expected.effectiveTargetHeadroom = 5.0f;
        expected.sdrScale = 2.5f;

        QTest::newRow("scene-referred-backend-luminance") << DisplayState{} << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::HighDynamicRange;
        display.luminanceBehavior = DisplayLuminanceBehavior::SceneReferred;
        display.sdrWhiteKnown = true;
        display.luminanceKnown = true;
        display.sdrWhiteNits = 200.0f;
        display.minLuminanceNits = 0.02f;
        display.maxLuminanceNits = 1200.0f;
        display.targetPrimariesKnown = true;
        display.targetPrimaries = displayP3Primaries();

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = true;
        backend.sdrWhiteKnown = true;
        backend.luminanceKnown = true;
        backend.sdrWhiteNits = 160.0f;
        backend.minLuminanceNits = 0.05f;
        backend.maxLuminanceNits = 800.0f;
        backend.currentHeadroom = 10.0f;
        backend.potentialHeadroom = 18.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sceneReferred = true;
        expected.targetPrimariesKnown = true;
        expected.targetPrimaries = displayP3Primaries();
        expected.sdrWhiteKnown = true;
        expected.luminanceKnown = true;
        expected.sdrWhiteNits = 200.0f;
        expected.minLuminanceNits = 0.02f;
        expected.maxLuminanceNits = 1200.0f;
        expected.currentHeadroom = 15.0f;
        expected.potentialHeadroom = 18.0f;
        expected.effectiveTargetHeadroom = 6.0f;
        expected.sdrScale = 2.5f;

        QTest::newRow("active-windows-hdr-takes-precedence") << display << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::StandardDynamicRange;
        display.sdrWhiteNits = 250.0f;
        display.minLuminanceNits = 0.01f;
        display.maxLuminanceNits = 2000.0f;
        display.targetPrimariesKnown = true;
        display.targetPrimaries = displayP3Primaries();

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = false;
        backend.sdrWhiteKnown = true;
        backend.luminanceKnown = true;
        backend.sdrWhiteNits = 120.0f;
        backend.minLuminanceNits = 0.05f;
        backend.maxLuminanceNits = 400.0f;
        backend.currentHeadroom = 2.0f;
        backend.potentialHeadroom = 6.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sdrWhiteKnown = true;
        expected.luminanceKnown = true;
        expected.sdrWhiteNits = 120.0f;
        expected.minLuminanceNits = 0.05f;
        expected.maxLuminanceNits = 400.0f;
        expected.currentHeadroom = 5.0f;
        expected.potentialHeadroom = 6.0f;
        expected.effectiveTargetHeadroom = 5.0f;

        QTest::newRow("inactive-windows-hdr-uses-backend") << display << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::WideColorGamut;
        display.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
        display.targetPrimariesKnown = true;
        display.targetPrimaries = displayP3Primaries();
        display.sdrWhiteNits = 160.0f;
        display.minLuminanceNits = 0.1f;
        display.maxLuminanceNits = 400.0f;

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = true;
        backend.sdrWhiteKnown = true;
        backend.sdrWhiteNits = 80.0f;
        backend.currentHeadroom = 4.0f;
        backend.potentialHeadroom = 4.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.targetPrimariesKnown = true;
        expected.targetPrimaries = displayP3Primaries();
        expected.currentHeadroom = 1.0f;
        expected.potentialHeadroom = 1.0f;
        expected.effectiveTargetHeadroom = 1.0f;
        expected.sdrScale = 1.0f;

        QTest::newRow("windows-wide-gamut-is-display-referred") << display << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::HighDynamicRange;
        display.luminanceBehavior = DisplayLuminanceBehavior::SceneReferred;

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = false;
        backend.currentHeadroom = 4.0f;
        backend.potentialHeadroom = 8.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sceneReferred = true;

        QTest::newRow("scene-referred-display-rejects-display-referred-headroom") << display << backend << expected;
    }

    {
        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = true;
        backend.currentHeadroom = 4.0f;
        backend.potentialHeadroom = 8.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sceneReferred = true;
        expected.currentHeadroom = 4.0f;
        expected.potentialHeadroom = 8.0f;
        expected.effectiveTargetHeadroom = 4.0f;

        QTest::newRow("unknown-luminance-uses-component-headroom") << DisplayState{} << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::HighDynamicRange;
        display.luminanceBehavior = DisplayLuminanceBehavior::SceneReferred;
        display.sdrWhiteKnown = true;
        display.sdrWhiteNits = 160.0f;

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = true;
        backend.luminanceKnown = true;
        backend.minLuminanceNits = 0.03f;
        backend.maxLuminanceNits = 640.0f;
        backend.currentHeadroom = 2.0f;
        backend.potentialHeadroom = 9.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sceneReferred = true;
        expected.sdrWhiteKnown = true;
        expected.luminanceKnown = true;
        expected.sdrWhiteNits = 160.0f;
        expected.minLuminanceNits = 0.03f;
        expected.maxLuminanceNits = 640.0f;
        expected.currentHeadroom = 8.0f;
        expected.potentialHeadroom = 9.0f;
        expected.effectiveTargetHeadroom = 4.0f;
        expected.sdrScale = 2.0f;

        QTest::newRow("display-white-and-backend-luminance-combine") << display << backend << expected;
    }

    {
        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.currentHeadroom = 0.5f;
        backend.potentialHeadroom = 0.75f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;

        QTest::newRow("headroom-never-falls-below-one") << DisplayState{} << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::HighDynamicRange;
        display.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
        display.currentHeadroom = 3.5f;
        display.potentialHeadroom = 5.0f;

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = false;
        backend.currentHeadroom = 2.0f;
        backend.potentialHeadroom = 4.0f;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.currentHeadroom = 3.5f;
        expected.potentialHeadroom = 5.0f;
        expected.effectiveTargetHeadroom = 3.5f;

        QTest::newRow("display-referred-edr-with-unknown-sdr-white") << display << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::HighDynamicRange;
        display.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
        display.sdrWhiteKnown = true;
        display.luminanceKnown = true;
        display.sdrWhiteNits = 200.0f;
        display.minLuminanceNits = 0.001f;
        display.maxLuminanceNits = 1000.0f;
        display.currentHeadroom = 5.0f;
        display.potentialHeadroom = 5.0f;

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = false;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sdrWhiteKnown = true;
        expected.luminanceKnown = true;
        expected.sdrWhiteNits = 200.0f;
        expected.minLuminanceNits = 0.001f;
        expected.maxLuminanceNits = 1000.0f;
        expected.currentHeadroom = 5.0f;
        expected.potentialHeadroom = 5.0f;
        expected.effectiveTargetHeadroom = 5.0f;
        expected.sdrScale = 1.0f;

        QTest::newRow("display-referred-reference-white-needs-no-scale") << display << backend << expected;
    }

    {
        DisplayState display;
        display.valid = true;
        display.colorMode = DisplayColorMode::StandardDynamicRange;
        display.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
        display.sdrWhiteKnown = true;
        display.luminanceKnown = true;
        display.sdrWhiteNits = 162.0f;
        display.minLuminanceNits = 0.2f;
        display.maxLuminanceNits = 162.0f;
        display.currentHeadroom = 1.0f;
        display.potentialHeadroom = 1.0f;

        PresentationBackendState backend;
        backend.hdrPresentationActive = true;
        backend.sceneReferred = false;
        backend.useSdrDisplayTargetForHdrPresentation = true;

        PresentationTarget expected;
        expected.hdrPresentationActive = true;
        expected.sdrWhiteKnown = true;
        expected.luminanceKnown = true;
        expected.sdrWhiteNits = 162.0f;
        expected.minLuminanceNits = 0.2f;
        expected.maxLuminanceNits = 162.0f;
        expected.currentHeadroom = 1.0f;
        expected.potentialHeadroom = 1.0f;
        expected.effectiveTargetHeadroom = 1.0f;
        expected.sdrScale = 1.0f;

        QTest::newRow("stable-hdr-surface-on-sdr-output") << display << backend << expected;
    }
}

void PresentationTargetTest::calculation() {
    QFETCH(DisplayState, display);
    QFETCH(PresentationBackendState, backend);
    QFETCH(PresentationTarget, expected);

    PresentationTarget const actual = calculatePresentationTarget(display, backend);

    QCOMPARE(actual.hdrPresentationActive, expected.hdrPresentationActive);
    QCOMPARE(actual.sceneReferred, expected.sceneReferred);
    QCOMPARE(actual.targetPrimariesKnown, expected.targetPrimariesKnown);
    QVERIFY(actual.targetPrimaries == expected.targetPrimaries);
    QCOMPARE(actual.sdrWhiteKnown, expected.sdrWhiteKnown);
    QCOMPARE(actual.luminanceKnown, expected.luminanceKnown);
    QCOMPARE(actual.sdrWhiteNits, expected.sdrWhiteNits);
    QCOMPARE(actual.minLuminanceNits, expected.minLuminanceNits);
    QCOMPARE(actual.maxLuminanceNits, expected.maxLuminanceNits);
    QCOMPARE(actual.currentHeadroom, expected.currentHeadroom);
    QCOMPARE(actual.potentialHeadroom, expected.potentialHeadroom);
    QCOMPARE(actual.effectiveTargetHeadroom, expected.effectiveTargetHeadroom);
    QCOMPARE(actual.sdrScale, expected.sdrScale);
}

void PresentationTargetTest::automaticPhysicalTargetEligibility() {
    PresentationTarget target;
    target.sceneReferred = true;
    target.luminanceKnown = true;
    target.maxLuminanceNits = 600.0f;

    QVERIFY(canUseAutomaticPhysicalTarget(target, true));
    QVERIFY(!canUseAutomaticPhysicalTarget(target, false));

    target.sceneReferred = false;
    QVERIFY(!canUseAutomaticPhysicalTarget(target, true));

    target.sceneReferred = true;
    target.luminanceKnown = false;
    QVERIFY(!canUseAutomaticPhysicalTarget(target, true));

    target.luminanceKnown = true;
    target.maxLuminanceNits = 79.0f;
    QVERIFY(!canUseAutomaticPhysicalTarget(target, true));

    target.sdrWhiteKnown = true;
    target.sdrWhiteNits = 200.0f;
    target.maxLuminanceNits = 199.0f;
    QVERIFY(!canUseAutomaticPhysicalTarget(target, true));

    target.maxLuminanceNits = 200.0f;
    QVERIFY(canUseAutomaticPhysicalTarget(target, true));
}

QTEST_APPLESS_MAIN(PresentationTargetTest)
#include "tst_PresentationTarget.moc"
