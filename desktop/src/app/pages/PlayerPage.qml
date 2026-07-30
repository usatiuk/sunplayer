import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

VideoPage {
    id: root

    required property MediaSession session

    readonly property bool ready:
        session.state === MediaSession.Ready && session.hasFrame
    readonly property bool sessionActive:
        ready || session.seeking

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

    videoViewportRect:
        Qt.rect(videoFrame.x, videoFrame.y,
                videoFrame.width, videoFrame.height)
    videoViewportVisible:
        visible && ready

    FileDialog {
        id: openDialog

        title: qsTr("Open media")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("Media files (*)")
        ]
        onAccepted: root.session.openMedia(selectedFile)
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.ready
        color: "#090b10"
    }

    Item {
        id: videoFrame

        x: 24
        y: 72
        width: Math.max(1, root.width - 48)
        height: Math.max(
            1,
            root.height - y
                - (timelineBar.visible ? timelineBar.height + 24 : 24))

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: root.ready ? "#303746" : "transparent"
            border.width: 1
        }
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
            font.pixelSize: 22
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Video playback is available; audio comes next.")
            color: "#8e97a8"
        }

        Button {
            objectName: "openMediaButton"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Open media…")
            onClicked: openDialog.open()
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
            text: qsTr("Opening %1…").arg(
                root.session.displayName)
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
                root.formatTime(
                    root.session.positionMilliseconds))
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
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: 16
        }
        visible: root.sessionActive
        height: readyLayout.implicitHeight + 16
        radius: 8
        color: Qt.rgba(17 / 255, 19 / 255, 24 / 255, 0.92)
        border.color: "#303746"

        RowLayout {
            id: readyLayout

            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: 12
            }
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Label {
                    Layout.fillWidth: true
                    text: root.session.displayName
                    color: "white"
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("%1 · %2 · %3 · %4")
                        .arg(root.session.ended
                            ? qsTr("Ended")
                            : root.session.seeking
                                ? qsTr("Seeking")
                            : root.session.playing
                                ? qsTr("Playing")
                                : qsTr("Paused"))
                        .arg(root.session.videoSummary)
                        .arg(root.session.decoderName)
                        .arg(root.session.decodePath)
                    color: "#9ca6b8"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.session.hardwareFallbackReason.length > 0
                    text: qsTr("Hardware decode fallback: %1")
                        .arg(root.session.hardwareFallbackReason)
                    color: "#d2a85d"
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("%1 decoded · %2 selected · %3 dropped · %4 queued")
                        .arg(root.session.decodedVideoFrames)
                        .arg(root.session.selectedVideoFrames)
                        .arg(root.session.droppedVideoFrames)
                        .arg(root.session.queuedVideoFrames)
                    color: "#778195"
                    elide: Text.ElideRight
                }
            }

            Button {
                objectName: "playPauseButton"
                enabled: !root.session.seeking
                text: root.session.ended
                    ? qsTr("Replay")
                    : root.session.playing
                        ? qsTr("Pause")
                        : qsTr("Play")
                onClicked: root.session.playing
                    ? root.session.pause()
                    : root.session.play()
            }

            Button {
                text: qsTr("Open another…")
                onClicked: openDialog.open()
            }

            Button {
                objectName: "closeMediaButton"
                text: qsTr("Close")
                onClicked: root.session.cancel()
            }
        }
    }

    Rectangle {
        id: timelineBar

        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 16
        }
        visible: root.sessionActive
        height: 56
        radius: 8
        color: Qt.rgba(17 / 255, 19 / 255, 24 / 255, 0.92)
        border.color: "#303746"

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: 12
            }
            spacing: 10

            Label {
                objectName: "positionLabel"
                text: root.formatTime(
                    root.session.positionMilliseconds)
                color: "white"
                font.features: {
                    "tnum": 1
                }
            }

            Slider {
                id: seekSlider
                objectName: "seekSlider"

                Layout.fillWidth: true
                from: 0
                to: Math.max(
                    1, root.session.durationMilliseconds)
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
                color: "#9ca6b8"
                font.features: {
                    "tnum": 1
                }
            }
        }
    }
}
