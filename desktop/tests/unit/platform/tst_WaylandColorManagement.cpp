#include <QtTest>

#include "platform/linux/WaylandColorManagement.h"

class WaylandColorManagementTest final : public QObject {
    Q_OBJECT

private slots:
    void incompleteManagedContractUsesUnmanagedSrgb_data();
    void incompleteManagedContractUsesUnmanagedSrgb();
    void completeManagedContractUsesGamma22();
    void extendedLinearIsObservedWithoutPrematureHdrActivation();
};

namespace {
WaylandColorManagementCapabilities completeManagedSdrCapabilities() {
    return {
        .protocolAdvertised = true,
        .inventoryComplete = true,
        .parametricDescriptions = true,
        .perceptualIntent = true,
        .namedSrgbPrimaries = true,
        .gamma22Transfer = true,
    };
}
}

void WaylandColorManagementTest::
incompleteManagedContractUsesUnmanagedSrgb_data() {
    QTest::addColumn<WaylandColorManagementCapabilities>("capabilities");

    QTest::newRow("global-absent")
        << WaylandColorManagementCapabilities{};

    auto capabilities = completeManagedSdrCapabilities();
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

void WaylandColorManagementTest::
incompleteManagedContractUsesUnmanagedSrgb() {
    QFETCH(WaylandColorManagementCapabilities, capabilities);

    const WaylandSdrSurfaceSelection selection =
        selectWaylandSdrSurface(capabilities);

    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::UnmanagedSrgb);
    const PresentationSurfaceContract presentation =
        selection.presentationContract();
    QCOMPARE(
        presentation.sdrTransfer,
        PresentationOutputTransfer::PiecewiseSrgb);
    QVERIFY(!presentation.extendedLinearAllowed);
    QVERIFY(!selection.diagnostic.isEmpty());
}

void WaylandColorManagementTest::completeManagedContractUsesGamma22() {
    const WaylandSdrSurfaceSelection selection =
        selectWaylandSdrSurface(completeManagedSdrCapabilities());

    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::ManagedGamma22);
    const PresentationSurfaceContract presentation =
        selection.presentationContract();
    QCOMPARE(
        presentation.sdrTransfer,
        PresentationOutputTransfer::Gamma22);
    QVERIFY(!presentation.extendedLinearAllowed);
}

void WaylandColorManagementTest::
extendedLinearIsObservedWithoutPrematureHdrActivation() {
    auto capabilities = completeManagedSdrCapabilities();
    capabilities.extendedLinearTransfer = true;

    QVERIFY(capabilities.supportsManagedHdrObservation());
    const WaylandSdrSurfaceSelection selection =
        selectWaylandSdrSurface(capabilities);
    QCOMPARE(selection.mode, WaylandSdrSurfaceMode::ManagedGamma22);
    QVERIFY(!selection.presentationContract().extendedLinearAllowed);
}

QTEST_APPLESS_MAIN(WaylandColorManagementTest)
#include "tst_WaylandColorManagement.moc"
