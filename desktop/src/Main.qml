import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 360
    visible: true
    title: qsTr("Sunroom")

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Sunroom")
            font.pixelSize: 32
            font.weight: Font.DemiBold
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Your Qt Quick app is ready.")
            color: root.palette.placeholderText
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Get started")
            onClicked: statusLabel.visible = true
        }

        Label {
            id: statusLabel

            Layout.alignment: Qt.AlignHCenter
            visible: false
            text: qsTr("Hello from QML!")
            color: root.palette.accent
        }
    }
}
