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
            ? qsTr("SDR white · 1.0× white")
            : Math.round(value * root.hdrReferenceWhiteNits)
                + qsTr(" nits · ") + value.toFixed(1)
                + qsTr("× HDR reference white")
    }

    function targetHeadroomLabel(value) {
        return root.outputState.sdrWhiteKnown
            ? Math.round(value * root.outputState.sdrWhiteNits)
                + qsTr(" nits · ") + value.toFixed(1) + qsTr("× white")
            : value.toFixed(1) + qsTr("× SDR white")
    }

    Item {
        id: sdrInterface

        anchors.fill: parent

        Rectangle {
            id: headerPanel

            anchors {
                left: parent.left
                top: parent.top
                margins: 24
            }
            width: headerColumn.implicitWidth
            height: headerColumn.implicitHeight
            color: "#111318"

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
                    text: root.outputState.extendedLinearActive
                        ? (root.outputState.displayHdrEnabled
                            ? qsTr("FP16 HDR presentation active")
                            : qsTr("FP16 extended-linear presentation active"))
                        : (root.outputState.displayHdrEnabled
                            ? qsTr("SDR presentation on HDR display")
                            : qsTr("SDR presentation active"))
                    color: "#8ed6a8"
                }
            }
        }

        Rectangle {
            id: outputPanel

            x: parent.width - width - 36
            y: 124
            z: 10
            width: 286
            height: diagnosticColumn.implicitHeight + 24
            radius: 8
            color: Qt.rgba(27 / 255, 30 / 255, 38 / 255, 0.96)
            border.color: "#3b4250"

            DragHandler {
                target: outputPanel
                xAxis.minimum: 0
                xAxis.maximum: Math.max(0, outputPanel.parent.width - outputPanel.width)
                yAxis.minimum: 0
                yAxis.maximum: Math.max(0, outputPanel.parent.height - outputPanel.height)
            }

            ColumnLayout {
                id: diagnosticColumn

                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    margins: 12
                }
                spacing: 3

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Output state")
                    color: "white"
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: root.outputState.screenName
                    color: "#c8cfdb"
                    elide: Text.ElideRight
                }

                Label {
                    text: root.outputState.graphicsApi
                        + " · " + root.outputState.swapChainFormat
                    color: "#aeb6c5"
                }

                Label {
                    Layout.fillWidth: true
                    text: root.outputState.graphicsAdapter
                    color: "#aeb6c5"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: root.outputState.videoSurfaceProducer
                        + " · " + root.outputState.videoInputPath
                    color: "#aeb6c5"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: root.outputState.videoSurfaceFormat
                    color: "#aeb6c5"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: root.outputState.videoOutputPath
                        + " · " + root.outputState.videoCopySummary
                    color: "#aeb6c5"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: root.outputState.videoSynchronization
                    color: "#aeb6c5"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible:
                        root.outputState.videoFallbackReason.length > 0
                    text: qsTr("Fallback: %1").arg(
                        root.outputState.videoFallbackReason)
                    color: "#ffca7a"
                    elide: Text.ElideRight
                }

                Label {
                    text: qsTr("DPR %1 · %2 Hz")
                        .arg(root.outputState.devicePixelRatio.toFixed(2))
                        .arg(root.outputState.refreshRate.toFixed(1))
                    color: "#aeb6c5"
                }

                Label {
                    text: root.outputState.sceneReferred
                        ? qsTr("Scene-referred · 1.0 = 80 nits")
                        : qsTr("Display-referred · 1.0 = SDR white")
                    color: "#aeb6c5"
                }

                Label {
                    text: root.outputState.sdrWhiteKnown
                        ? qsTr("SDR white %1 nits · UI ×%2")
                            .arg(root.outputState.sdrWhiteNits.toFixed(1))
                            .arg(root.outputState.sdrScale.toFixed(2))
                        : qsTr("SDR white is display-referred · UI ×1")
                    color: "#aeb6c5"
                }

                Label {
                    text: !root.outputState.extendedLinearActive
                        ? qsTr("SDR output · HDR headroom unavailable")
                        : (root.outputState.luminanceKnown
                            ? qsTr("Display range %1–%2 nits · %3× white")
                                .arg(root.outputState.minLuminanceNits.toFixed(3))
                                .arg(root.outputState.maxLuminanceNits.toFixed(1))
                                .arg(root.effectiveTargetHeadroom.toFixed(2))
                            : qsTr("EDR headroom %1× · potential %2×")
                                .arg(root.outputState.currentHeadroom.toFixed(2))
                                .arg(root.outputState.potentialHeadroom.toFixed(2)))
                    color: "#aeb6c5"
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
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Reprobe")
                    onClicked: root.outputState.reprobePresentation()
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
                anchors.fill: parent
                color: "#111318"
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

                        Layout.fillWidth: true
                        from: 1
                        to: 25
                        value: root.videoSource.sourcePeakHeadroom
                        stepSize: 0.1
                        onMoved:
                            root.videoSource.sourcePeakHeadroom = value
                    }

                    Label {
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
