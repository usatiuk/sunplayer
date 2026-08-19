import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Developer-facing HDR and presentation diagnostics retained as an app page.
VideoPage {
    id: root

    required property PresentationOutputState outputState
    required property PresentationSettings presentationSettings
    required property DiagnosticVideoSource videoSource

    videoViewportRect:
        Qt.rect(canvasX, canvasY, canvasWidth, canvasHeight)
    videoViewportVisible: visible
    readonly property real effectiveTargetHeadroom:
        root.presentationSettings.automaticTargetPeak
        ? root.outputState.effectiveTargetHeadroom
        : root.presentationSettings.manualTargetHeadroom
    readonly property real hdrReferenceWhiteNits: 203
    readonly property real canvasX: 24
    readonly property real canvasY: 112
    readonly property real canvasWidth: Math.max(1, width - 48)
    readonly property real canvasHeight: Math.max(
        1, height - canvasY - footer.height - 40)

    function sourcePeakLabel(value) {
        return value <= 1
            ? qsTr("Relative SDR · sRGB / BT.709")
            : qsTr("HDR10/PQ · %1 nits · %2× %3-nit source ref")
                .arg(Math.round(value * root.hdrReferenceWhiteNits))
                .arg(value.toFixed(1))
                .arg(root.hdrReferenceWhiteNits)
    }

    function targetHeadroomLabel(value) {
        return root.outputState.sdrWhiteKnown
            ? Math.round(value * root.outputState.sdrWhiteNits)
                + qsTr(" nits · ") + value.toFixed(1) + qsTr("× white")
            : value.toFixed(1) + qsTr("× SDR white")
    }

    component DiagnosticText: Label {
        Layout.fillWidth: true
        color: "#aeb6c5"
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }

    Item {
        id: sdrInterface

        anchors.fill: parent

        Rectangle {
            id: headerPanel
            objectName: "hdrLabHeaderPanel"

            anchors {
                left: parent.left
                top: parent.top
                margins: 24
            }
            width: headerColumn.implicitWidth
            height: headerColumn.implicitHeight
            color: "black"

            Column {
                id: headerColumn

                spacing: 4

                Label {
                    text: qsTr("HDR Lab")
                    color: "white"
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }

                Label {
                    text: root.outputState.hdrPresentationActive
                        ? (root.outputState.displayHdrEnabled
                            ? qsTr("HDR presentation active")
                            : qsTr("Extended-range presentation active"))
                        : (root.outputState.displayHdrEnabled
                            ? qsTr("SDR presentation on HDR display")
                            : qsTr("SDR presentation active"))
                    color: "#8ed6a8"
                }
            }
        }

        Rectangle {
            id: outputPanel
            objectName: "hdrLabOutputPanel"

            x: parent.width - width - 36
            y: Math.min(124,
                Math.max(24, parent.height - height - 24))
            z: 10
            width: Math.min(440, parent.width - 48)
            height: Math.min(diagnosticColumn.implicitHeight + 24,
                Math.max(1, parent.height - 48))
            radius: 8
            color: "black"
            border.color: "#3b4250"
            clip: true

            DragHandler {
                target: outputPanel
                xAxis.minimum: 0
                xAxis.maximum: Math.max(0, outputPanel.parent.width - outputPanel.width)
                yAxis.minimum: 0
                yAxis.maximum: Math.max(0, outputPanel.parent.height - outputPanel.height)
            }

            Flickable {
                id: diagnosticScroll
                objectName: "hdrLabDiagnosticScroll"

                anchors {
                    fill: parent
                    margins: 12
                }
                contentWidth: width
                contentHeight: diagnosticColumn.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ScrollBar.vertical: ScrollBar {
                    id: diagnosticScrollBar
                }

                ColumnLayout {
                    id: diagnosticColumn

                    width: diagnosticScroll.width
                        - diagnosticScrollBar.width - 4
                    spacing: 3

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Output state")
                        color: "white"
                        font.weight: Font.DemiBold
                    }

                    DiagnosticText {
                        text: root.outputState.screenName
                        color: "#c8cfdb"
                    }

                    DiagnosticText {
                        text: root.outputState.graphicsApi
                            + " · " + root.outputState.swapChainFormat
                    }

                    DiagnosticText {
                        text: root.outputState.graphicsAdapter
                    }

                    DiagnosticText {
                        text: root.outputState.videoSurfaceProducer
                            + " · " + root.outputState.videoInputPath
                    }

                    DiagnosticText {
                        text: root.outputState.videoColorPolicy
                    }

                    DiagnosticText {
                        text: root.outputState.videoSurfaceFormat
                    }

                    DiagnosticText {
                        text: root.outputState.videoOutputPath
                            + " · " + root.outputState.videoCopySummary
                    }

                    DiagnosticText {
                        text: root.outputState.videoSynchronization
                    }

                    DiagnosticText {
                        visible:
                            root.outputState.videoFallbackReason.length > 0
                        text: qsTr("Fallback: %1").arg(
                            root.outputState.videoFallbackReason)
                        color: "#ffca7a"
                    }

                    DiagnosticText {
                        text: qsTr("DPR %1 · %2 Hz")
                            .arg(root.outputState.devicePixelRatio.toFixed(2))
                            .arg(root.outputState.refreshRate.toFixed(1))
                    }

                    DiagnosticText {
                        text: root.outputState.sceneReferred
                            ? qsTr("Swapchain scene-referred · 1.0 = 80 nits")
                            : qsTr("Display-referred · 1.0 = SDR white")
                    }

                    DiagnosticText {
                        text: root.outputState.displayColorMode
                            + " · " + root.outputState.targetGamut
                    }

                    DiagnosticText {
                        text: root.outputState.sdrWhiteKnown
                            ? qsTr("SDR white %1 nits · UI ×%2")
                                .arg(root.outputState.sdrWhiteNits.toFixed(1))
                                .arg(root.outputState.sdrScale.toFixed(2))
                            : qsTr("SDR white is display-referred · UI ×1")
                    }

                    DiagnosticText {
                        text: !root.outputState.hdrPresentationActive
                            ? qsTr("SDR output · HDR headroom unavailable")
                            : (root.outputState.luminanceKnown
                                ? qsTr("Compositor target %1–%2 nits · %3× reference")
                                    .arg(root.outputState.minLuminanceNits.toFixed(3))
                                    .arg(root.outputState.maxLuminanceNits.toFixed(1))
                                    .arg(root.effectiveTargetHeadroom.toFixed(2))
                                : qsTr("EDR headroom %1× · potential %2×")
                                    .arg(root.outputState.currentHeadroom.toFixed(2))
                                    .arg(root.outputState.potentialHeadroom.toFixed(2)))
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 12
                            color: "#404040"
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 12
                            color: "#808080"
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 12
                            color: "#ffffff"
                        }
                    }

                    Button {
                        objectName: "hdrLabReprobeButton"
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Reprobe")
                        onClicked: root.outputState.reprobePresentation()
                    }
                }
            }
        }

        Item {
            id: footer

            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                margins: 24
            }
            height: footerLayout.implicitHeight

            Rectangle {
                objectName: "hdrLabFooterPanel"

                anchors.fill: parent
                color: "black"
            }

            ColumnLayout {
                id: footerLayout

                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: qsTr("Pattern peak")
                        color: "white"
                    }

                    Slider {
                        id: sourcePeakSlider
                        objectName: "hdrLabSourcePeakSlider"

                        Layout.fillWidth: true
                        from: 1
                        to: 10
                        value: root.videoSource.sourcePeakHeadroom
                        stepSize: 0.1
                        onMoved:
                            root.videoSource.sourcePeakHeadroom = value
                    }

                    Label {
                        objectName: "hdrLabSourcePeakLabel"
                        Layout.minimumWidth: 140
                        horizontalAlignment: Text.AlignRight
                        text: root.sourcePeakLabel(
                            root.videoSource.sourcePeakHeadroom)
                        color: "white"
                    }

                    Switch {
                        id: toneMapSwitch

                        text: qsTr("Tone map")
                        palette.windowText: "#f2f4f8"
                        checked: root.videoSource.toneMappingEnabled
                        onToggled:
                            root.videoSource.toneMappingEnabled = checked
                    }

                    Switch {
                        text: qsTr("Animate")
                        palette.windowText: "#f2f4f8"
                        checked: root.videoSource.animatePattern
                        onToggled:
                            root.videoSource.animatePattern = checked
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Switch {
                        text: qsTr("Auto display peak")
                        palette.windowText: "#f2f4f8"
                        checked:
                            root.presentationSettings.automaticTargetPeak
                        onToggled:
                            root.presentationSettings.automaticTargetPeak =
                                checked
                    }

                    Slider {
                        id: targetPeakSlider

                        Layout.fillWidth: true
                        enabled:
                            !root.presentationSettings.automaticTargetPeak
                        from: 1
                        to: 25
                        value:
                            root.presentationSettings.manualTargetHeadroom
                        stepSize: 0.1
                        onMoved:
                            root.presentationSettings.manualTargetHeadroom =
                                value
                    }

                    Label {
                        Layout.minimumWidth: 140
                        horizontalAlignment: Text.AlignRight
                        text: root.targetHeadroomLabel(
                            root.effectiveTargetHeadroom)
                        color: "white"
                    }

                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: qsTr("Diagnostic renderer")
                        color: "white"
                    }

                    Switch {
                        objectName: "videoRendererSwitch"
                        text: checked
                            ? qsTr("libplacebo")
                            : qsTr("Procedural QRhi")
                        palette.windowText: "#f2f4f8"
                        checked: root.videoSource.useLibplacebo
                        onToggled:
                            root.videoSource.useLibplacebo = checked
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Label {
                        text: qsTr("Comparison tool · not a player fallback")
                        color: "#7f899b"
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Pattern 1.0 is SDR white. Highlights are tone-mapped into the display headroom, then encoded for the active presentation path.")
                    color: "#7f899b"
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
