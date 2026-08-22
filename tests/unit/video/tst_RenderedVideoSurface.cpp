#include <limits>

#include <QtTest>

#include "video/RenderedVideoSurface.h"
#include "video/VideoTargetInterop.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
RenderedVideoSurfaceDescription canonicalDescription() {
    RenderedVideoSurfaceDescription description;
    description.pixelSize = {1920, 1080};
    description.pixelFormat = RenderedVideoPixelFormat::Rgba16Float;
    description.colorSpace = RenderedVideoColorSpace::LinearSrgb;
    description.luminance = RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative;
    description.alphaMode = RenderedVideoAlphaMode::Opaque;
    description.referenceWhiteNits = 203.0f;
    description.targetMinimumLuminanceKnown = true;
    description.targetMinimumLuminanceNits = 0.005f;
    description.targetPeakLuminanceKnown = true;
    description.targetPeakHeadroom = 5.0f;
    return description;
}

RenderedVideoSurfaceState canonicalState() {
    RenderedVideoSurfaceState state;
    state.description = canonicalDescription();
    state.graphicsDeviceGeneration = 7;
    state.contentRevision = 13;
    return state;
}

ColorPrimaries displayP3Primaries() {
    return {
        .red = {0.680f, 0.320f},
        .green = {0.265f, 0.690f},
        .blue = {0.150f, 0.060f},
        .white = {0.3127f, 0.3290f},
    };
}
} // namespace

class RenderedVideoSurfaceTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void canonicalDescriptionIsValid();
    void descriptionRequiresCompleteSemantics();
    void descriptionRequiresFiniteLuminance();
    void descriptionRequiresValidTargetPrimaries();
    void stateRequiresNonzeroIdentities();
    void equalSemanticStateIsReusableAcrossNativeChanges();
    void lifecycleAndContentChangesInvalidate();
    void semanticChangesInvalidate();
    void unavailableTargetDiagnosticsRequireReason();
    void directTargetDiagnosticsRequireNoCopies();
    void fallbackDiagnosticsRequireObservableCostsAndReason();
};

void RenderedVideoSurfaceTest::canonicalDescriptionIsValid() {
    QVERIFY(canonicalDescription().isValid());
    QVERIFY(canonicalState().isValid());
}

void RenderedVideoSurfaceTest::descriptionRequiresCompleteSemantics() {
    {
        auto description = canonicalDescription();
        description.pixelSize = {};
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.pixelSize = {-1, 1080};
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.pixelFormat = RenderedVideoPixelFormat::Unknown;
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.colorSpace = RenderedVideoColorSpace::Unknown;
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.luminance = RenderedVideoLuminance::Unknown;
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.alphaMode = RenderedVideoAlphaMode::Unknown;
        QVERIFY(!description.isValid());
    }
}

void RenderedVideoSurfaceTest::descriptionRequiresFiniteLuminance() {
    for (float const invalid : {
             -1.0f,
             0.0f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
         }) {
        auto description = canonicalDescription();
        description.referenceWhiteNits = invalid;
        QVERIFY(!description.isValid());
    }

    for (float const invalid : {
             -1.0f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
         }) {
        auto description = canonicalDescription();
        description.targetMinimumLuminanceNits = invalid;
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.targetMinimumLuminanceKnown = false;
        description.targetMinimumLuminanceNits = 0.0f;
        QVERIFY(description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.targetMinimumLuminanceNits = 0.0f;
        QVERIFY(description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.targetMinimumLuminanceKnown = false;
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.targetMinimumLuminanceNits = description.referenceWhiteNits * description.targetPeakHeadroom + 1.0f;
        QVERIFY(!description.isValid());
    }

    for (float const invalid : {
             -1.0f,
             0.0f,
             0.99f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
         }) {
        auto description = canonicalDescription();
        description.targetPeakHeadroom = invalid;
        QVERIFY(!description.isValid());
    }
}

void RenderedVideoSurfaceTest::descriptionRequiresValidTargetPrimaries() {
    {
        auto description = canonicalDescription();
        description.targetPrimariesKnown = true;
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.targetPrimariesKnown = true;
        description.targetPrimaries = displayP3Primaries();
        QVERIFY(description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.targetPrimaries = displayP3Primaries();
        QVERIFY(!description.isValid());
    }
    {
        auto description = canonicalDescription();
        description.targetPrimariesKnown = true;
        description.targetPrimaries = displayP3Primaries();
        description.targetPrimaries.white = {0.9f, 0.05f};
        QVERIFY(!description.isValid());
    }
}

void RenderedVideoSurfaceTest::stateRequiresNonzeroIdentities() {
    {
        auto state = canonicalState();
        state.graphicsDeviceGeneration = 0;
        QVERIFY(!state.isValid());
    }
    {
        auto state = canonicalState();
        state.contentRevision = 0;
        QVERIFY(!state.isValid());
    }
}

void RenderedVideoSurfaceTest::equalSemanticStateIsReusableAcrossNativeChanges() {
    auto const beforeNativeOutputChange = canonicalState();
    auto const afterNativeOutputChange = canonicalState();

    // Native output and swapchain identities are deliberately absent. Equal
    // semantic targets reuse the device-owned video surface.
    QVERIFY(beforeNativeOutputChange.isReusableFor(afterNativeOutputChange));
}

void RenderedVideoSurfaceTest::lifecycleAndContentChangesInvalidate() {
    auto const completed = canonicalState();

    {
        auto requested = canonicalState();
        ++requested.graphicsDeviceGeneration;
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        ++requested.contentRevision;
        QVERIFY(!completed.isReusableFor(requested));
    }
}

void RenderedVideoSurfaceTest::semanticChangesInvalidate() {
    auto const completed = canonicalState();

    {
        auto requested = canonicalState();
        requested.description.pixelSize = {1280, 720};
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        requested.description.referenceWhiteNits = 100.0f;
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto completedAtKnownZero = canonicalState();
        completedAtKnownZero.description.targetMinimumLuminanceNits = 0.0f;
        auto requested = completedAtKnownZero;
        requested.description.targetMinimumLuminanceKnown = false;
        QVERIFY(completedAtKnownZero.isValid());
        QVERIFY(requested.isValid());
        QVERIFY(!completedAtKnownZero.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        requested.description.targetMinimumLuminanceNits = 0.01f;
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        requested.description.targetPeakLuminanceKnown = false;
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        requested.description.targetPeakHeadroom = 3.0f;
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        requested.description.targetPrimariesKnown = true;
        requested.description.targetPrimaries = displayP3Primaries();
        QVERIFY(requested.isValid());
        QVERIFY(!completed.isReusableFor(requested));
    }
}

void RenderedVideoSurfaceTest::unavailableTargetDiagnosticsRequireReason() {
    VideoTargetInteropDiagnostics diagnostics;
    diagnostics.synchronizationMode = QStringLiteral("Not active");
    QVERIFY(!diagnostics.isValid());
    diagnostics.fallbackReason = QStringLiteral("Target not provisioned");
    QVERIFY(diagnostics.isValid());
    QCOMPARE(videoOutputPathName(diagnostics.outputPath), QStringLiteral("Unavailable"));
}

void RenderedVideoSurfaceTest::directTargetDiagnosticsRequireNoCopies() {
    VideoTargetInteropDiagnostics diagnostics;
    diagnostics.outputPath = VideoOutputPath::DirectRenderTarget;
    diagnostics.synchronizationMode = QStringLiteral("QRhi command-buffer ordering");
    QVERIFY(diagnostics.isValid());
    QCOMPARE(videoOutputPathName(diagnostics.outputPath), QStringLiteral("Direct render target"));

    diagnostics.knownOutputGpuCopiesPerRender = 1;
    QVERIFY(!diagnostics.isValid());
    diagnostics.knownOutputGpuCopiesPerRender = 0;
    diagnostics.fallbackReason = QStringLiteral("Unexpected");
    QVERIFY(!diagnostics.isValid());
}

void RenderedVideoSurfaceTest::fallbackDiagnosticsRequireObservableCostsAndReason() {
    VideoTargetInteropDiagnostics diagnostics;
    diagnostics.outputPath = VideoOutputPath::SameDeviceGpuCopy;
    diagnostics.synchronizationMode = QStringLiteral("Backend fence");
    QVERIFY(!diagnostics.isValid());
    diagnostics.knownOutputGpuCopiesPerRender = 1;
    QVERIFY(!diagnostics.isValid());
    diagnostics.fallbackReason = QStringLiteral("Direct target format unavailable");
    QVERIFY(diagnostics.isValid());

    diagnostics.outputPath = VideoOutputPath::CpuRoundTrip;
    diagnostics.knownOutputGpuCopiesPerRender = 0;
    diagnostics.knownOutputCpuTransfersPerRender = 1;
    QVERIFY(!diagnostics.isValid());
    diagnostics.knownOutputCpuTransfersPerRender = 2;
    QVERIFY(diagnostics.isValid());
    QCOMPARE(videoOutputPathName(diagnostics.outputPath), QStringLiteral("CPU round trip"));
}

QTEST_APPLESS_MAIN(RenderedVideoSurfaceTest)
#include "tst_RenderedVideoSurface.moc"
