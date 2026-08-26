# Windows overlapped presentation window

Status: Complete

## Goal

Make SunPlayer's framed Windows presentation HWND an ordinary unowned,
resizable top-level window so Windows and third-party desktop managers can
recognize and move a maximized window between displays. Completion requires the
native window to omit `WS_POPUP` in normal and maximized states, retain its
ordinary resize/maximize capabilities, preserve existing presentation and
fullscreen behavior, and pass the user's DisplayFusion drag test.

## Grounded current behavior

* SunPlayer creates one top-level `PresentationWindow` as a `QWindow` with the
  default `Qt::Window` flags.
* On Qt 6.11.1 for Windows, the resulting unowned window has style
  `0x96cf0000`: ordinary caption, system-menu, resize, minimize, and maximize
  bits plus `WS_POPUP`.
* Qt's Windows platform implementation adds `WS_POPUP` while translating a
  framed top-level `Qt::Window`; Qt exposes no public flag that requests the
  same framed window without that native bit.
* DisplayFusion does not preserve maximization while dragging this HWND between
  displays. The user's prototype test passed after only `WS_POPUP` was removed,
  producing style `0x16cf0000`.
* The native window exists by the end of `PresentationWindow::initialize()`:
  display-state attachment may create it, and `winId()` establishes it when
  needed. The correction can therefore happen once before the window is shown.

## Chosen approach

* In the Windows-only portion of `PresentationWindow.cpp`, obtain the HWND
  after output-state attachment and remove only `WS_POPUP` with
  `SetWindowLongPtrW`.
* Notify Windows of the native style change with
  `SetWindowPos(..., SWP_FRAMECHANGED)` while preserving position, size,
  activation, and Z order.
* Treat a Windows API failure as a bounded platform warning and continue with
  Qt's still-usable native style. The registered real-window test remains a
  hard regression gate on supported development and release configurations.
* Extend the existing Windows initial-window application probe to assert that
  the real presentation HWND is unowned, has neither `WS_POPUP` nor `WS_CHILD`,
  and retains `WS_THICKFRAME` and `WS_MAXIMIZEBOX`. Keep the existing native
  black-background assertion in the same bounded process test.
* Assert the same framed-window contract after both fullscreen restoration
  paths. Qt may intentionally use `WS_POPUP` while the window is fullscreen;
  the required contract resumes when Qt restores normal or maximized state.

The correction remains private to the application-window boundary. It does not
change Qt flags, reimplement movement, monitor selection, maximize geometry, or
fullscreen transitions.

## Invariants and non-goals

* Non-Windows builds and window behavior remain unchanged.
* Qt remains the owner of the native HWND and presentation surface.
* During the one-time framed-window correction, all pre-existing native style
  bits except `WS_POPUP` remain unchanged.
* SunPlayer does not add DisplayFusion-specific detection or behavior.
* No current Windows code changes Qt window flags after correction. A future
  flag change must happen before native creation/correction or explicitly
  reapply and verify the native contract because Qt reconstructs HWND styles.
* Windows client-side decorations are not part of this change. A future CSD
  implementation must preserve the ordinary managed-window contract, prefer an
  expanded client area with native frame semantics, use system move/resize
  operations, and rerun native Snap, maximize/restore, multi-display,
  fullscreen, DPI, shadow, and third-party-manager validation. The current
  Linux `FramelessWindowHint` path removes the Windows resize-frame contract and
  cannot simply be enabled on Windows.
* macOS CSD is separate platform work and must use native macOS evidence.

## Implementation slices

1. Add the narrow Windows native-style correction at presentation-window
   initialization.
2. Extend the existing real-window Windows verification with style and owner
   assertions.
3. Synchronize the application architecture and testing documentation.
4. Build the Windows Debug target and run the focused real-window checks.
5. Run the independent correctness, risk/simplicity, and evidence/scope review
   loop; resolve findings and rerun affected checks.

## Validation

* Build `sunplayer` and the registered tests with the pinned Qt 6.11.1 Windows
  configuration.
* Run `application-initial-background` to prove the final real HWND contract
  and existing pre-presentation black fill.
* Run `application-fullscreen` to cover normal/maximized/fullscreen transitions
  and restoration, including the native contract after both restorations.
* Preserve the user's successful DisplayFusion maximized cross-monitor drag as
  the physical acceptance result; repeat it if review changes native behavior.
* Run `git diff --check`, inspect the final diff/status, and confirm no unrelated
  files are included.

## Documentation impact

Update the application subsystem's window-ownership/event description, the
Windows real-window verification description in `docs/TESTING.md`, and the
testing subsystem's built-application coverage status. This narrow platform
compatibility correction does not require an architecture decision or a
root-roadmap scope change.

## Commit boundary

Ship implementation, regression coverage, synchronized documentation, and this
completed plan as one commit.

## Remaining gaps

The native process probe can establish HWND classification and core window-state
regressions, but DisplayFusion integration itself remains a physical desktop
check because it has no project-owned automation boundary.

## Outcome

SunPlayer now removes only Qt's extra `WS_POPUP` bit from the framed Windows
presentation HWND before show, leaving Qt as the window/surface owner and
preserving every other style bit. Failure remains a bounded logged degradation.
The real initial-window probe protects the ordinary top-level contract, and the
fullscreen smoke protects it after both normal and maximized restoration.

Future Windows CSD work must establish its Qt flags before this correction or
explicitly reapply the contract; the Linux frameless path is not directly
portable to Windows. Expanded-client-area/native-frame behavior is the intended
starting point for that separate investigation.

## Validation performed

* The pinned Windows Debug `sunplayer` target compiled and linked successfully
  after implementation and again after review cleanup.
* `application-initial-background` passed with the augmented real-HWND contract
  plus the existing pre-presentation black-fill assertion.
* `application-fullscreen` passed with the augmented contract checks after
  normal and maximized fullscreen restoration.
* The clean baseline measured `0x96cf0000`; the one-bit prototype measured
  `0x16cf0000`. The user confirmed that behaviorally identical prototype fixed
  DisplayFusion's maximized cross-monitor drag. The reset and final
  reimplementation were not separately subjected to that physical drag.
* Three independent plan reviewers covered behavior/correctness,
  architecture/failure risk with explicit simplicity/anti-overengineering, and
  tests/evidence/docs/scope. Their findings narrowed fullscreen semantics,
  changed fatal failure to logged degradation, added restoration assertions,
  completed the HWND contract, and clarified future CSD ordering.
* The same three independent lenses reviewed the implementation. They found no
  correctness or architecture defect; all cleanup and documentation findings
  were resolved before the final rebuild and focused test rerun.
* `git diff --check` passed before and after implementation review cleanup.

## Resulting commit

`Use an ordinary Windows presentation window` (the commit containing this
plan).
