import QtQuick

// Common page-facing contract consumed by AppShell and the presentation layer.
Item {
    property rect videoViewportRect: Qt.rect(0, 0, 0, 0)
    property bool videoViewportVisible: false
}
