# Jellyfin header and conventional navigation

## Accepted scope

Keep Home/library tabs beside the Jellyfin title in one header. Use the same
application palette and black page background as Open video; add space between
illustrated show headers and season cards. Preserve Qt's default scrolling.
Support mouse Back/Forward and standard keyboard navigation across Jellyfin
browse history and Open video ↔ Jellyfin. Playback Back closes the video and
returns to its opener; Forward must never restart closed playback.

No new dependency, navigation framework, generic router or StackView migration.
Qt StackView supplies visual push/pop page ownership, not automatic browser
Forward history or mouse-side-button commands. Existing page ownership remains.

## Implementation and research

* JellyfinBrowser owns visited typed navigation paths and one cursor, replacing
  the old destructive ancestor stack. New visits discard the forward tail;
  authentication loss/logout clear history. Each restored location reloads
  server data through existing request guards.
* AppShell owns shared Back/Forward commands and the one Open/Jellyfin return
  capability. A thumb-button-only MouseArea uses them; the toolbar arrow uses
  the separate logical Up command, returning to Open video from a library root.
  PresentationWindow matches Qt's standard Back/Forward key sequences and
  emits the same intent. Browser Backspace remains ordinary editing input.
* A small shared applicationPalette function provides the existing palette to
  main and the real-QML test harness. The existing shell Item applies its
  application colors through SystemPalette, since Quick Controls otherwise
  use style-specific palettes; no theme layer is added. Tabs shrink and
  use their existing horizontal Flickable. Illustrated headers have a 20 px
  gap before gallery cards.

Primary research: [Qt StackView](https://doc.qt.io/qt-6/qml-qtquick-controls-stackview.html),
[Qt standard keys](https://doc.qt.io/qt-6/qkeysequence.html#standard-shortcuts),
[Qt Shortcut](https://doc.qt.io/qt-6/qml-qtquick-shortcut.html),
[KDE Elisa ContentView](https://github.com/KDE/elisa/blob/master/src/qml/ContentView.qml),
[Kirigami PageRow](https://github.com/KDE/kirigami/blob/master/src/controls/PageRow.qml).
These production apps explicitly connect ordinary mouse/keyboard inputs to
shared navigation commands rather than receiving history automatically.

## CI prerequisite and delivery constraints

CI for 419766d passed Linux and AppImage but failed Windows: new shell-test QML
imports generated a Ninja cache directory containing `_../src`. The agent's
minimal fix reuses the existing test-only QT_QML_SKIP_CACHEGEN property for all
five new imports; production QML caching remains enabled. Pushed separately as
`df73aac`; its [CI run](https://github.com/usatiuk/sunplayer/actions/runs/39123818887)
must pass before any further push. All UI/navigation work continues locally.
No Windows/macOS builds are run on this Linux host.

## Validation

Three independent read-only implementation reviews found no material
correctness/simplicity defect. Planned public-boundary checks cover browser
Back/Forward, branch truncation and auth clearing; real QQuickWindow thumb-button
routing, Open/Jellyfin return behavior, compact header geometry, shared palette
contrast and populated screenshots under Basic/Fusion.

Shell command-signal tests do not exercise PresentationWindow's native keyboard
matching. Standard keys and Backspace in the actual redirected window remain
manual acceptance unless an existing application scenario covers them.


Final local validation: Linux Debug build and both QML lint targets passed.
`jellyfin`, `qml-shell`, `qml-shell-fusion` and `packaged-qml-module` passed
4/4 (43.62 s). New cases exercise history branching/auth reset and actual
QQuickWindow thumb-button events, as well as downstream command-signal routes.
Geometry checks cover a one-row header at 1280/800/760 px, black page background
and the 20 px illustrated-header gap. Populated Basic/Fusion captures were
inspected. Checkbox contrast remains covered with the actual shared application
palette; the initial palette-only attempt failed that check and was corrected
by explicit palette inheritance at the existing shell root.

Header polish requested subsequently: Back is a compact arrow-only ToolButton
with accessible name/tooltip; the Jellyfin title has 12 px additional margins on
each side. Illustrated backgrounds are square and span the full left/right
window edges. Body text and gallery/episode cards keep 24 px insets, and the
20 px gap before season cards remains. Uses ordinary layout margins and delegate
containers, preserving native scrolling. Shell geometry tests assert the show
header starts at x=0 and spans the root width.

## Subsequent corrections

The toolbar arrow follows the logical hierarchy rather than visits. Same-level
Home/Movies/Shows switches remain in mouse/keyboard history only. Home items fetch
the official ancestor array, reversed into a path and trimmed above the user's
library. Grouped UserView IDs can differ; returned CollectionFolder/Series/Season
parents remain usable without guessing which tab they belong to. A pending Home
selection is invalidated on media close/replacement, preventing a late ancestor
reply from reopening cancelled playback. No Qt hover customization was added.

The Windows-only Absolute PQ setting and status row are hidden on Linux/macOS;
saved preferences and Windows availability behavior are preserved.

CI for df73aac passed Linux/AppImage but failed Windows deployment: windeployqt
searched the Qt SDK for QtKeychain's third-party DLL. The isolated fix 30404b1
links only Windows QtKeychain statically (macOS already did). Pushed immediately
as explicitly requested; its
[CI run](https://github.com/usatiuk/sunplayer/actions/runs/39127007680) passed
Windows, Linux and AppImage, satisfying the prerequisite for this follow-up push.

Final validation after logical-Up/PQ corrections: Linux Debug build and
all_qmllint passed; settings-dialog, qml-shell (Basic), qml-shell-fusion,
packaged-qml-module and jellyfin passed 5/5 (44.44 s). Full-width square-header
capture was inspected. Focused independent reviews caught and verified fixes
for grouped library IDs and late Home-selection playback after cancellation.
The latest Debug executable is `build/linux-debug/sunplayer`. The updated
AppImage and checksum are in `out/appimage/`; the Docker rebuild, isolated
packaging/QML checks and playback smoke passed (position 1523 ms, Pulse audio,
73142 presented frames), and the checksum verified. Artifacts remain ignored.
Windows CI for 30404b1 passed alongside Linux and AppImage.

## Windows regression correction

Actual Windows acceptance exposed white text on native light buttons. The
shell-wide SystemPalette propagation in 1198f56 forced native button text but
could not recolor Windows' native-painted backgrounds. Remove this propagation
and the global QApplication palette/forced dark scheme; ordinary controls use
system colors. Preserve explicit light text only on custom dark surfaces and
checkbox labels. Toolbar actions use ordinary compact native buttons to keep
foreground/background pairs correct in every interaction state.
Rename the checkbox to Remember me. The extracted ApplicationPalette helper
is no longer needed. Stock-control palette comparisons cover main/login
buttons, and hosted Windows runs the Windows style alongside Basic/Fusion.

The same acceptance reported HTTP 0. The isolated MSIX manifest had no network
capabilities; add only general-use internetClient/privateNetworkClientServer
for public/LAN Jellyfin. Do not disable isolation or add speculative credential
permissions. HTTP 0 now exposes a safe symbolic Qt network-error category.
Linux tests cannot validate the installed Windows AppContainer or its
Credential Manager access; both remain actual packaged acceptance checks.

The multi-track audit found no unrelated feature scope creep. It identified
the global palette override and missing native-style coverage, plus an
Open/Jellyfin history edge recorded in DEFERRED rather than expanding this fix.

Correction validation: Linux Debug rebuild and all_qmllint passed. The five
affected suites (settings-dialog, Basic/Fusion qml-shell, packaged-qml-module,
jellyfin) passed 5/5 in 44.95 s. Enabled/disabled stock-button comparisons and
real refused TCP connection behavior pass. Linux screenshots retain readable
canvas labels and stock button color pairs. Independent palette/network/scope
reviews found no remaining blocker after replacing toolbar ToolButtons with
standard Buttons. No Windows/macOS build or package execution ran locally.
