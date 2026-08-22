# Single-file media ingress

Status: Complete

## Goal

Let a user open one local media file through the existing file dialog, a
command-line or Windows file-association launch, and a native file drop onto
the presentation window. Every ingress must reach the existing
`PresentationWindow::openMedia()` / `MediaSession` replacement behavior
without adding playlist or application-instance coordination semantics.

## Grounded starting state

* `main()` accepts an optional positional local path and opens its first value
  through `PresentationWindow::openMedia()`.
* `PlayerPage` sends the file dialog result directly to `MediaSession`, while
  `PresentationWindow::openMedia()` is not exposed to QML.
* The visible `PresentationWindow` forwards selected input to a redirected
  `QQuickWindow`; drag-and-drop is not handled at either boundary.
* The Windows full-trust MSIX is built and schema-validated in CI, but its
  manifest intentionally declares no file associations.
* `MediaSession` already rejects non-local URLs and replaces an active open or
  playback generation without blocking the UI.

## Design

* Keep `PresentationWindow::openMedia()` as the single application ingress,
  make it QML-callable, and route the existing file dialog through it.
* Emit one application-open notification so `AppShell` returns from HDR Lab
  to Player. QML remains the page and active-source-route authority.
* Handle `DragEnter` and `Drop` on the visible `PresentationWindow`. Use one
  stateless helper to accept exactly one valid local URL with a nonempty local
  path when `CopyAction` is available. Force that action so opening can never
  tell the drag source to move or delete a file.
* Do not handle `DragMove` or `DragLeave` without hover visuals or region
  restrictions. Qt retains an accepted enter action across movement.
* Do not synchronously stat or canonicalize a dropped path. `MediaSession` and
  FFmpeg own open failure, and a GUI-thread probe could block on a UNC share.
* Reject more than one ordinary positional media argument instead of silently
  ignoring later values.
* Declare one `uap3:FileTypeAssociation` named `sunplayermedia` with
  `MultiSelectModel="Single"`. Register the common standalone video-container
  extensions backed by the pinned FFmpeg demuxer families. Keep specialist,
  raw-stream, playlist, image, and audio-only extensions out of the product
  association even when FFmpeg can parse them.
* Use the standard full-trust association launch. Windows supplies the local
  path as a process argument, so no custom verb, parameters, WinRT activation
  dispatcher, registry write, or additional capability is needed.

The implementation retains these invariants:

* One local URL is accepted; zero, multiple, and non-local drop URLs are not.
* Opening never implies moving, deleting, or modifying the source file.
* The visible native window owns drop acceptance. The redirected QML scene
  does not gain a partial general-purpose drag-event forwarding model.
* File-dialog, launch, and drop requests use the existing application open
  boundary and existing latest-open-wins session behavior.
* Windows registers supported file types but never takes over user defaults.
* Windows-specific declarations remain in Windows packaging; the application
  open boundary remains usable by later macOS and Linux activation adapters.

## Explicit non-goals

* Playlists, queues, multi-file opening, folders, recent files, or session
  restoration.
* Single-instance enforcement or interprocess request forwarding.
* Remote URLs, protocol handlers, streaming, or source caching.
* A general redirected drag/drop, touch, tablet, accessibility, or input-method
  forwarding framework.
* Automatically changing the user's Windows default applications.

## Implementation and validation

1. Route file-dialog opening through the application boundary and handle one
   local native file drop at the visible window.
2. Return application-open requests to Player and reject multiple positional
   paths without adding instance coordination.
3. Declare the reviewed Windows association with explicit single-file
   selection behavior.
4. Add focused pure drop-policy, QML command/route, and real-window event
   coverage; retain the existing actual-application positional-open scenario
   and package generation as the launch and manifest-schema checks.
5. Synchronize application, UI, packaging, testing, root-plan, and deferred
   documentation with behavior actually validated.
6. Build and run affected checks, package the Windows artifact, perform
   independent correctness/architecture/simplicity review, resolve findings,
   and revalidate before shipping.

## Grounding sources

* Microsoft documents `windows.fileTypeAssociation`, the `Single` selection
  model, and user-owned default-app selection:
  <https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/desktop-to-uwp-extensions>
* Qt documents drop-action replacement and local/UNC `QUrl` semantics:
  <https://doc.qt.io/qt-6/qdropevent.html#setDropAction>
  and <https://doc.qt.io/qt-6/qurl.html#isLocalFile>.
* Pinned Qt 6.11.1 sends drag events to the target `QWindow` and preserves an
  accepted enter action across drag movement. Production drag handling uses
  public Qt events; the bounded actual-window probe uses private QPA test
  instrumentation already present in the executable.

## Outcome

Shipped design:

* `PresentationWindow::openMedia()` is the one application ingress for startup,
  the file dialog, and native drops. Its notification returns HDR Lab to Player
  while QML retains page and active-source-route ownership.
* The visible native window accepts exactly one local or UNC URL when copy is
  available, always answers with copy, and performs no synchronous filesystem
  probe. The helper is stateless and shared by production event handling and a
  focused policy test.
* Ordinary startup rejects multiple positional paths with an explicit process
  diagnostic. The Windows actual-window probe enters Qt's platform boundary
  and proves copy acceptance plus exact delivery to `MediaSession`.
* The full-trust MSIX exposes one `Single` association for 13 common standalone
  video extensions across seven pinned FFmpeg demuxer families. It adds no
  custom activation handler, registry path, protocol, capability, or default-
  app takeover.
* No playlist, queue, multi-file behavior, single-instance IPC, filesystem
  preflight, general redirected drag framework, or premature cross-platform
  association registry was introduced.

Validation on Windows/MSVC/Qt 6.11.1:

* Focused ingress, QML, CLI, demuxer, and actual-playback checks passed.
* The complete Debug tree rebuilt after review and all 34/34 registered CTests
  passed together.
* The RelWithDebInfo player rebuilt and installed; the installed executable's
  packaged-QML verification passed.
* `winapp` created the final unsigned development MSIX successfully. The packed
  manifest contains the reviewed `uap3` association and exactly 13 extensions;
  the validation artifact SHA-256 is
  `2C26D60CDE196E6746788D4110BCE7341A5EDE5B90118580045DA1D1FFBF05DF`.
* Independent Windows/correctness, architecture/failure, and explicit
  simplicity/tests/docs reviews found no blocker or important issue. Four
  minor evidence/clarity findings were resolved before the final validation.

Commit identifier: `Open local media from Windows shell` (the implementation
commit containing this completed plan).

Remaining release validation, not implementation defects:

* Exercise a signed or loose installed package through Explorer **Open with**
  using spaces, non-ASCII, UNC, and multi-selection-first-only inputs.
* Exercise an actual Explorer/OLE file drag; automation begins at Qt's platform
  event boundary and then crosses the real production `QWindow` path.
* Play representative content for all seven advertised demuxer families,
  including a real 192-byte MTS/M2TS transport stream. Demuxer availability is
  not a universal codec/profile guarantee.
