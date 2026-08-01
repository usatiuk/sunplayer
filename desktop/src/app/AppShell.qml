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

    StackLayout {
        id: pageStack

        anchors.fill: parent
        currentIndex: root.currentPage

        PlayerPage {
            id: playerPage

            session: root.mediaSession
            onHdrLabRequested: root.currentPage = 1
        }

        HdrLabPage {
            id: hdrLabPage

            outputState: root.presentationOutput
            presentationSettings: root.presentationPolicy
            videoSource: root.diagnosticSource
        }
    }

    Button {
        objectName: "backToPlayerButton"

        anchors {
            right: parent.right
            top: parent.top
            margins: 24
        }
        z: 30
        visible: root.currentPage === 1
        text: qsTr("← Player")
        onClicked: root.currentPage = 0
    }
}
