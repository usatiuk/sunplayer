import QtQuick

Item {
    id: root

    required property PresentationOutputState presentationOutput
    required property PresentationSettings presentationPolicy
    required property DiagnosticVideoSource diagnosticSource
    required property VideoViewportState viewportState

    // These two properties form the page-facing viewport contract. The first
    // real Player page can become active without changing presentation code.
    readonly property rect activeVideoViewportRect:
        Qt.rect(hdrLabPage.x + hdrLabPage.videoViewportRect.x,
                hdrLabPage.y + hdrLabPage.videoViewportRect.y,
                hdrLabPage.videoViewportRect.width,
                hdrLabPage.videoViewportRect.height)
    readonly property bool activeVideoViewportVisible:
        hdrLabPage.visible && hdrLabPage.videoViewportVisible

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

    HdrLabPage {
        id: hdrLabPage

        anchors.fill: parent
        outputState: root.presentationOutput
        presentationSettings: root.presentationPolicy
        videoSource: root.diagnosticSource
    }
}
