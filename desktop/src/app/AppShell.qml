import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property PresentationOutputState presentationOutput
    required property PresentationSettings presentationPolicy
    required property DiagnosticVideoSource diagnosticSource
    required property MediaSession mediaSession
    required property ActiveVideoSource activeVideoSource
    required property VideoViewportState viewportState

    property int currentPage: 0
    readonly property VideoPage activePage:
        currentPage === 0 ? playerPage : hdrLabPage
    readonly property rect activeVideoViewportRect:
        Qt.rect(pageStack.x + activePage.x
                    + activePage.videoViewportRect.x,
                pageStack.y + activePage.y
                    + activePage.videoViewportRect.y,
                activePage.videoViewportRect.width,
                activePage.videoViewportRect.height)
    readonly property bool activeVideoViewportVisible:
        activePage.visible && activePage.videoViewportVisible

    Binding {
        target: root.activeVideoSource
        property: "route"
        value: root.currentPage === 0
            ? ActiveVideoSource.Player
            : ActiveVideoSource.Diagnostics
    }

    Binding {
        target: root.viewportState
        property: "rect"
        value: root.activeVideoViewportRect
    }

    Binding {
        target: root.viewportState
        property: "visible"
        value: root.activeVideoViewportVisible
    }

    Rectangle {
        id: navigation

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        z: 20
        height: 56
        color: "#111318"
        border.color: "#292e39"

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 18
                rightMargin: 18
            }
            spacing: 8

            Label {
                text: qsTr("Sunroom")
                color: "white"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.rightMargin: 16
            }

            Button {
                objectName: "playerPageButton"
                text: qsTr("Player")
                checkable: true
                checked: root.currentPage === 0
                onClicked: root.currentPage = 0
            }

            Button {
                objectName: "hdrLabPageButton"
                text: qsTr("HDR Lab")
                checkable: true
                checked: root.currentPage === 1
                onClicked: root.currentPage = 1
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                visible: root.mediaSession.state
                    === MediaSession.Ready
                text: root.mediaSession.displayName
                color: "#9ca6b8"
                elide: Text.ElideMiddle
                Layout.maximumWidth: 360
            }
        }
    }

    StackLayout {
        id: pageStack

        anchors {
            left: parent.left
            right: parent.right
            top: navigation.bottom
            bottom: parent.bottom
        }
        currentIndex: root.currentPage

        PlayerPage {
            id: playerPage

            session: root.mediaSession
        }

        HdrLabPage {
            id: hdrLabPage

            outputState: root.presentationOutput
            presentationSettings: root.presentationPolicy
            videoSource: root.diagnosticSource
        }
    }
}
