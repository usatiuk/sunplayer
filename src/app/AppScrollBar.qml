import QtQuick
import QtQuick.Controls

ScrollBar {
    // QTBUG-138957: Fluent's Fusion fallback uses near-background stroke colors.
    Binding on palette.mid {
        when: Qt.platform.os === "windows"
        value: "#888888"
    }
    Binding on palette.dark {
        when: Qt.platform.os === "windows"
        value: "#aaaaaa"
    }
}
