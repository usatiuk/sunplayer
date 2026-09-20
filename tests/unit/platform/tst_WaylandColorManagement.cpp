#include <QtTest>
#include <libplacebo/colorspace.h>

#include "platform/linux/WaylandColorManagement.h"

class WaylandColorManagementTest final : public QObject {
    Q_OBJECT

  private slots:
    void incompleteManagedContractUsesUnmanagedSrgb_data();
    void incompleteManagedContractUsesUnmanagedSrgb();
    void completeManagedContractUsesGamma22();
    void hdr10CapabilitiesChooseStableHdr();
    void composedVolumeCoversOverlaysAndEncodedPeak();
    void completePreferredDescriptionPublishesDisplayState();
    void explicitTargetPrimariesArePublished();
    void referenceWhiteEqualsTargetMaximumIsSdr();
    void incompleteOrInvalidPreferredDescriptionIsRejected();
    void presentationModeTracksCapabilityAndBoundedRejection();
};

namespace {
WaylandColorManagementCapabilities completeManagedSdrCapabilities() {
    return {
        .protocolAdvertised = true,
        .protocolVersion = WaylandColorManagementCapabilities::requiredProtocolVersion,
        .inventoryComplete = true,
        .parametricDescriptions = true,
        .perceptualIntent = true,
        .namedSrgbPrimaries = true,
        .gamma22Transfer = true,
    };
}

WaylandColorPrimaries srgbPrimaries() {
    return {
        .red = {.x = 0.64f, .y = 0.33f},
        .green = {.x = 0.30f, .y = 0.60f},
        .blue = {.x = 0.15f, .y = 0.06f},
        .white = {.x = 0.3127f, .y = 0.3290f},
    };
}

WaylandColorPrimaries displayP3Primaries() {
    return {
        .red = {.x = 0.680f, .y = 0.320f},
        .green = {.x = 0.265f, .y = 0.690f},
        .blue = {.x = 0.150f, .y = 0.060f},
        .white = {.x = 0.3127f, .y = 0.3290f},
    };
}

WaylandPreferredDescription completeHdrDescription() {
    return {
        .parametric = true,
        .primariesKnown = true,
        .primaries = srgbPrimaries(),
        .transferFunction = WaylandTransferFunction::Pq,
        .luminancesKnown = true,
        .minimumLuminanceNits = 0.005f,
        .maximumLuminanceNits = 1000.0f,
        .referenceWhiteNits = 200.0f,
        .targetLuminanceKnown = true,
        .targetMinimumLuminanceNits = 0.001f,
        .targetMaximumLuminanceNits = 1000.0f,
    };
}
} // namespace

void WaylandColorManagementTest::incompleteManagedContractUsesUnmanagedSrgb_data() {
    QTest::addColumn<WaylandColorManagementCapabilities>("capabilities");

    QTest::newRow("global-absent") << WaylandColorManagementCapabilities{};

    auto capabilities = completeManagedSdrCapabilities();
    capabilities.protocolVersion = 1;
    QTest::newRow("older-protocol-version") << capabilities;

    capabilities = completeManagedSdrCapabilities();
    capabilities.inventoryComplete = false;
    QTest::newRow("inventory-incomplete") << capabilities;

    capabilities = completeManagedSdrCapabilities();
    capabilities.parametricDescriptions = false;
    QTest::newRow("parametric-absent") << capabilities;

    capabilities = completeManagedSdrCapabilities();
    capabilities.perceptualIntent = false;
    QTest::newRow("perceptual-absent") << capabilities;

    capabilities = completeManagedSdrCapabilities();
    capabilities.namedSrgbPrimaries = false;
    QTest::newRow("named-srgb-absent") << capabilities;

    capabilities = completeManagedSdrCapabilities();
    capabilities.gamma22Transfer = false;
    QTest::newRow("gamma22-absent") << capabilities;
}

void WaylandColorManagementTest::incompleteManagedContractUsesUnmanagedSrgb() {
    QFETCH(WaylandColorManagementCapabilities, capabilities);

    WaylandSurfaceSelection const selection = selectWaylandSurface(capabilities);

    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::UnmanagedSrgb);
    PresentationSurfaceContract const presentation = selection.presentationContract();
    QCOMPARE(presentation.outputEncoding(false), PresentationOutputEncoding::Srgb);
    QVERIFY(!presentation.hdr10Required());
    QVERIFY(!selection.diagnostic.isEmpty());
}

void WaylandColorManagementTest::completeManagedContractUsesGamma22() {
    WaylandSurfaceSelection const selection = selectWaylandSurface(completeManagedSdrCapabilities());

    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::ManagedGamma22);
    PresentationSurfaceContract const presentation = selection.presentationContract();
    QCOMPARE(presentation.outputEncoding(false), PresentationOutputEncoding::Gamma22Srgb);
    QVERIFY(!presentation.hdr10Required());
}

void WaylandColorManagementTest::hdr10CapabilitiesChooseStableHdr() {
    auto capabilities = completeManagedSdrCapabilities();
    capabilities.namedBt2020Primaries = false;
    QVERIFY(!capabilities.supportsManagedHdr10());
    capabilities.namedBt2020Primaries = true;
    capabilities.pqTransfer = false;
    QVERIFY(!capabilities.supportsManagedHdr10());
    capabilities.pqTransfer = true;
    capabilities.masteringDisplayPrimaries = true;
    QVERIFY(capabilities.supportsManagedHdr10());
    WaylandSurfaceSelection const selection = selectWaylandSurface(capabilities);
    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::ManagedGamma22);
    QVERIFY(selection.presentationContract().hdr10Required());
}

void WaylandColorManagementTest::composedVolumeCoversOverlaysAndEncodedPeak() {
    auto const srgb = srgbPrimaries();
    auto const p3 = displayP3Primaries();
    QCOMPARE(waylandCompositionVolume(1.0f, srgb).maximumNits, 203U);
    QCOMPARE(waylandCompositionVolume(1.0001f, srgb).maximumNits, 204U);
    QCOMPARE(waylandCompositionVolume(6.0f, srgb).maximumNits, 1218U);
    QCOMPARE(waylandCompositionVolume(PresentationSurfaceContract::pqMaximumHeadroom, p3).maximumNits, 10000U);
    QCOMPARE(waylandCompositionVolume(1.0f, {}).primaries, srgb);
    auto const p3Volume = waylandCompositionVolume(4.0f, p3);
    QCOMPARE(p3Volume.primaries.red.x, 0.708f);
    QVERIFY(p3Volume.maximumNits > 812U);
    QVERIFY(p3Volume.maximumNits < 1000U);
    auto const matrix =
        pl_get_color_mapping_matrix(pl_raw_primaries_get(PL_COLOR_PRIM_DISPLAY_P3),
                                    pl_raw_primaries_get(PL_COLOR_PRIM_BT_2020), PL_INTENT_RELATIVE_COLORIMETRIC);
    // Check transformed cube corners, not a duplicate of the bound formula.
    for (int corner = 0; corner < 8; ++corner) {
        float rgb[]{(corner & 1) ? 4.0f : 0.0f, (corner & 2) ? 4.0f : 0.0f, (corner & 4) ? 4.0f : 0.0f};
        pl_matrix3x3_apply(&matrix, rgb);
        for (float component : rgb) {
            QVERIFY(component * 203.0f <= p3Volume.maximumNits);
        }
    }
    auto narrow = srgb;
    narrow.red = {0.55f, 0.33f};
    QVERIFY(narrow.isValid());
    auto const conservative = waylandCompositionVolume(4.0f, narrow);
    QCOMPARE(conservative.primaries.red.x, 0.708f);
    QVERIFY(conservative.maximumNits >= 812U);
    auto differentWhite = p3;
    differentWhite.white = {0.3457f, 0.3585f};
    QCOMPARE(waylandCompositionVolume(4.0f, differentWhite).primaries, conservative.primaries);
    QCOMPARE(waylandCompositionVolume(4.0f, differentWhite).maximumNits, 10000U);
    auto capabilities = completeManagedSdrCapabilities();
    capabilities.namedBt2020Primaries = true;
    capabilities.pqTransfer = true;
    QVERIFY(!capabilities.supportsManagedHdr10());
    QCOMPARE(selectWaylandSurface(capabilities).presentationContract().mode,
             PresentationSurfaceMode::ManagedGamma22Sdr);
}

void WaylandColorManagementTest::completePreferredDescriptionPublishesDisplayState() {
    auto const display = displayStateFromWaylandDescription(completeHdrDescription());

    QVERIFY(display.has_value());
    QVERIFY(display->valid);
    QCOMPARE(display->colorMode, DisplayColorMode::HighDynamicRange);
    QCOMPARE(display->luminanceBehavior, DisplayLuminanceBehavior::DisplayReferred);
    QVERIFY(display->sdrWhiteKnown);
    QVERIFY(display->luminanceKnown);
    QCOMPARE(display->sdrWhiteNits, 200.0f);
    QCOMPARE(display->minLuminanceNits, 0.001f);
    QCOMPARE(display->maxLuminanceNits, 1000.0f);
    QCOMPARE(display->currentHeadroom, 5.0f);
    QCOMPARE(display->potentialHeadroom, 5.0f);
    QVERIFY(display->targetPrimariesKnown);
    QVERIFY(display->targetPrimaries == srgbPrimaries());
}

void WaylandColorManagementTest::explicitTargetPrimariesArePublished() {
    auto description = completeHdrDescription();
    description.targetPrimariesKnown = true;
    description.targetPrimaries = displayP3Primaries();

    auto const display = displayStateFromWaylandDescription(description);

    QVERIFY(display.has_value());
    QVERIFY(display->targetPrimariesKnown);
    QVERIFY(display->targetPrimaries == displayP3Primaries());
}

void WaylandColorManagementTest::referenceWhiteEqualsTargetMaximumIsSdr() {
    auto description = completeHdrDescription();
    description.targetMaximumLuminanceNits = description.referenceWhiteNits;

    auto const display = displayStateFromWaylandDescription(description);

    QVERIFY(display.has_value());
    QVERIFY(display->valid);
    QCOMPARE(display->colorMode, DisplayColorMode::StandardDynamicRange);
    QCOMPARE(display->currentHeadroom, 1.0f);
    QCOMPARE(display->potentialHeadroom, 1.0f);
}

void WaylandColorManagementTest::incompleteOrInvalidPreferredDescriptionIsRejected() {
    auto description = completeHdrDescription();
    description.parametric = false;
    QVERIFY(!displayStateFromWaylandDescription(description).has_value());

    description = completeHdrDescription();
    description.primaries.green = description.primaries.red;
    description.primaries.blue = description.primaries.red;
    QVERIFY(!displayStateFromWaylandDescription(description).has_value());

    description = completeHdrDescription();
    description.referenceWhiteNits = description.minimumLuminanceNits;
    QVERIFY(!displayStateFromWaylandDescription(description).has_value());

    description = completeHdrDescription();
    description.targetMaximumLuminanceNits = description.targetMinimumLuminanceNits;
    QVERIFY(!displayStateFromWaylandDescription(description).has_value());
}

void WaylandColorManagementTest::presentationModeTracksCapabilityAndBoundedRejection() {
    auto capabilities = completeManagedSdrCapabilities();
    QCOMPARE(selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, 7, std::nullopt),
             PresentationSurfaceMode::ManagedGamma22Sdr);

    capabilities.namedBt2020Primaries = true;
    capabilities.pqTransfer = true;
    capabilities.masteringDisplayPrimaries = true;
    QCOMPARE(selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, 7, std::nullopt),
             PresentationSurfaceMode::ManagedHdr10Pq);
    PresentationSurfaceContract const hdr10Contract{
        .mode = PresentationSurfaceMode::ManagedHdr10Pq,
    };
    QVERIFY(hdr10Contract.hdr10Required());
    QCOMPARE(hdr10Contract.outputEncoding(false), PresentationOutputEncoding::Bt2020Pq);
    QCOMPARE(hdr10Contract.constrainTargetHeadroom(1.0f), 1.0f);
    QCOMPARE(hdr10Contract.constrainTargetHeadroom(5.0f), 5.0f);
    QCOMPARE(hdr10Contract.constrainTargetHeadroom(10000.0f / 162.0f), PresentationSurfaceContract::pqMaximumHeadroom);
    QCOMPARE(hdr10Contract.constrainTargetHeadroom(PresentationSurfaceContract::pqMaximumHeadroom),
             PresentationSurfaceContract::pqMaximumHeadroom);

    PresentationSurfaceContract const managedSdrContract{
        .mode = PresentationSurfaceMode::ManagedGamma22Sdr,
    };
    QCOMPARE(managedSdrContract.constrainTargetHeadroom(5.0f), 1.0f);

    WaylandHdrRejection const rejection{
        .graphicsDeviceGeneration = 7,
    };
    QCOMPARE(selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, 7, rejection),
             PresentationSurfaceMode::ManagedGamma22Sdr);
    QCOMPARE(selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, 8, rejection),
             PresentationSurfaceMode::ManagedHdr10Pq);

    QCOMPARE(selectWaylandPresentationMode(WaylandSdrSurfaceMode::UnmanagedSrgb, capabilities, 7, std::nullopt),
             PresentationSurfaceMode::UnmanagedSrgb);
}

QTEST_APPLESS_MAIN(WaylandColorManagementTest)
#include "tst_WaylandColorManagement.moc"
