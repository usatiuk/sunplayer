pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property string title
    required property var items
    property bool landscape: false
    signal activated(int row)
    spacing: 14
    visible: items.length > 0

    Label {
        Layout.fillWidth: true
        text: root.title
        color: "#f1f3f7"
        font.pixelSize: 23
        font.weight: Font.DemiBold
    }
    ListView {
        Layout.fillWidth: true
        Layout.preferredHeight: root.landscape ? 226 : 310
        clip: true
        orientation: ListView.Horizontal
        spacing: 18
        ScrollBar.horizontal: AppScrollBar {}
        model: root.items
        delegate: JellyfinPoster {
            required property var modelData
            required property int index
            item: modelData
            landscape: root.landscape
            width: root.landscape ? 260 : 156
            onClicked: root.activated(index)
        }
    }
}
