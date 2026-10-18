pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    required property var item
    property bool landscape: false
    hoverEnabled: true
    implicitWidth: landscape ? 280 : 160
    implicitHeight: contents.implicitHeight
    Accessible.name: item.name

    contentItem: Column {
        id: contents
        spacing: 7
        Rectangle {
            width: parent.width
            height: root.landscape ? width * 9 / 16 : width * 1.5
            radius: 6
            color: "#222833"
            clip: true
            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                text: root.item.name
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                textFormat: Text.PlainText
                color: "#929cae"
            }
            Image {
                anchors.fill: parent
                source: root.item.artwork
                fillMode: Image.PreserveAspectCrop
            }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: 6
                border.width: root.hovered || root.visualFocus ? 2 : 0
                border.color: "#53c8ed"
            }
            Rectangle {
                anchors {
                    top: parent.top
                    right: parent.right
                    margins: 9
                }
                width: 26
                height: 26
                radius: 13
                color: "#25b2dd"
                visible: root.item.kind !== "CollectionFolder" && root.item.watched
                Label {
                    anchors.centerIn: parent
                    text: "✓"
                    color: "#08202b"
                    font.bold: true
                }
            }
            Rectangle {
                anchors {
                    left: parent.left
                    bottom: parent.bottom
                }
                height: 4
                width: parent.width * root.item.progress
                visible: root.item.kind !== "CollectionFolder" && !root.item.watched && root.item.progress > 0
                color: "#25b2dd"
            }
        }
        Label {
            width: parent.width
            text: root.item.name
            color: root.hovered || root.visualFocus ? "#71d3f0" : "#f1f3f7"
            font.pixelSize: 15
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        Label {
            width: parent.width
            text: root.item.subtitle
            visible: text.length > 0
            font.pixelSize: 12
            color: "#929cae"
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
    }
}
