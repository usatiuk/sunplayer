pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "jellyfinDetailHeader"
    required property var item
    readonly property bool illustrated: item.kind === "Movie" || item.kind === "Series" || item.kind === "Season"
    readonly property bool compact: width < 720
    signal playRequested(bool fromStart)
    signal watchedRequested(bool watched)
    implicitHeight: hero.implicitHeight + 48

    Rectangle {
        anchors.fill: parent
        color: root.illustrated ? "#181c24" : "transparent"
    }
    Image {
        anchors.fill: parent
        visible: root.illustrated
        source: root.item.backdrop
        fillMode: Image.PreserveAspectCrop
        opacity: 0.25
        clip: true
    }
    RowLayout {
        id: hero
        x: 24
        y: 24
        width: parent.width - 48
        spacing: root.compact ? 18 : 30
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: root.compact ? 104 : 180
            Layout.preferredHeight: width * 1.5
            visible: root.illustrated
            color: "#272e3a"
            radius: 6
            clip: true
            Image {
                anchors.fill: parent
                source: root.item.artwork
                fillMode: Image.PreserveAspectCrop
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 13
            Label {
                visible: root.illustrated
                text: root.item.kind === "Movie" ? qsTr("MOVIE") : root.item.kind === "Series" ? qsTr("TV SHOW") : qsTr("SEASON")
                color: "#77cde8"
                font.pixelSize: 11
                font.letterSpacing: 2
            }
            Label {
                Layout.fillWidth: true
                text: root.item.name
                textFormat: Text.PlainText
                font.pixelSize: root.compact ? 25 : 34
                font.weight: Font.Bold
                color: "#f1f3f7"
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.item.subtitle
                textFormat: Text.PlainText
                color: "#b0b8c8"
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: root.item.overview.length > 0
                text: root.item.overview
                textFormat: Text.PlainText
                color: "#c5ccd8"
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: 10
                visible: root.item.playable
                Button {
                    text: !root.item.watched && root.item.progress > 0 ? qsTr("▶ Resume") : qsTr("▶ Play")
                    highlighted: true
                    onClicked: root.playRequested(false)
                }
                Button {
                    visible: !root.item.watched && root.item.progress > 0
                    text: qsTr("Play from start")
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
