pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property real renderDevicePixelRatio
    required property string applicationDisplayName
    required property WindowCommands windowCommands
    required property PresentationOutputState presentationOutput
    required property PresentationSettings presentationPolicy
    required property DiagnosticVideoSource diagnosticSource
    required property MediaSession mediaSession
    required property ActiveVideoSource activeVideoSource
    required property VideoViewportState viewportState
    required property SupportController supportController

    property JellyfinBrowser jellyfin: null
    property int currentPage: 0
    property int playbackReturnPage: 0
    property bool jellyfinVisited: false
    readonly property bool jellyfinAvailable: jellyfin !== null && !windowCommands.jellyfinHidden

    onJellyfinAvailableChanged: {
        if (!jellyfinAvailable) {
            if (currentPage === 2)
                currentPage = 0
            if (playbackReturnPage === 2)
                playbackReturnPage = 0
        }
    }

    function openJellyfin() {
        if (!jellyfinAvailable)
            return
        jellyfinVisited = true
        currentPage = 2
    }
    function goUp() {
        if (currentPage === 2 && jellyfin.canGoUp)
            jellyfin.up()
        else if (currentPage === 2) {
            mediaSession.cancel()
            currentPage = 0
        }
    }
    function goBack() {
        if (currentPage === 2) {
            if (jellyfin.canGoBack)
                jellyfin.back()
            else {
                mediaSession.cancel()
                currentPage = 0
            }
        } else if (currentPage === 1) {
            currentPage = 0
        } else if (mediaSession.state !== MediaSession.Empty) {
            mediaSession.cancel()
            currentPage = playbackReturnPage
        }
    }
    function goForward() {
        if (currentPage === 2)
            jellyfin.forward()
        else if (currentPage === 0 && mediaSession.state === MediaSession.Empty && jellyfinVisited && jellyfinAvailable)
            currentPage = 2
    }

    MouseArea {
        anchors.fill: parent
        z: 50
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        scrollGestureEnabled: false
        onClicked: function(mouse) {
            if (mouse.button === Qt.BackButton)
                root.goBack()
            else
                root.goForward()
        }
    }
    readonly property real chromeTop: windowChrome.available ? windowChrome.titleBarHeight : 0
    readonly property VideoPage activePage:
        currentPage === 0 ? playerPage : currentPage === 1 ? hdrLabPage : (browserLoader.item as VideoPage)
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
        value: root.currentPage === 1
            ? ActiveVideoSource.Diagnostics
            : ActiveVideoSource.Player
    }

    Connections {
        target: root.windowCommands

        function onNavigationBackRequested() { root.goBack() }
        function onNavigationForwardRequested() { root.goForward() }
        function onMediaOpenRequested() {
            root.playbackReturnPage = root.currentPage
            root.currentPage = 0
        }
    }

    Connections {
        target: root.supportController

        function onHdrLabRequested() {
            root.currentPage = 1
        }
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
            applicationDisplayName: root.applicationDisplayName
            outputState: root.presentationOutput
            windowCommands: root.windowCommands
            supportController: root.supportController
            topContentInset: root.chromeTop - pageStack.y
            jellyfinAvailable: root.jellyfinAvailable
            onJellyfinRequested: root.openJellyfin()
        }

        HdrLabPage {
            id: hdrLabPage

            outputState: root.presentationOutput
            presentationSettings: root.presentationPolicy
            videoSource: root.diagnosticSource
        }
        Loader {
            id: browserLoader
            active: root.jellyfin !== null
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: JellyfinPage {
                id: jellyfinPage
                browser: root.jellyfin
                onBackRequested: root.goUp()
            }
        }

    }

    Button {
        objectName: "playerBackButton"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 24
        anchors.topMargin: root.chromeTop + 24
        z: 30
        visible: root.currentPage === 0 && root.mediaSession.state !== MediaSession.Empty
            && (!playerPage.sessionActive || playerPage.controlsShouldShow)
        text: qsTr("Back")
        display: AbstractButton.IconOnly
        implicitWidth: Math.max(40, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(40, implicitContentHeight + topPadding + bottomPadding)
        icon.source: "icons/lucide/arrow-left.svg"
        icon.color: palette.buttonText
        icon.width: 20
        icon.height: 20
        ToolTip.visible: hovered
        ToolTip.text: text
        onClicked: root.goBack()
    }

    ClientSideWindowChrome {
        id: windowChrome

        anchors.fill: parent
        z: 100
        controller: root.windowCommands.windowChrome
        applicationDisplayName: root.applicationDisplayName
        renderDevicePixelRatio: root.renderDevicePixelRatio
        titleRequested: root.currentPage !== 0
            || !playerPage.sessionActive
            || playerPage.controlsShouldShow
        contentInsetRequested: root.currentPage !== 0
            || !playerPage.sessionActive
        onUserActivity: function(position) {
            if (root.currentPage === 0)
                playerPage.observePointerMovement(
                    windowChrome.mapToItem(playerPage, position.x, position.y))
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
        text: qsTr("Return to player")
        display: AbstractButton.IconOnly
        implicitWidth: Math.max(40, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(40, implicitContentHeight + topPadding + bottomPadding)
        icon.source: "icons/lucide/arrow-left.svg"
        icon.color: palette.buttonText
        icon.width: 20
        icon.height: 20
        ToolTip.visible: hovered
        ToolTip.text: text
        onClicked: root.currentPage = 0
    }
}
