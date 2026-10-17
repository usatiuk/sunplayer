# Qt control contrast, 2026-10-17

Scope: Qt 6.10.2 on Ubuntu 26.04 and Qt 6.11.1 Windows/macOS style sources.
The AppImage's portal theme exposed a light control palette on the dark player
surface. The idle overflow action became white-on-white specifically when its
ToolButton background appeared, including Tab-reason keyboard focus. Different
idle/focused states must not be mistaken for different packaged code.

## Findings and bounded fixes

* Fusion `ToolButton` preserves the original colors of our white ellipsis SVG.
  A regular icon-only `Button` has a persistent background; explicitly bind
  `icon.color` to `palette.buttonText` so foreground/background stay paired
  across styles. Preserve the square shape with `implicitWidth: implicitHeight`.
* Merely tinting the transparent ToolButton to a light theme's black foreground
  makes its idle icon black on the dark player. Reusing the transport's custom
  button also requires addressing its previously missing focus indication.
  Neither is preferable to a standard Button for this idle action.
* Fusion Switch labels read `palette.text`; Basic, Windows and macOS read
  `palette.windowText`. Set both roles on the four deliberately dark HDR Lab
  controls. Replacing one role with the other would regress another style.
* Custom transport/titlebar buttons already own their backgrounds. Include
  `visualFocus` in their existing hover highlight instead of creating a new
  focus framework, platform conditional, palette override or copied style.

Native Wayland probes with the bundled Qt reproduced the focused white-on-white
ToolButton and readable regular Button under light portal and dark GTK themes.
They also reproduced the Fusion switch's dark text despite a white windowText
role, and verified that setting text preserves the intended light label.
The source-checked Windows/macOS styles are not native runtime evidence.

Qt's portal theme delegates palette queries but does not override/forward
`requestColorScheme`. GTK implements it; the inherited platform-theme handler
ignores the requested scheme. This omission remains in Qt 6.11.1, the 6.11
branch and development header checked. Fixing application control contrast
does not require patching Qt or changing portal selection. Keep that Linux
integration issue separate from shared UI behavior.

## Primary sources

* [Qt icon tint contract](https://doc.qt.io/qt-6.10/qtquickcontrols-icons.html)
* [Fusion Button](https://github.com/qt/qtdeclarative/blob/6.10/src/quickcontrols/fusion/Button.qml)
  and [ToolButton](https://github.com/qt/qtdeclarative/blob/6.10/src/quickcontrols/fusion/ToolButton.qml)
* [Fusion Switch](https://github.com/qt/qtdeclarative/blob/6.10/src/quickcontrols/fusion/Switch.qml),
  [Windows Switch](https://github.com/qt/qtdeclarative/blob/v6.11.1/src/quickcontrols/windows/Switch.qml),
  [macOS Switch](https://github.com/qt/qtdeclarative/blob/v6.11.1/src/quickcontrols/macos/Switch.qml)
* [Portal theme](https://github.com/qt/qtbase/blob/v6.11.1/src/plugins/platformthemes/xdgdesktopportal/qxdgdesktopportaltheme.h),
  [GTK theme](https://github.com/qt/qtbase/blob/6.10/src/plugins/platformthemes/gtk3/qgtk3theme.cpp),
  [default request handler](https://github.com/qt/qtbase/blob/6.10/src/gui/kernel/qplatformtheme.cpp)
