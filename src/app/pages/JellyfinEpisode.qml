pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var item
    signal playRequested(bool fromStart)
    signal watchedRequested(bool watched)
    implicitHeight: episode.implicitHeight + 32
    radius: 8
    color: "#191d25"
    RowLayout {
        id: episode
        x: 16
        y: 16
        width: parent.width - 32
        spacing: 20
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: root.width < 700 ? 120 : 210
            Layout.preferredHeight: width * 9 / 16
            color: "#252c38"
            radius: 5
            clip: true
            Image {
                anchors.fill: parent
                source: root.item.artwork
                fillMode: Image.PreserveAspectCrop
            }
            Rectangle {
                anchors {
                    left: parent.left
                    bottom: parent.bottom
                }
                height: 3
                width: parent.width * root.item.progress
                color: "#25b2dd"
                visible: !root.item.watched
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 9
            Label {
                Layout.fillWidth: true
                text: root.item.name
                textFormat: Text.PlainText
                color: "#f1f3f7"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: root.item.subtitle
                textFormat: Text.PlainText
                color: "#929cae"
                wrapMode: Text.Wrap
                visible: text.length > 0
            }
            Label {
                Layout.fillWidth: true
                text: root.item.overview
                textFormat: Text.PlainText
                color: "#b6bfce"
                wrapMode: Text.Wrap
                visible: text.length > 0
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    text: !root.item.watched && root.item.progress > 0 ? qsTr("▶ Resume") : qsTr("▶ Play")
                    highlighted: true
                    onClicked: root.playRequested(false)
                }
                Button {
                    visible: !root.item.watched && root.item.progress > 0
                    text: qsTr("From start")
                    onClicked: root.playRequested(true)
                }
                Button {
                    text: root.item.watched ? qsTr("✓ Watched") : qsTr("Mark watched")
                    Accessible.name: root.item.watched ? qsTr("Mark unwatched") : qsTr("Mark watched")
                    onClicked: root.watchedRequested(!root.item.watched)
                }
            }
        }
    }
}
