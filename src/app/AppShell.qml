import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property real renderDevicePixelRatio
    required property WindowCommands windowCommands
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
        target: root.windowCommands
        property: "cursorHidden"
        value: root.currentPage === 0 && playerPage.cursorShouldHide
    }

    Binding {
        target: root.windowCommands
        property: "windowShortcutsBlocked"
        value: root.activePage.windowShortcutsBlocked
    }

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

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: windowChrome.contentTop
            bottom: parent.bottom
        }
        currentIndex: root.currentPage

        PlayerPage {
            id: playerPage

            session: root.mediaSession
            windowCommands: root.windowCommands
            onHdrLabRequested: root.currentPage = 1
        }

        HdrLabPage {
            id: hdrLabPage

            outputState: root.presentationOutput
            presentationSettings: root.presentationPolicy
            videoSource: root.diagnosticSource
        }
    }

    ClientSideWindowChrome {
        id: windowChrome

        anchors.fill: parent
        z: 100
        controller: root.windowCommands.windowChrome
        renderDevicePixelRatio: root.renderDevicePixelRatio
        titleRequested: root.currentPage !== 0
            || !playerPage.sessionActive
            || playerPage.controlsShouldShow
        contentInsetRequested: root.currentPage !== 0
            || !playerPage.sessionActive
        onUserActivity: {
            if (root.currentPage === 0)
                playerPage.revealControls()
        }
    }

    Button {
        objectName: "backToPlayerButton"

        anchors {
            right: parent.right
            top: parent.top
            margins: 24
            topMargin: windowChrome.contentTop + 24
        }
        z: 30
        visible: root.currentPage === 1
        text: qsTr("← Player")
        onClicked: root.currentPage = 0
    }
}
