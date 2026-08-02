pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root

    objectName: "clientSideWindowChrome"
    required property var controller
    required property bool titleRequested
    required property bool contentInsetRequested
    signal userActivity
    readonly property real titleBarHeight: 38
    readonly property bool available:
        controller.enabled && !controller.fullscreen
    readonly property real contentTop:
        available && contentInsetRequested ? titleBarHeight : 0
    readonly property bool resizeEnabled:
        available && !controller.maximized

    function beginResize(edgeMask) {
        controller.beginSystemResize(edgeMask)
    }

    Rectangle {
        id: titleBar

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        objectName: "clientSideTitleBar"
        z: 1
        height: root.titleBarHeight
        visible: root.available
        enabled: visible && root.titleRequested
        opacity: root.titleRequested ? 1 : 0
        color: "#000000"

        Behavior on opacity {
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutCubic
            }
        }

        MouseArea {
            anchors {
                fill: parent
                rightMargin: windowButtons.width
            }
            acceptedButtons: Qt.LeftButton
            onPressed: root.controller.beginSystemMove()
            onDoubleClicked: root.controller.toggleMaximized()
        }

        Label {
            anchors {
                left: parent.left
                leftMargin: 14
                verticalCenter: parent.verticalCenter
            }
            text: qsTr("Sunroom")
            color: "#e8eaf0"
            font.pixelSize: 13
            font.weight: Font.Medium
        }

        Row {
            id: windowButtons

            anchors {
                right: parent.right
                top: parent.top
                bottom: parent.bottom
            }

            component WindowButton: ToolButton {
                id: windowButton

                required property string themeIconName
                required property url fallbackIconSource
                width: 46
                height: titleBar.height
                display: AbstractButton.IconOnly
                icon.name: themeIconName
                icon.source: fallbackIconSource
                icon.width: 16
                icon.height: 16
                icon.color: "#e8eaf0"
                background: Rectangle {
                    color: windowButton.down ? "#424650"
                        : windowButton.hovered ? "#2b2e36"
                        : "transparent"
                }
            }

            WindowButton {
                themeIconName: "window-minimize-symbolic"
                fallbackIconSource: "icons/lucide/minus.svg"
                Accessible.name: qsTr("Minimize")
                onClicked: root.controller.minimize()
            }

            WindowButton {
                themeIconName: root.controller.maximized
                    ? "window-restore-symbolic"
                    : "window-maximize-symbolic"
                fallbackIconSource: root.controller.maximized
                    ? "icons/lucide/copy.svg"
                    : "icons/lucide/square.svg"
                Accessible.name: root.controller.maximized
                    ? qsTr("Restore") : qsTr("Maximize")
                onClicked: root.controller.toggleMaximized()
            }

            WindowButton {
                id: closeButton

                themeIconName: "window-close-symbolic"
                fallbackIconSource: "icons/lucide/x.svg"
                Accessible.name: qsTr("Close")
                background: Rectangle {
                    color: closeButton.down ? "#b71c2a"
                        : closeButton.hovered ? "#d52b3f"
                        : "transparent"
                }
                onClicked: root.controller.close()
            }
        }

    }

    HoverHandler {
        enabled: root.controller.enabled
            && !root.controller.fullscreen
        onPointChanged: root.userActivity()
    }

    MouseArea {
        z: 2
        width: 6
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        visible: root.resizeEnabled
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.LeftEdge)
    }
    MouseArea {
        z: 2
        width: 6
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        visible: root.resizeEnabled
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.RightEdge)
    }
    MouseArea {
        z: 2
        height: 6
        anchors { left: parent.left; right: parent.right; top: parent.top }
        visible: root.resizeEnabled
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.TopEdge)
    }
    MouseArea {
        z: 2
        height: 6
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: root.resizeEnabled
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(Qt.BottomEdge)
    }

    component ResizeCorner: MouseArea {
        required property int edgeMask
        z: 2
        width: 12
        height: 12
        visible: root.resizeEnabled
        acceptedButtons: Qt.LeftButton
        onPressed: root.beginResize(edgeMask)
    }

    ResizeCorner {
        anchors { left: parent.left; top: parent.top }
        edgeMask: Qt.TopEdge | Qt.LeftEdge
        cursorShape: Qt.SizeFDiagCursor
    }
    ResizeCorner {
        anchors { right: parent.right; top: parent.top }
        edgeMask: Qt.TopEdge | Qt.RightEdge
        cursorShape: Qt.SizeBDiagCursor
    }
    ResizeCorner {
        anchors { left: parent.left; bottom: parent.bottom }
        edgeMask: Qt.BottomEdge | Qt.LeftEdge
        cursorShape: Qt.SizeBDiagCursor
    }
    ResizeCorner {
        anchors { right: parent.right; bottom: parent.bottom }
        edgeMask: Qt.BottomEdge | Qt.RightEdge
        cursorShape: Qt.SizeFDiagCursor
    }
}
