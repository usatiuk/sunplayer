pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VideoPage {
    id: root
    required property JellyfinBrowser browser
    signal backRequested
    windowShortcutsBlocked: true

    Rectangle {
        anchors.fill: parent
        objectName: "jellyfinBackground"
        color: "#000000"
    }
    Control {
        anchors.fill: parent
        padding: 24
        contentItem: ColumnLayout {
            spacing: 18
            RowLayout {
                Layout.fillWidth: true
                ToolButton {
                    objectName: "jellyfinBackButton"
                    text: "←"
                    font.pixelSize: 22
                    implicitWidth: 40
                    implicitHeight: 40
                    Accessible.name: qsTr("Up one level")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Up one level")
                    enabled: !root.browser.busy || !root.browser.canGoUp
                    onClicked: root.backRequested()
                }
                Label {
                    objectName: "jellyfinTitle"
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    text: "Jellyfin"
                    color: "#f1f3f7"
                    font.pixelSize: 23
                    font.weight: Font.DemiBold
                }
                Flickable {
                    objectName: "jellyfinTabs"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: root.browser.authenticated
                    Layout.preferredHeight: tabs.implicitHeight
                    contentWidth: tabs.implicitWidth
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    clip: true
                    Row {
                        id: tabs
                        spacing: 8
                        Button {
                            text: qsTr("Home")
                            checkable: true
                            checked: root.browser.isHome
                            highlighted: checked
                            enabled: !root.browser.busy
                            onClicked: root.browser.home()
                        }
                        Repeater {
                            model: root.browser.libraries
                            delegate: Button {
                                required property var modelData
                                required property int index
                                text: modelData.name
                                checkable: true
                                checked: root.browser.activeLibraryId === modelData.itemId
                                highlighted: checked
                                enabled: !root.browser.busy
                                onClicked: root.browser.selectLibrary(index)
                            }
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    visible: !root.browser.authenticated
                }
                BusyIndicator {
                    running: root.browser.busy
                    visible: running
                    implicitWidth: 28
                    implicitHeight: 28
                }
                ToolButton {
                    text: qsTr("Refresh")
                    visible: root.browser.authenticated
                    enabled: !root.browser.busy
                    onClicked: root.browser.retry()
                }
                ToolButton {
                    text: qsTr("Sign out")
                    visible: root.browser.authenticated
                    enabled: !root.browser.busy
                    onClicked: root.browser.logout()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: root.browser.message.length > 0
                text: root.browser.message
                color: "#efba7b"
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                Layout.maximumWidth: 440
                Layout.fillWidth: true
                visible: !root.browser.authenticated
                spacing: 12
                Item {
                    Layout.fillHeight: true
                }
                Label {
                    text: qsTr("Connect to your library")
                    color: "#f4f5f7"
                    font.pixelSize: 22
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Enter your Jellyfin server address and sign in.")
                    color: "#b3b8c4"
                    wrapMode: Text.Wrap
                }
                TextField {
                    id: serverField
                    objectName: "jellyfinServer"
                    Layout.fillWidth: true
                    placeholderText: "https://jellyfin.example.com"
                    text: root.browser.savedServer
                    enabled: !root.browser.busy
                    onAccepted: usernameField.forceActiveFocus()
                }
                TextField {
                    id: usernameField
                    objectName: "jellyfinUsername"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Username")
                    enabled: !root.browser.busy
                    onAccepted: passwordField.forceActiveFocus()
                }
                TextField {
                    id: passwordField
                    objectName: "jellyfinPassword"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Password")
                    echoMode: TextInput.Password
                    enabled: !root.browser.busy
                    onAccepted: signInButton.clicked()
                }
                CheckBox {
                    id: remember
                    objectName: "jellyfinRemember"
                    text: qsTr("Remember this login securely")
                    checked: true
                    enabled: !root.browser.busy
                }
                Button {
                    id: signInButton
                    objectName: "jellyfinSignIn"
                    Layout.fillWidth: true
                    text: qsTr("Sign in")
                    enabled: !root.browser.busy && serverField.text.length > 0 && usernameField.text.length > 0
                    onClicked: {
                        root.browser.login(serverField.text, usernameField.text, passwordField.text, remember.checked);
                        passwordField.clear();
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
            }
            Loader {
                id: contentLoader
                Layout.leftMargin: -24
                Layout.rightMargin: -24
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.browser.authenticated
                active: root.browser.authenticated
                sourceComponent: root.browser.isHome ? homeView : catalogView
            }
        }
    }

    Component {
        id: homeView
        Flickable {
            id: homeScroll
            contentWidth: width
            contentHeight: homeContent.implicitHeight
            clip: true
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: homeContent
                x: 24
                width: homeScroll.width - 48
                spacing: 28
                Label {
                    text: qsTr("My media")
                    color: "#f1f3f7"
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
                Grid {
                    id: libraryGrid
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.floor(width / 300))
                    spacing: 20
                    Repeater {
                        model: root.browser.libraries
                        delegate: JellyfinPoster {
                            required property var modelData
                            required property int index
                            item: modelData
                            landscape: true
                            width: Math.min(420, (homeContent.width - 20 * (libraryGrid.columns - 1)) / libraryGrid.columns)
                            enabled: !root.browser.busy
                            onClicked: root.browser.selectLibrary(index)
                        }
                    }
                }
                JellyfinShelf {
                    Layout.fillWidth: true
                    title: qsTr("Continue Watching")
                    items: root.browser.continueWatching
                    landscape: true
                    enabled: !root.browser.busy
                    onActivated: function (row) {
                        root.browser.openHomeItem(JellyfinBrowser.ContinueWatching, row);
                    }
                }
                JellyfinShelf {
                    Layout.fillWidth: true
                    title: qsTr("Recently Added in Movies")
                    items: root.browser.recentMovies
                    enabled: !root.browser.busy
                    onActivated: function (row) {
                        root.browser.openHomeItem(JellyfinBrowser.RecentMovies, row);
                    }
                }
                JellyfinShelf {
                    Layout.fillWidth: true
                    title: qsTr("Recently Added in Shows")
                    items: root.browser.recentShows
                    enabled: !root.browser.busy
                    onActivated: function (row) {
                        root.browser.openHomeItem(JellyfinBrowser.RecentShows, row);
                    }
                }
            }
        }
    }
    Component {
        id: catalogView
        Loader {
            id: catalogLoader
            sourceComponent: root.browser.currentItem.kind === "Season" ? episodes : gallery
            Component {
                id: moreFooter
                Item {
                    width: catalogLoader.width
                    height: moreButton.visible ? moreButton.implicitHeight + 20 : 0
                    Button {
                        id: moreButton
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.browser.hasMore
                        enabled: !root.browser.busy
                        text: qsTr("Load more")
                        onClicked: root.browser.loadMore()
                    }
                }
            }
            Component {
                id: gallery
                GridView {
                    id: posters
                    objectName: "jellyfinItems"
                    clip: true
                    model: root.browser
                    readonly property int columns: Math.max(1, Math.floor((width - 48) / 170))
                    // Keep 24 px outer insets and 20 px card gaps under the full-width header.
                    cellWidth: (width - 48 + 20) / columns
                    cellHeight: (cellWidth - 20) * 1.5 + 76
                    ScrollBar.vertical: ScrollBar {}
                    header: Item {
                        width: posters.width
                        height: galleryDetail.implicitHeight + (galleryDetail.illustrated ? 20 : 0)
                        JellyfinDetail {
                            id: galleryDetail
                            width: parent.width
                            item: root.browser.currentItem
                            enabled: !root.browser.busy
                            onPlayRequested: function (fromStart) {
                                root.browser.playCurrent(fromStart);
                            }
                            onWatchedRequested: function (watched) {
                                root.browser.setCurrentWatched(watched);
                            }
                        }
                    }
                    delegate: Item {
                        id: posterCell
                        required property var itemData
                        required property int index
                        width: posters.cellWidth
                        height: posters.cellHeight
                        JellyfinPoster {
                            x: 24
                            item: posterCell.itemData
                            width: parent.width - 20
                            enabled: !root.browser.busy
                            onClicked: root.browser.enter(posterCell.index)
                        }
                    }
                    footer: moreFooter
                }
            }
            Component {
                id: episodes
                ListView {
                    id: episodeList
                    objectName: "jellyfinEpisodes"
                    clip: true
                    spacing: 16
                    model: root.browser
                    ScrollBar.vertical: ScrollBar {}
                    header: JellyfinDetail {
                        width: episodeList.width
                        item: root.browser.currentItem
                    }
                    delegate: Item {
                        id: episodeCell
                        required property var itemData
                        required property int index
                        width: episodeList.width
                        height: episodeCard.implicitHeight
                        JellyfinEpisode {
                            id: episodeCard
                            x: 24
                            item: episodeCell.itemData
                            width: parent.width - 48
                            enabled: !root.browser.busy
                            onPlayRequested: function (fromStart) {
                                root.browser.play(episodeCell.index, fromStart);
                            }
                            onWatchedRequested: function (watched) {
                                root.browser.setWatched(episodeCell.index, watched);
                            }
                        }
                    }
                    footer: moreFooter
                }
            }
        }
    }
}
