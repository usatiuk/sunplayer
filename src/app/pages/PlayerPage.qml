pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

VideoPage {
    id: root
    objectName: "playerPage"

    required property MediaSession session
    required property PresentationOutputState outputState
    required property WindowCommands windowCommands

    signal hdrLabRequested

    readonly property bool sessionReady:
        session.state === MediaSession.Ready
    readonly property bool frameReady:
        sessionReady && session.hasFrame
    readonly property bool sessionActive:
        sessionReady || session.seeking
    property bool showPlaybackDetails: false
    property bool controlsVisibleByActivity: true
    property point lastPointerPosition: Qt.point(-1, -1)
    readonly property bool controlsPinned:
        !session.playRequested
        || session.ended
        || session.seeking
        || session.playbackInterruption !== MediaSession.None
        || transportMenu.visible
        || detailsHover.hovered
        || seekSlider.pressed
        || volumeSlider.pressed
    readonly property bool controlsShouldShow:
        sessionActive
        && (controlsPinned || controlsVisibleByActivity)
    readonly property bool cursorShouldHide:
        frameReady && transportIsland.opacity < 0.05

    windowShortcutsBlocked: transportMenu.visible || openDialog.visible

    videoViewportRect:
        Qt.rect(0, 0, root.width, root.height)
    videoViewportVisible:
        visible && sessionReady

    Connections {
        target: root.windowCommands

        function onRelativeSeekRequested(milliseconds) {
            root.seekBy(milliseconds)
        }
    }

    function formatTime(milliseconds) {
        if (milliseconds < 0)
            return "--:--"
        const totalSeconds = Math.floor(milliseconds / 1000)
        const seconds = totalSeconds % 60
        const totalMinutes = Math.floor(totalSeconds / 60)
        const minutes = totalMinutes % 60
        const hours = Math.floor(totalMinutes / 60)
        const paddedSeconds = seconds.toString().padStart(2, "0")
        const paddedMinutes = minutes.toString().padStart(2, "0")
        return hours > 0
            ? hours + ":" + paddedMinutes + ":" + paddedSeconds
            : totalMinutes + ":" + paddedSeconds
    }

    function clockSourceText(source) {
        switch (source) {
        case MediaSession.PresentedAudio:
            return qsTr("audio master")
        case MediaSession.FrozenAudio:
            return qsTr("audio clock held")
        case MediaSession.ProvisionalMonotonic:
            return qsTr("audio clock starting")
        case MediaSession.PostAudioMonotonic:
            return qsTr("post-audio clock")
        default:
            return qsTr("monotonic clock")
        }
    }

    function playbackStateText() {
        if (session.ended)
            return qsTr("Ended")
        if (session.seeking)
            return qsTr("Seeking")
        if (!session.playRequested)
            return qsTr("Paused")
        switch (session.playbackInterruption) {
        case MediaSession.Buffering:
            return qsTr("Buffering audio")
        default:
            return qsTr("Playing")
        }
    }

    function presentationSummary() {
        if (outputState.swapChainFormat.length === 0
                || outputState.swapChainFormat === "Unavailable") {
            return qsTr("Presentation details unavailable")
        }
        let mode
        if (outputState.hdrPresentationActive) {
            mode = outputState.displayHdrEnabled
                ? qsTr("HDR presentation active")
                : qsTr("Extended-range presentation active")
        } else if (session.videoHdr) {
            mode = qsTr("HDR source mapped to SDR presentation")
        } else {
            mode = qsTr("SDR presentation")
        }
        return qsTr("%1 · %2").arg(mode).arg(outputState.swapChainFormat)
    }

    function revealControls() {
        if (!sessionActive)
            return
        controlsVisibleByActivity = true
        if (!controlsPinned)
            hideControlsTimer.restart()
    }

    function observePointerMovement(position) {
        if (Math.abs(position.x - lastPointerPosition.x) < 0.5
                && Math.abs(position.y - lastPointerPosition.y) < 0.5) {
            return
        }
        lastPointerPosition = position
        revealControls()
    }

    function togglePlayback() {
        if (!sessionReady || session.seeking)
            return
        if (session.playRequested && !session.ended)
            session.pause()
        else
            session.play()
        revealControls()
    }

    function seekBy(milliseconds) {
        if (!session.seekable
                || session.durationMilliseconds <= 0
                || session.seeking) {
            return
        }
        const target = Math.max(
            0,
            Math.min(
                session.durationMilliseconds,
                session.positionMilliseconds + milliseconds))
        session.seekToMilliseconds(Math.round(target))
        revealControls()
    }

    onSessionActiveChanged: {
        if (sessionActive)
            revealControls()
        else
            hideControlsTimer.stop()
    }

    onControlsPinnedChanged: {
        if (controlsPinned) {
            controlsVisibleByActivity = true
            hideControlsTimer.stop()
        } else if (sessionActive) {
            hideControlsTimer.restart()
        }
    }

    component IslandButton: AbstractButton {
        id: control

        property bool prominent: false
        property url iconSource

        implicitWidth: prominent
            ? 52
            : 40
        implicitHeight: prominent ? 42 : 36
        padding: 0

        contentItem: Item {
            Image {
                objectName: control.objectName.length > 0
                    ? control.objectName + "Icon"
                    : ""
                anchors.centerIn: parent
                width: control.prominent ? 17 : 15
                height: width
                source: control.iconSource
                sourceSize: Qt.size(64, 64)
                fillMode: Image.PreserveAspectFit
                mipmap: true
                opacity: control.enabled ? 1 : 0.38
            }
        }

        background: Rectangle {
            radius: height / 2
            color: control.down
                ? Qt.rgba(1, 1, 1, 0.24)
                : control.hovered
                    ? Qt.rgba(1, 1, 1, 0.15)
                    : control.prominent
                        ? Qt.rgba(1, 1, 1, 0.11)
                        : "transparent"
        }
    }

    component IslandSlider: Slider {
        id: control

        implicitHeight: 24
        leftPadding: 2
        rightPadding: 2

        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding
                + control.availableHeight / 2 - height / 2
            width: control.availableWidth
            height: 4
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.25)

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: control.enabled ? "#f5f6fa" : "#777b84"
            }
        }

        handle: Rectangle {
            x: control.leftPadding
                + control.visualPosition
                    * (control.availableWidth - width)
            y: control.topPadding
                + control.availableHeight / 2 - height / 2
            width: control.pressed || control.hovered ? 14 : 12
            height: width
            radius: width / 2
            color: control.enabled ? "#ffffff" : "#858993"

            Behavior on width {
                NumberAnimation { duration: 90 }
            }
        }
    }

    component DetailsSectionTitle: Label {
        Layout.fillWidth: true
        topPadding: 5
        color: "#d6d9e0"
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
    }

    FileDialog {
        id: openDialog
        objectName: "openDialog"

        title: qsTr("Open media")
        fileMode: FileDialog.OpenFile
        // A native macOS sheet must attach to the visible presentation window,
        // not the offscreen QQuickWindow that owns this redirected scene.
        parentWindow: Qt.platform.os === "osx" ? root.windowCommands : null
        nameFilters: [
            qsTr("Media files (*)")
        ]
        onAccepted: root.windowCommands.openMedia(selectedFile)
    }

    Timer {
        id: hideControlsTimer

        interval: 2400
        repeat: false
        onTriggered: {
            if (!root.controlsPinned)
                root.controlsVisibleByActivity = false
        }
    }

    HoverHandler {
        objectName: "playbackHoverHandler"
        acceptedDevices:
            PointerDevice.Mouse | PointerDevice.TouchPad
        onPointChanged: root.observePointerMovement(point.position)
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.frameReady
        color: "#000000"
    }

    MouseArea {
        objectName: "fullscreenBackgroundMouseArea"

        property bool fullscreenTogglePending: false

        anchors.fill: parent
        visible: root.sessionReady
        acceptedButtons: Qt.LeftButton
        onVisibleChanged: {
            if (!visible)
                fullscreenTogglePending = false
        }
        onDoubleClicked: fullscreenTogglePending = true
        onReleased: {
            if (!fullscreenTogglePending)
                return
            fullscreenTogglePending = false
            root.windowCommands.toggleFullscreen()
        }
        onCanceled: fullscreenTogglePending = false
    }

    ColumnLayout {
        objectName: "emptyState"
        anchors.centerIn: parent
        visible: root.session.state === MediaSession.Empty
        spacing: 14

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Open a video")
            color: "white"
            font.pixelSize: 24
            font.weight: Font.DemiBold
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Play local video with HDR and audio.")
            color: "#9297a2"
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10

            Button {
                objectName: "openMediaButton"
                text: qsTr("Open media…")
                onClicked: openDialog.open()
            }

            Button {
                objectName: "emptyHdrLabButton"
                text: qsTr("HDR Lab")
                onClicked: root.hdrLabRequested()
            }
        }
    }

    ColumnLayout {
        objectName: "waitingForVideoState"
        anchors.centerIn: parent
        visible: root.sessionReady && !root.session.hasFrame
        spacing: 12

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: parent.visible
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Preparing video…")
            color: "#c7ccd6"
        }
    }

    ColumnLayout {
        objectName: "openingState"
        anchors.centerIn: parent
        visible: root.session.state === MediaSession.Opening
            && !root.session.seeking
        spacing: 14

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: parent.visible
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Opening %1…").arg(root.session.displayName)
            color: "white"
            font.pixelSize: 20
        }

        Button {
            objectName: "cancelOpenButton"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Cancel")
            onClicked: root.session.cancel()
        }
    }

    ColumnLayout {
        objectName: "seekingState"
        anchors.centerIn: parent
        visible: root.session.seeking
        spacing: 14

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: parent.visible
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Seeking to %1…").arg(
                root.formatTime(root.session.positionMilliseconds))
            color: "white"
            font.pixelSize: 20
        }
    }

    ColumnLayout {
        objectName: "errorState"
        anchors.centerIn: parent
        visible: root.session.state === MediaSession.Error
        width: Math.min(560, root.width - 64)
        spacing: 14

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Could not display this video")
            color: "#ffb5ad"
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.session.errorMessage
            color: "#c7ccd6"
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10

            Button {
                objectName: "retryMediaButton"
                text: qsTr("Retry")
                enabled: root.session.mediaUrl.toString().length > 0
                onClicked: root.session.retry()
            }

            Button {
                text: qsTr("Open another…")
                onClicked: openDialog.open()
            }
        }
    }

    Rectangle {
        id: detailsPanel
        objectName: "playbackDetailsPanel"

        anchors {
            right: parent.right
            top: parent.top
            margins: 20
        }
        z: 20
        visible: root.showPlaybackDetails && root.sessionActive
        width: Math.min(400, Math.max(280, root.width - 40))
        height: Math.min(detailsLayout.implicitHeight + 24,
                         Math.max(1, root.height - 40))
        radius: 14
        color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.86)
        border.color: Qt.rgba(1, 1, 1, 0.16)

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
        }

        HoverHandler {
            id: detailsHover
        }

        Flickable {
            id: detailsFlickable

            anchors {
                fill: parent
                margins: 12
            }
            contentWidth: width
            contentHeight: detailsLayout.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: detailsLayout

                width: detailsFlickable.width
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Playback details")
                        color: "white"
                        font.weight: Font.DemiBold
                    }

                    IslandButton {
                        objectName: "closePlaybackDetailsButton"
                        iconSource: "icons/lucide/x.svg"
                        text: qsTr("Close playback details")
                        onClicked: root.showPlaybackDetails = false
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.session.displayName
                    color: "#d6d9e0"
                    elide: Text.ElideMiddle
                }

                DetailsSectionTitle {
                    text: qsTr("Media")
                }

                Label {
                    objectName: "playbackStateLabel"
                    Layout.fillWidth: true
                    text: root.session.containerFormat.length > 0
                        ? qsTr("%1 · %2 · %3")
                            .arg(root.playbackStateText())
                            .arg(root.session.containerFormat)
                            .arg(root.formatTime(root.session.durationMilliseconds))
                        : qsTr("%1 · %2")
                            .arg(root.playbackStateText())
                            .arg(root.formatTime(root.session.durationMilliseconds))
                    color: "#afb4bf"
                    wrapMode: Text.Wrap
                }

                DetailsSectionTitle {
                    text: qsTr("Video")
                }

                Label {
                    objectName: "selectedVideoDetailsLabel"
                    Layout.fillWidth: true
                    text: root.session.selectedVideoTrackSummary
                    color: "#afb4bf"
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("%1 · %2")
                        .arg(root.session.videoDynamicRange)
                        .arg(root.session.videoSummary)
                    color: "#afb4bf"
                    wrapMode: Text.Wrap
                }

                Label {
                    objectName: "videoSignalDetailsLabel"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.session.videoSignalSummary
                    color: "#8f96a3"
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("%1 · %2")
                        .arg(root.session.decoderName)
                        .arg(root.session.decodePath)
                    color: "#8f96a3"
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.session.hardwareFallbackReason.length > 0
                    text: qsTr("Hardware fallback: %1")
                        .arg(root.session.hardwareFallbackReason)
                    color: "#d7ae68"
                    wrapMode: Text.Wrap
                }

                DetailsSectionTitle {
                    text: qsTr("Audio")
                }

                Label {
                    objectName: "selectedAudioDetailsLabel"
                    Layout.fillWidth: true
                    text: root.session.selectedAudioTrackSummary.length > 0
                        ? root.session.selectedAudioTrackSummary
                        : qsTr("No audio track")
                    color: "#afb4bf"
                    wrapMode: Text.Wrap
                }

                DetailsSectionTitle {
                    text: qsTr("Subtitles")
                }

                Label {
                    objectName: "selectedSubtitleDetailsLabel"
                    Layout.fillWidth: true
                    text: root.session.selectedSubtitleTrackSummary
                    color: "#afb4bf"
                    wrapMode: Text.Wrap
                }

                Label {
                    objectName: "subtitleDiagnosticsLabel"
                    Layout.fillWidth: true
                    visible: root.session.subtitleError.length > 0
                    text: qsTr("Subtitles: %1")
                        .arg(root.session.subtitleError)
                    color: "#d7ae68"
                    wrapMode: Text.Wrap
                }

                DetailsSectionTitle {
                    text: qsTr("Output")
                }

                Label {
                    objectName: "outputDetailsLabel"
                    Layout.fillWidth: true
                    text: root.presentationSummary()
                    color: "#afb4bf"
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.outputState.videoColorPolicy.length > 0
                        && root.outputState.videoColorPolicy !== "Unavailable"
                    text: root.outputState.videoColorPolicy
                    color: "#8f96a3"
                    wrapMode: Text.Wrap
                }

                DetailsSectionTitle {
                    text: qsTr("Performance")
                }

                Label {
                    objectName: "videoPerformanceDetailsLabel"
                    Layout.fillWidth: true
                    text: qsTr("%1 decoded · %2 selected · %3 dropped · %4 queued")
                        .arg(root.session.decodedVideoFrames)
                        .arg(root.session.selectedVideoFrames)
                        .arg(root.session.droppedVideoFrames)
                        .arg(root.session.queuedVideoFrames)
                    color: "#8f96a3"
                    wrapMode: Text.Wrap
                }

                Label {
                    objectName: "audioDiagnosticsLabel"
                    Layout.fillWidth: true
                    visible: root.session.hasAudioOutput
                    text: qsTr("%1 · %2 · %3 ms PCM · %4 underrun frames")
                        .arg(root.session.audioBackend)
                        .arg(root.clockSourceText(
                            root.session.mediaClockSource))
                        .arg(root.session.audioQueuedMilliseconds)
                        .arg(root.session.audioUnderrunFrames)
                    color: root.session.audioClockReliable
                        ? "#8f96a3"
                        : "#d7ae68"
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Rectangle {
        id: transportIsland
        objectName: "transportIsland"

        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: 24
        }
        z: 30
        visible: root.sessionActive
        enabled: opacity > 0.05
        width: Math.min(680, Math.max(300, root.width - 32))
        height: transportLayout.implicitHeight + 24
        radius: 18
        opacity: root.controlsShouldShow ? 1 : 0
        color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 0.82)
        border.color: Qt.rgba(1, 1, 1, 0.18)

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutCubic
            }
        }

        ColumnLayout {
            id: transportLayout

            anchors {
                fill: parent
                margins: 12
            }
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    objectName: "positionLabel"
                    text: root.formatTime(
                        seekSlider.pressed
                            ? Math.round(seekSlider.valueAt(
                                seekSlider.position))
                            : root.session.positionMilliseconds)
                    color: "#f5f6fa"
                    font.features: { "tnum": 1 }
                }

                IslandSlider {
                    id: seekSlider
                    objectName: "seekSlider"

                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, root.session.durationMilliseconds)
                    live: false
                    enabled: root.session.seekable
                        && root.session.durationMilliseconds > 0
                        && !root.session.seeking
                    onMoved: {
                        if (enabled && !pressed) {
                            root.session.seekToMilliseconds(
                                Math.round(valueAt(position)))
                        }
                    }
                    onPressedChanged: {
                        root.revealControls()
                        if (enabled && !pressed) {
                            root.session.seekToMilliseconds(
                                Math.round(valueAt(position)))
                        }
                    }

                    Binding on value {
                        when: !seekSlider.pressed
                        value: root.session.positionMilliseconds
                        restoreMode: Binding.RestoreBinding
                    }
                }

                Label {
                    objectName: "durationLabel"
                    text: root.formatTime(
                        root.session.durationMilliseconds)
                    color: "#b4b8c1"
                    font.features: { "tnum": 1 }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 42

                RowLayout {
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: 6

                    IslandButton {
                        id: muteButton
                        objectName: "muteButton"

                        visible: root.session.hasAudioOutput
                        iconSource: root.session.muted
                            ? "icons/lucide/volume-x.svg"
                            : "icons/lucide/volume-2.svg"
                        text: root.session.muted
                            ? qsTr("Unmute")
                            : qsTr("Mute")
                        onClicked:
                            root.session.muted = !root.session.muted
                    }

                    IslandSlider {
                        id: volumeSlider
                        objectName: "volumeSlider"

                        visible: root.session.hasAudioOutput
                            && transportIsland.width >= 560
                        Layout.preferredWidth: 104
                        from: 0
                        to: 1
                        enabled: !root.session.muted
                        onMoved: root.session.volume = value

                        Binding on value {
                            when: !volumeSlider.pressed
                            value: root.session.volume
                            restoreMode: Binding.RestoreBinding
                        }
                    }
                }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6

                    IslandButton {
                        objectName: "seekBackwardButton"
                        Layout.alignment: Qt.AlignVCenter
                        iconSource: "icons/lucide/rotate-ccw.svg"
                        text: qsTr("Seek backward 10 seconds")
                        enabled: root.session.seekable
                            && root.session.durationMilliseconds > 0
                            && !root.session.seeking
                        onClicked: root.seekBy(-10000)
                    }

                    IslandButton {
                        id: playPauseButton
                        objectName: "playPauseButton"

                        Layout.alignment: Qt.AlignVCenter
                        prominent: true
                        enabled: !root.session.seeking
                        iconSource: root.session.playRequested
                            && !root.session.ended
                                ? "icons/lucide/pause.svg"
                                : "icons/lucide/play.svg"
                        text: root.session.playRequested
                            && !root.session.ended
                                ? qsTr("Pause")
                                : qsTr("Play")
                        onClicked: root.togglePlayback()
                    }

                    IslandButton {
                        objectName: "seekForwardButton"
                        Layout.alignment: Qt.AlignVCenter
                        iconSource: "icons/lucide/rotate-cw.svg"
                        text: qsTr("Seek forward 10 seconds")
                        enabled: root.session.seekable
                            && root.session.durationMilliseconds > 0
                            && !root.session.seeking
                        onClicked: root.seekBy(10000)
                    }
                }

                IslandButton {
                    id: moreButton
                    objectName: "moreButton"

                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    iconSource: "icons/lucide/ellipsis.svg"
                    text: qsTr("More actions")
                    onClicked: transportMenu.popup()
                }
            }
        }

        Menu {
            id: transportMenu
            objectName: "transportMenu"

            MenuItem {
                text: qsTr("Open another…")
                onClicked: openDialog.open()
            }

            MenuItem {
                objectName: "playbackDetailsMenuItem"
                text: qsTr("Show playback details")
                checkable: true
                checked: root.showPlaybackDetails
                onClicked:
                    root.showPlaybackDetails = !root.showPlaybackDetails
            }

            MenuItem {
                objectName: "blankOtherDisplaysMenuItem"
                visible: root.windowCommands.otherDisplayBlankingAvailable
                text: qsTr("Blank other displays in fullscreen")
                checkable: true
                checked: root.windowCommands.blankOtherDisplaysInFullscreen
                onClicked: root.windowCommands.blankOtherDisplaysInFullscreen =
                    !root.windowCommands.blankOtherDisplaysInFullscreen
            }

            Menu {
                id: videoTrackMenu
                objectName: "videoTrackMenu"
                title: qsTr("Video track")

                Instantiator {
                    id: videoTrackInstantiator
                    model: root.session.videoTracks

                    delegate: MenuItem {
                        objectName: "videoTrack_" + streamIndex
                        required property string label
                        required property int streamIndex
                        required property bool available

                        text: label
                        enabled: available
                        checkable: true
                        checked: streamIndex === root.session.selectedVideoStreamIndex
                        onTriggered: root.session.selectVideoStream(streamIndex)
                    }

                    onObjectAdded: (index, object) =>
                        videoTrackMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) =>
                        videoTrackMenu.removeItem(object)
                }

                MenuItem {
                    visible: videoTrackInstantiator.count === 0
                    text: qsTr("No video tracks available")
                    enabled: false
                }
            }

            Menu {
                id: audioTrackMenu
                objectName: "audioTrackMenu"
                title: qsTr("Audio track")

                Instantiator {
                    id: audioTrackInstantiator
                    model: root.session.audioTracks

                    delegate: MenuItem {
                        objectName: "audioTrack_" + streamIndex
                        required property string label
                        required property int streamIndex
                        required property bool available

                        text: label
                        enabled: available
                        checkable: true
                        checked: streamIndex === root.session.selectedAudioStreamIndex
                        onTriggered: root.session.selectAudioStream(streamIndex)
                    }

                    onObjectAdded: (index, object) =>
                        audioTrackMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) =>
                        audioTrackMenu.removeItem(object)
                }

                MenuItem {
                    visible: audioTrackInstantiator.count === 0
                    text: qsTr("No audio tracks available")
                    enabled: false
                }
            }

            Menu {
                id: subtitleMenu
                objectName: "subtitleMenu"
                title: qsTr("Subtitles")

                Instantiator {
                    id: subtitleTrackInstantiator
                    model: root.session.subtitleTracks

                    delegate: MenuItem {
                        objectName: "subtitleTrack_" + streamIndex
                        required property string label
                        required property int streamIndex
                        required property bool selected
                        required property bool available

                        text: label
                        enabled: available
                        checkable: true
                        checked: selected
                        onTriggered:
                            root.session.selectSubtitleStream(streamIndex)
                    }

                    onObjectAdded: (index, object) =>
                        subtitleMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) =>
                        subtitleMenu.removeItem(object)
                }

                MenuItem {
                    visible: subtitleTrackInstantiator.count === 1
                    text: qsTr("No subtitles available")
                    enabled: false
                }
            }

            MenuItem {
                objectName: "hdrLabMenuItem"
                text: qsTr("HDR Lab")
                onClicked: root.hdrLabRequested()
            }

            MenuSeparator {}

            MenuItem {
                objectName: "closeMediaButton"
                text: qsTr("Close video")
                onClicked: root.session.cancel()
            }
        }
    }
}
