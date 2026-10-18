# Use Qt dark appearance and Fluent Windows controls

## Accepted scope and evidence

The player/browser canvas is dark but Windows Qt Quick scrollbars remain light.
Qt 6.11.1's default Quick Windows style explicitly installs a light palette;
its native control renderer does not support dark appearance. QWidget dialogs
use a separate style: Windows11 supports the application's dark color scheme.
The user chose Qt's built-in FluentWinUI3 Quick style on Windows.

Request app-wide Dark through QStyleHints after QApplication construction and
before creating UI. Select FluentWinUI3 only on Windows before QML imports.
Retain platform-default Linux Fusion/macOS native Quick styles and the existing
widget style. Windows and Cocoa implement the appearance request; Linux support
depends on the platform theme plugin. Do not force palettes or individual text
roles, customize scrollbars, add theme settings, replace native pickers, or add
platform workarounds. OS-owned dialogs may retain their own theme behavior.

## Validation and review

Run existing Basic/Fusion shell coverage with the dark request; update the
Windows stock-style case to FluentWinUI3. Preserve the stock-control palette
comparison that guards readable text/background pairs. Check existing QML
module deployment and hosted packaged-module smoke coverage. No Windows/macOS
build or execution locally. Keep deferred QtKeychain build changes untouched.
Independent reviews cover native/Quick style behavior, minimality and packaging.

## Sources

- https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop
- https://doc.qt.io/qt-6/qtquickcontrols-styles.html
- https://doc.qt.io/qt-6/qtquickcontrols-fluentwinui3.html
- https://github.com/qt/qtdeclarative/blob/v6.11.1/src/quicknativestyle/qstyle/windows/qquickwindowsstyle.cpp
- https://github.com/qt/qtbase/blob/v6.11.1/src/plugins/platforms/windows/qwindowstheme.cpp
- https://github.com/qt/qtbase/blob/v6.11.1/src/plugins/platforms/cocoa/qcocoatheme.mm

## Implementation and local evidence

Implemented the app-wide QStyleHints Dark request and Windows-only stock
FluentWinUI3 selection before QML imports. No palette overrides, widget-style
replacement, dependency changes or custom scrollbar behavior were added.

Linux debug build, Basic/Fusion shell scenarios, packaged QML module check and
all_qmllint passed. An optional local Fluent probe could not load the module:
the Linux distribution Qt 6.10.2 installation does not include it. No dependency
was installed. Windows CI runs the Fluent shell scenario and validates deployed
imports through --verify-qml; the latter compiles rather than instantiates the
component. No native Windows/macOS build or execution ran locally.

Three independent read-only implementation reviews covered behavior, simplicity,
and delivery/tests/docs. No blocking findings or runtime cleanup were identified.
Native Windows appearance remains a user acceptance check after CI packaging.
