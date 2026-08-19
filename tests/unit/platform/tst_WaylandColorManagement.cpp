#include <QtTest>

#include "platform/linux/WaylandColorManagement.h"

class WaylandColorManagementTest final : public QObject {
    Q_OBJECT

  private slots:
    void incompleteManagedContractUsesUnmanagedSrgb_data();
    void incompleteManagedContractUsesUnmanagedSrgb();
    void completeManagedContractUsesGamma22();
    void hdr10CapabilitiesChooseStableHdr();
    void completePreferredDescriptionPublishesDisplayState();
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
    QVERIFY(capabilities.supportsManagedHdr10());
    WaylandSurfaceSelection const selection = selectWaylandSurface(capabilities);
    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::ManagedGamma22);
    QVERIFY(selection.presentationContract().hdr10Required());
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
