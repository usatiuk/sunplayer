#include <QtTest>
#include <libplacebo/colorspace.h>

#include "platform/linux/WaylandColorManagement.h"

class WaylandColorManagementTest final : public QObject {
    Q_OBJECT

  private slots:
    void displaySdrUsesExplicitPrimariesWithoutHdr();
    void displaySdrRejectionIsBounded();
    void incompleteManagedContractUsesUnmanagedSrgb_data();
    void incompleteManagedContractUsesUnmanagedSrgb();
    void completeManagedContractUsesGamma22();
    void hdrSelectionFollowsDisplay_data();
    void hdrSelectionFollowsDisplay();
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
    auto const selection = selectWaylandSurface(capabilities);
    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::UnmanagedSrgb);
    QVERIFY(!selection.diagnostic.isEmpty());
}

void WaylandColorManagementTest::completeManagedContractUsesGamma22() {
    auto const selection = selectWaylandSurface(completeManagedSdrCapabilities());
    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::ManagedGamma22);
}

void WaylandColorManagementTest::displaySdrUsesExplicitPrimariesWithoutHdr() {
    auto capabilities = completeManagedSdrCapabilities();
    capabilities.explicitPrimaries = true;
    capabilities.explicitLuminances = true;
    auto description = completeHdrDescription();
    description.referenceWhiteNits = description.targetMaximumLuminanceNits;
    description.targetPrimariesKnown = true;
    description.targetPrimaries = displayP3Primaries();
    auto const display = displayStateFromWaylandDescription(description);
    QVERIFY(display);
    QCOMPARE(display->colorMode, DisplayColorMode::WideColorGamut);
    QCOMPARE(display->targetPrimaries, displayP3Primaries());
    auto const select = [&] {
        return selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, *display, 7, {});
    };
    QCOMPARE(select(), WaylandPresentationMode::ManagedDisplaySdr);
    capabilities.explicitLuminances = false;
    QCOMPARE(select(), WaylandPresentationMode::ManagedGamma22Sdr);
    capabilities.explicitLuminances = true;
    capabilities.explicitPrimaries = false;
    QCOMPARE(select(), WaylandPresentationMode::ManagedGamma22Sdr);
}

void WaylandColorManagementTest::displaySdrRejectionIsBounded() {
    auto capabilities = completeManagedSdrCapabilities();
    capabilities.explicitPrimaries = capabilities.explicitLuminances = true;
    capabilities.namedBt2020Primaries = capabilities.pqTransfer = true;
    auto display = *displayStateFromWaylandDescription(completeHdrDescription());
    WaylandSurfaceRejection rejected{.graphicsDeviceGeneration = 7, .displaySdr = true};
    auto const select = [&](std::uint64_t generation) {
        return selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, display, generation,
                                             rejected);
    };
    QCOMPARE(select(7), WaylandPresentationMode::ManagedHdr10Pq);
    display.colorMode = DisplayColorMode::WideColorGamut;
    QCOMPARE(select(7), WaylandPresentationMode::ManagedGamma22Sdr);
    QCOMPARE(select(8), WaylandPresentationMode::ManagedDisplaySdr);
    display.colorMode = DisplayColorMode::HighDynamicRange;
    rejected.hdr10 = true;
    QCOMPARE(select(7), WaylandPresentationMode::ManagedGamma22Sdr);
    QCOMPARE(select(8), WaylandPresentationMode::ManagedHdr10Pq);
}

void WaylandColorManagementTest::hdrSelectionFollowsDisplay_data() {
    QTest::addColumn<bool>("masteringSupported");
    QTest::newRow("optional-mastering-absent") << false;
    QTest::newRow("optional-mastering-present") << true;
}

void WaylandColorManagementTest::hdrSelectionFollowsDisplay() {
    QFETCH(bool, masteringSupported);
    auto capabilities = completeManagedSdrCapabilities();
    capabilities.masteringDisplayPrimaries = masteringSupported;
    capabilities.explicitLuminances = capabilities.explicitPrimaries = true;
    capabilities.pqTransfer = capabilities.namedBt2020Primaries = true;
    QVERIFY(capabilities.supportsManagedHdr10());
    auto display = *displayStateFromWaylandDescription(completeHdrDescription());
    auto const select = [&] {
        return selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, display, 7, {});
    };
    QCOMPARE(select(), WaylandPresentationMode::ManagedHdr10Pq);
    for (auto mode : {DisplayColorMode::StandardDynamicRange, DisplayColorMode::WideColorGamut}) {
        display.colorMode = mode;
        QCOMPARE(select(), WaylandPresentationMode::ManagedDisplaySdr);
    }
    display.valid = false;
    QCOMPARE(select(), WaylandPresentationMode::ManagedGamma22Sdr);
    display.valid = true;
    display.colorMode = DisplayColorMode::HighDynamicRange;
    QCOMPARE(select(), WaylandPresentationMode::ManagedHdr10Pq);
    capabilities.pqTransfer = false;
    QCOMPARE(select(), WaylandPresentationMode::ManagedDisplaySdr);
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
    description.targetMinimumLuminanceNits = description.referenceWhiteNits;
    QVERIFY(!displayStateFromWaylandDescription(description).has_value());

    description = completeHdrDescription();
    description.targetMaximumLuminanceNits = description.targetMinimumLuminanceNits;
    QVERIFY(!displayStateFromWaylandDescription(description).has_value());
}

void WaylandColorManagementTest::presentationModeTracksCapabilityAndBoundedRejection() {
    auto capabilities = completeManagedSdrCapabilities();
    auto const display = *displayStateFromWaylandDescription(completeHdrDescription());
    auto const select = [&](std::uint64_t generation, WaylandSurfaceRejection rejection = {}) {
        return selectWaylandPresentationMode(WaylandSdrSurfaceMode::ManagedGamma22, capabilities, display, generation,
                                             rejection);
    };
    QCOMPARE(select(7), WaylandPresentationMode::ManagedGamma22Sdr);
    capabilities.namedBt2020Primaries = capabilities.pqTransfer = capabilities.explicitLuminances = true;
    QCOMPARE(select(7), WaylandPresentationMode::ManagedHdr10Pq);
    WaylandSurfaceRejection rejection{.graphicsDeviceGeneration = 7, .hdr10 = true};
    QCOMPARE(select(7, rejection), WaylandPresentationMode::ManagedGamma22Sdr);
    QCOMPARE(select(8, rejection), WaylandPresentationMode::ManagedHdr10Pq);
    QCOMPARE(selectWaylandPresentationMode(WaylandSdrSurfaceMode::UnmanagedSrgb, capabilities, display, 7, {}),
             WaylandPresentationMode::UnmanagedSrgb);
    PresentationSurfaceContract pq{.encoding = PresentationOutputEncoding::Bt2020Pq};
    QCOMPARE(pq.primaries(), bt2020ColorPrimaries);
    QCOMPARE(pq.constrainTargetHeadroom(5.0f), 5.0f);
    QCOMPARE(pq.constrainTargetHeadroom(100.0f), PresentationSurfaceContract::pqMaximumHeadroom);
    PresentationSurfaceContract sdr{.encoding = PresentationOutputEncoding::Gamma22,
                                    .sdrPrimaries = displayP3Primaries()};
    QCOMPARE(sdr.primaries(), displayP3Primaries());
    QCOMPARE(sdr.constrainTargetHeadroom(5.0f), 1.0f);
}

QTEST_APPLESS_MAIN(WaylandColorManagementTest)
#include "tst_WaylandColorManagement.moc"
