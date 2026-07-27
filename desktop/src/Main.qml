import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property bool automaticTargetPeak: true
    property real manualTargetHeadroom: 7.5
    property real phase: 0
    readonly property real sourcePeakHeadroom: sourcePeakSlider.value
    readonly property bool toneMappingEnabled: toneMapSwitch.checked
    readonly property real effectiveTargetHeadroom: automaticTargetPeak
        ? (outputState.hdrActive
            ? outputState.currentHeadroom / Math.max(outputState.sdrScale, 0.001)
            : 1.0)
        : manualTargetHeadroom
    readonly property real canvasX: 24
    readonly property real canvasY: 112
    readonly property real canvasWidth: Math.max(1, width - 48)
    readonly property real canvasHeight: Math.max(
        1, height - canvasY - footer.height - 40)

    function headroomLabel(value) {
        return outputState.sdrWhiteKnown
            ? Math.round(value * outputState.sdrWhiteNits)
                + qsTr(" nits · ") + value.toFixed(1) + qsTr("× white")
            : value.toFixed(1) + qsTr("× SDR white")
    }

    NumberAnimation on phase {
        from: 0
        to: 1
        duration: 8000
        loops: Animation.Infinite
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
                    text: qsTr("RHI / HDR playground")
                    color: "white"
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }

                Label {
                    text: outputState.hdrActive
                        ? qsTr("FP16 HDR presentation active")
                        : qsTr("FP16 scRGB · SDR output")
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
                    text: outputState.screenName
                    color: "#c8cfdb"
                    elide: Text.ElideRight
                }

                Label {
                    text: outputState.graphicsApi + " · " + outputState.swapChainFormat
                    color: "#aeb6c5"
                }

                Label {
                    text: qsTr("DPR %1 · %2 Hz")
                        .arg(outputState.devicePixelRatio.toFixed(2))
                        .arg(outputState.refreshRate.toFixed(1))
                    color: "#aeb6c5"
                }

                Label {
                    text: outputState.sceneReferred
                        ? qsTr("Scene-referred · 1.0 = 80 nits")
                        : qsTr("Display-referred · 1.0 = SDR white")
                    color: "#aeb6c5"
                }

                Label {
                    text: outputState.sdrWhiteKnown
                        ? qsTr("SDR white %1 nits · UI ×%2")
                            .arg(outputState.sdrWhiteNits.toFixed(1))
                            .arg(outputState.sdrScale.toFixed(2))
                        : qsTr("SDR white is display-referred · UI ×1")
                    color: "#aeb6c5"
                }

                Label {
                    text: outputState.maxLuminanceNits > 0
                        ? qsTr("Display range %1–%2 nits · %3× white")
                            .arg(outputState.minLuminanceNits.toFixed(3))
                            .arg(outputState.maxLuminanceNits.toFixed(1))
                            .arg(root.effectiveTargetHeadroom.toFixed(2))
                        : qsTr("EDR headroom %1× · potential %2×")
                            .arg(outputState.currentHeadroom.toFixed(2))
                            .arg(outputState.potentialHeadroom.toFixed(2))
                    color: "#aeb6c5"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        Layout.fillWidth: true
                        height: 12
                        color: "#404040"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 12
                        color: "#808080"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 12
                        color: "#ffffff"
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Refresh")
                    onClicked: outputState.refresh()
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
                        value: 12.5
                        stepSize: 0.1
                    }

                    Label {
                        Layout.minimumWidth: 140
                        horizontalAlignment: Text.AlignRight
                        text: root.headroomLabel(sourcePeakSlider.value)
                        color: "white"
                    }

                    Switch {
                        id: toneMapSwitch

                        text: qsTr("Tone map")
                        checked: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Switch {
                        text: qsTr("Auto display peak")
                        checked: root.automaticTargetPeak
                        onToggled: root.automaticTargetPeak = checked
                    }

                    Slider {
                        id: targetPeakSlider

                        Layout.fillWidth: true
                        enabled: !root.automaticTargetPeak
                        from: 1
                        to: 25
                        value: root.manualTargetHeadroom
                        stepSize: 0.1
                        onMoved: root.manualTargetHeadroom = value
                    }

                    Label {
                        Layout.minimumWidth: 140
                        horizontalAlignment: Text.AlignRight
                        text: root.headroomLabel(root.effectiveTargetHeadroom)
                        color: "white"
                    }

                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Pattern 1.0 is SDR white. Highlights are tone-mapped into the display headroom, then converted to Windows scRGB.")
                    color: "#7f899b"
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
