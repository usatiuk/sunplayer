# 0035: Share playback actions with native desktop media controls

* Status: Accepted
* Date: 2026-10-17

## Decision

One window-owned, build-selected native adapter calls the same MediaSession
actions as the UI: QtDBus MPRIS on Linux, HWND SMTC through C++/WinRT on Windows,
and public MediaPlayer centers on macOS. Native resources and callback lifetime
stay local to each adapter; the application releases them before dependencies.
No second playback engine, controller framework, or cross-platform scheduler.

MPRIS requires Stop retaining the item with subsequent Play from the beginning.
Replace the existing private intent boolean with Playing/Paused/Stopped and
share pause-like halt mechanics. Stop retains resources/frame/position; Play
uses the existing restart-at-zero path. No decoder lifecycle state, stopped
viewport policy, or Stop-specific graphics recovery protocol is necessary.

Capture the accepted-open generation into a retained item ID for MPRIS stale
SetPosition protection. Do not identify the item by the changing live decoder
generation, introduce a separate serial source, or stamp native events lacking
an item identity. Expose the existing clock sample and semantic position jumps;
native publication is eventually consistent and owns only its publication cache.

## Alternatives and limits

Teardown-based Stop adds lifecycle/UI policy without an API requirement. A
controller/command bus and shared seek preview are unnecessary for one owner;
the existing QML interaction stays in QML. Evaluated wrapper libraries add
protocol patches, substantial toolkit dependencies or Rust/FFI integration, so
reuse current Qt/OS SDKs. Sources and alternatives are in the
[research](../research/2026-10-17-native-media-controls.md).

Desktop arbitration and remote-shell timeline rendering are OS behavior. Native
Windows/macOS builds and host acceptance remain required evidence; Linux wire
tests do not imply that evidence. See the
[delivery plan](../plans/cross-cutting/2026-10-17-native-media-controls.md).
