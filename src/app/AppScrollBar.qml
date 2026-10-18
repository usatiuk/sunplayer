import QtQuick
import QtQuick.Controls

ScrollBar {
    id: root

    // QTBUG-138957: Fluent's Fusion fallback uses near-background stroke colors.
    Binding on palette.mid {
        when: Qt.platform.os === "windows"
        value: root.palette.text
    }
    Binding on palette.dark {
        when: Qt.platform.os === "windows"
        value: root.palette.text
    }
}
