# Qt Quick native-dialog parenting under redirected rendering

Date: 2026-08-02

## Finding

Qt 6.11.1 resolves an unset `Dialog.parentWindow` from the dialog's nearest
item and therefore selects Sunroom's hidden redirected `QQuickWindow`.
Opening a window-modal Cocoa file dialog then materializes that hidden window
and attaches the sheet to the resulting blank toplevel.

For the pinned Qt release, the smallest supported application fix is to set
`FileDialog.parentWindow` to Sunroom's visible `PresentationWindow` on macOS.
The property is the public dialog-parent contract, and the rebuilt application
is user-confirmed to open the sheet without the second window.

Keep the override macOS-only. Qt Quick's non-native file-dialog implementation
requires a `QQuickWindow`; an ordinary `QWindow` is not a valid fallback
parent. Sunroom's normal macOS path uses the native dialog. Deliberate
non-native-dialog support would need to retain the redirected Quick window as
its parent and is not part of the current port.

## Upstream status

Qt commit
[`bd1da1d7972f02a3be6e872a5fa05f73556d56d3`](https://github.com/qt/qtdeclarative/commit/bd1da1d7972f02a3be6e872a5fa05f73556d56d3)
changes `QQuickAbstractDialog::windowForOpen()` to consult
`QQuickRenderControl::renderWindowFor()`. The commit is marked for the 6.12,
6.11, and 6.8 branches but is absent from the inspected `v6.11.1` source.
When Sunroom updates Qt, verify the shipped patch level and both native and
deliberately forced non-native dialog behavior before removing the explicit
macOS binding.

## Primary evidence

* [Qt 6.11 Dialog `parentWindow`](https://doc.qt.io/qt-6.11/qml-qtquick-dialogs-dialog.html)
* [Qt 6.11 `QQuickRenderControl`](https://doc.qt.io/qt-6.11/qquickrendercontrol.html)
* [Qt 6.11.1 `QQuickAbstractDialog`](https://github.com/qt/qtdeclarative/blob/v6.11.1/src/quickdialogs/quickdialogs/qquickabstractdialog.cpp)
* [Qt 6.11.1 non-native file-dialog parent check](https://github.com/qt/qtdeclarative/blob/v6.11.1/src/quickdialogs/quickdialogsquickimpl/qquickplatformfiledialog.cpp)
