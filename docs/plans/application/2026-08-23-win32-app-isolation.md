# Strict Win32 App Isolation

Status: Implementation complete; external release gates remain

## Goal

Ship the existing Windows 11 24H2+ player inside appSilo without FullTrust and
without changing the media pipeline. Keep the two Windows compatibility calls
small, explicit, and outside cross-platform playback code.

Grounding and prototype evidence are recorded in
`docs/research/2026-08-23-win32-app-isolation.md`.

## Implementation

1. Change the shared Windows manifest to the strict `uap18` declaration proven
   by the prototype: `Isolated.App`, `appContainer`, and `appSilo`; no legacy
   entry point and no capabilities.
2. Add one Windows desktop-integration module containing only:
   * single-file selection through `IFileOpenDialog`;
   * external URI launch through `Windows.System.Launcher`;
   * passive AppContainer and appSilo token queries.
3. Route Windows Open media through that module. Keep the existing Qt
   `FileDialog` on macOS and Linux, and keep every selected URL flowing through
   `PresentationWindow::openMedia()`.
4. Route Windows source/bug-report links through that module. Preserve
   `QDesktopServices` on other platforms and the existing user-visible result
   handling.
5. Add the two nullable Windows token facts to copied diagnostics. Query
   failure means unavailable; it never affects startup or playback. If Windows
   reports AppContainer without appSilo, show a small non-modal warning on the
   Player screen because the comparative test proved file browsing is broken.
6. Update focused support and QML tests, plus the Windows packaging guide.

## Explicit non-goals

* No runtime fail-closed check, modal startup warning, or sandbox feature gate.
  The proven AppContainer-without-appSilo state gets only a small Player-screen
  warning and a technical log entry.
* No Windows App SDK dependency unless the proven direct picker regresses.
* No custom FFmpeg I/O, file copying, storage-token persistence, or filesystem
  probe.
* No second manifest, FullTrust fallback, helper executable, or new capability.
* No general platform-service framework beyond the three proven Windows calls.

## Validation checklist

1. Build the app and focused unit/UI tests on Windows.
2. Generate and launch the development package from a clean registration.
3. Confirm copied diagnostics report AppContainer and appSilo `yes`.
4. Open and play one local file and one consented UNC/NAS file.
5. Test Open with and drag/drop independently.
6. Test Source code and Report a bug browser activation.
7. Exercise audio, D3D11VA, seeking, SDR, HDR, and application restart.
8. Build the unsigned Store MSIX/MSIXUPLOAD and inspect the packed manifest for
   the strict declaration and absence of FullTrust/capabilities.
9. Run independent correctness, packaging, and simplicity review; resolve all
   material findings and repeat affected checks.

## Release decision

Keep the change only if the strict package remains a fully usable player. A
failure in a core flow is a reason to stop and reassess, not to grant broader
capabilities silently.

## Evidence

Completed on Windows 11 25H2:

* strict appSilo package launch with AppContainer and appSilo tokens both
  reported as `yes`;
* direct Open media, unchanged FFmpeg playback, D3D11VA, WASAPI, and external
  browser activation;
* comparative plain-AppContainer launch showing that drag/drop and URI
  activation work while the Win32 dialog cannot browse normal files;
* visual confirmation of the non-modal warning in that broken comparison
  state, with focused UI coverage proving it remains hidden when the condition
  is false;
* final Debug suite: 36 of 36 tests passed, including GPU and hardware-decode
  integration tests;
* final 48,466,841-byte RelWithDebInfo MSIX built successfully; its packed
  manifest contains only strict appSilo activation, empty capabilities, and no
  FullTrust declaration;
* independent correctness, packaging, and simplicity reviews found no
  remaining implementation defects.

Still required before a Store release:

* run the package smoke matrix on a serviced Windows 11 24H2 machine;
* install and smoke a signed MSIX, including Open with, UNC/NAS selection,
  Report a bug, restart, seeking, SDR, and HDR;
* upload the real Store-identity `.msixupload` to a Partner Center draft and
  complete certification.
