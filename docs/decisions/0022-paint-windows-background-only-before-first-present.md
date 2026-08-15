# 0022: Paint the Windows background only before first presentation

* Status: Accepted
* Date: 2026-08-09
* Related:
  [0001: Application-owned QRhi with redirected Qt Quick](0001-application-owned-qrhi-composition.md),
  [0002: Prefer extended-linear sRGB presentation](0002-extended-linear-srgb-presentation.md)

## Context

On Windows, showing the Qt-owned presentation window exposes its native client
area before SunPlayer can create and present the first QRhi swapchain frame.
Qt acknowledges the initial background-erase message without painting. The
temporary DWM surface can therefore appear white even though the application
palette, Player background, and final compositor are dark.

SunPlayer uses a D3D11 flip-model HWND for SDR and HDR presentation. GDI must not
remain a second producer after the first successful DXGI presentation. The
research and pinned-source evidence are recorded in
[the Windows first-presentation note](../research/2026-08-09-windows-first-presentation-background.md).

## Decision

`PresentationWindow` handles `WM_ERASEBKGND` on Windows while the current
native surface has not completed a successful QRhi presentation. It fills the
entire client rectangle with stock black and leaves `WM_PAINT` to Qt so the
normal exposure and render sequence continues.

The presentation engine owns the authoritative state. A successful
`QRhi::endFrame()` transfers client-area ownership to API presentation. Native
surface creation begins a new pre-presentation interval; swapchain resize,
output changes, and graphics-device recovery on the same surface do not.

After that transition, SunPlayer performs no GDI client painting on the HWND.
The D3D11 swapchain remains the sole producer.

## Consequences

* Startup remains immediate, but its temporary client content matches the
  compositor's black clear instead of flashing white.
* The behavior is Windows-only and does not add macOS, Wayland, QML, media, or
  HDR policy branches.
* Failure to obtain or fill the transient erase DC falls through to Qt without
  blocking exposure or graphics recovery.
* A native-message application probe protects the pre-present color; existing
  real-window scenarios protect continued QRhi presentation.

## Alternatives considered

### Hide, cloak, or fade in the window

Rejected. These choices introduce another native visibility lifecycle, delay
the perceived launch, or make Qt use a layered accelerated HWND.

### Change QML or the application palette

Rejected. Neither owns the client area before the first swapchain frame.

### Continue GDI painting after first present

Rejected. It conflicts with flip-model ownership and HDR presentation.
