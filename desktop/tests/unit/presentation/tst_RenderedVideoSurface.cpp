#include <limits>

#include <QtTest>

#include "presentation/RenderedVideoSurface.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
RenderedVideoSurfaceDescription canonicalDescription() {
    RenderedVideoSurfaceDescription description;
    description.pixelSize = {1920, 1080};
    description.pixelFormat = RenderedVideoPixelFormat::Rgba16Float;
    description.colorSpace = RenderedVideoColorSpace::LinearSrgb;
    description.luminance =
        RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative;
    description.alphaMode = RenderedVideoAlphaMode::Opaque;
    description.referenceWhiteNits = 203.0f;
    description.targetPeakHeadroom = 5.0f;
    return description;
}

RenderedVideoSurfaceState canonicalState() {
    RenderedVideoSurfaceState state;
    state.description = canonicalDescription();
    state.graphicsDeviceGeneration = 7;
    state.displayTargetRevision = 11;
    state.contentRevision = 13;
    return state;
}
}

class RenderedVideoSurfaceTest final : public QObject {
    Q_OBJECT

public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(
            SEM_FAILCRITICALERRORS
            | SEM_NOGPFAULTERRORBOX
            | SEM_NOOPENFILEERRORBOX);
#endif
    }

private slots:
    void canonicalDescriptionIsValid();
    void descriptionRequiresCompleteSemantics();
    void descriptionRequiresFiniteLuminance();
    void stateRequiresNonzeroRevisions();
    void identicalStateIsReusable();
    void lifecycleAndContentChangesInvalidate();
    void semanticChangesInvalidate();
    void swapchainOnlyRecreationPreservesReuse();
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
    for (const float invalid : {
             -1.0f,
             0.0f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
         }) {
        auto description = canonicalDescription();
        description.referenceWhiteNits = invalid;
        QVERIFY(!description.isValid());
    }

    for (const float invalid : {
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

void RenderedVideoSurfaceTest::stateRequiresNonzeroRevisions() {
    {
        auto state = canonicalState();
        state.graphicsDeviceGeneration = 0;
        QVERIFY(!state.isValid());
    }
    {
        auto state = canonicalState();
        state.displayTargetRevision = 0;
        QVERIFY(!state.isValid());
    }
    {
        auto state = canonicalState();
        state.contentRevision = 0;
        QVERIFY(!state.isValid());
    }
}

void RenderedVideoSurfaceTest::identicalStateIsReusable() {
    const auto completed = canonicalState();
    const auto requested = canonicalState();
    QVERIFY(completed.isReusableFor(requested));
}

void RenderedVideoSurfaceTest::lifecycleAndContentChangesInvalidate() {
    const auto completed = canonicalState();

    {
        auto requested = canonicalState();
        ++requested.graphicsDeviceGeneration;
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        ++requested.displayTargetRevision;
        QVERIFY(!completed.isReusableFor(requested));
    }
    {
        auto requested = canonicalState();
        ++requested.contentRevision;
        QVERIFY(!completed.isReusableFor(requested));
    }
}

void RenderedVideoSurfaceTest::semanticChangesInvalidate() {
    const auto completed = canonicalState();

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
        auto requested = canonicalState();
        requested.description.targetPeakHeadroom = 3.0f;
        QVERIFY(!completed.isReusableFor(requested));
    }
}

void RenderedVideoSurfaceTest::swapchainOnlyRecreationPreservesReuse() {
    const auto beforeSwapchainRecreation = canonicalState();
    const auto afterSwapchainRecreation = canonicalState();

    // A swapchain identity is deliberately not part of the surface key.
    QVERIFY(beforeSwapchainRecreation.isReusableFor(
        afterSwapchainRecreation));
}

QTEST_APPLESS_MAIN(RenderedVideoSurfaceTest)
#include "tst_RenderedVideoSurface.moc"
