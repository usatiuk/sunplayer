# Managed Wayland HDR delivery

Status: Implemented; native hardware validation remains open

## Goal

Complete the managed-output portion of the native Wayland port with one
coherent presentation contract:

* bind color-management-v1 version 2 and publish only completed, validated
  preferred descriptions;
* own one mutable color declaration on the stable Qt `wl_surface`;
* present an HDR-capable window as BT.2020/PQ for its lifetime, independent of
  HDR/SDR monitor movement;
* couple that declaration to an opaque RGB10A2 QRhi HDR10 swapchain and one
  final linear-sRGB to BT.2020/PQ encoder;
* retain a complete managed gamma-2.2 SDR fallback after a genuine HDR
  presentation failure; and
* rerender the retained current frame when preferred target values change,
  including while playback is paused.

Linux keeps the macOS-style working contract: numeric `1.0` is active platform
reference white. Sunroom does not apply Windows scRGB `/80` or
`referenceWhiteNits / 80` scaling on Linux. The final PQ pass serializes that
working coordinate; the compositor maps it to each output.

## Grounded findings

The implementation is grounded in the Ubuntu Qt 6.10.2, Mesa 26.0.3, Mutter
50.1, KWin 6.6.5, Wayland protocols 1.47, and libplacebo 7.360.0 sources.

* Qt's named `SRgbLinear` declaration omits extended target volume. Its values
  above `1.0` are encoding-valid but not a complete portable HDR declaration,
  and Mutter/Mesa do not expose QRhi's exact extended-linear pair.
* BT.2020/PQ does not require `extended_target_volume`. Mesa exposes the exact
  Vulkan HDR10 pair when the compositor advertises BT.2020 and PQ.
* Qt's Wayland Vulkan backend changes the selected semantic color space to
  `PASS_THROUGH_EXT` when the same pixel format supports it. The application
  can then own the one Wayland color surface without Vulkan WSI creating a
  competitor.
* QRhi can otherwise fall back to the first native format while retaining the
  requested logical format enum. Require both RGB10A2/HDR10 and
  RGB10A2/pass-through in the raw Vulkan format list.
* Stock Qt binds protocol version 1 and exposes neither v2 readiness nor
  description failure. Sunroom must own the narrow v2 protocol boundary.
* A Wayland image description labels buffer content, not a monitor mode.
  Preferred feedback is advisory. Mutter and KWin keep the client description
  separate from their preferred per-output state.
* PQ's 203-nit reference is a source coordinate that the compositor anchors to
  the output reference. It is not physical Linux SDR-white telemetry.
* Linux QRhi `hdrInfo()` contains generic defaults rather than useful output
  observations and must not activate Windows' scene-referred scaling.

The full evidence and production precedents are in
[the research note](../../research/2026-08-03-wayland-hdr10-presentation.md).

## Design

### Protocol ownership and feedback

Keep the version-2 color-manager binding for the window-context lifetime. At
startup, inventory capabilities and create both named descriptions before the
window presents:

* sRGB primaries plus gamma 2.2;
* BT.2020 primaries plus ST 2084 PQ when supported.

Wait for `ready2`. Rejection of the managed-sRGB description selects unmanaged
SDR; rejection of only the PQ description retains managed SDR. Leave Qt's
requested Wayland color space empty so Qt does not create a competing color
surface.

The display provider follows the current `wl_surface` using Qt's private native
surface signals and platform-surface events. For each surface it creates one
Sunroom-owned color-management surface and one feedback object. It reapplies
the currently declared description after exceptional native-surface
recreation. The window context transfers this one provider exactly once and
asserts that it is released after the engine and before the native window, so
the mandatory declaration follower cannot be detached while presentation is
live. Normal output movement does not recreate the native surface.

On attach, explicit refresh, and each new preferred identity, request the
latest parametric description. Replace obsolete pending requests. Publish only
complete, boundary-validated semantic values, suppress equivalent publications,
and keep v2 identities adapter-local.

Preferred reference and target values feed existing target rendering and
diagnostics. They do not select the presentation encoding. Mutter currently
publishes the PQ envelope maximum while another compositor may publish a more
output-specific maximum; trust the declared value without compositor-specific
reinterpretation.

### Stable HDR surface and complete fallback

The explicit modes remain:

* `AdaptiveExtendedLinear` for Windows/macOS;
* `UnmanagedSrgb` for the Wayland no-protocol fallback;
* `ManagedGamma22Sdr`; and
* `ManagedHdr10Pq`.

When all managed-HDR declaration capabilities exist, the initial mode is
`ManagedHdr10Pq` even if preferred feedback currently describes an SDR output.
The compositor maps the same PQ content to HDR, SDR, or multiple outputs.
`screenChanged` and `preferred_changed2` never switch modes.

The controller changes only pending surface description state on the stable
`wl_surface`; it never calls `QWindow::destroy()`/`create()` for a mode change.
The engine first prepares the matching swapchain and encoded frame, then sends
`set_image_description` immediately before the Vulkan present. With no event-
loop turn between those operations, WSI's matching buffer commit applies the
description and pixels as one surface transaction.

HDR requires QRhi `HDR10` plus raw Vulkan RGB10A2 semantic-HDR10 and pass-
through pairs. Format absence, render-pass failure, initial creation failure,
or later resize/out-of-date recreation failure without device loss records one
rejection for the graphics-device generation and queues the complete managed-
SDR tuple. A new device generation may attempt the complete HDR tuple once
again; monitor movement and preferred values do not clear rejection.

Present incompatibility after an exceptional native-surface replacement
rebuilds the graphics domain while retaining media identity and logical
position; graphics invalidation clears the old decoded frame and re-decodes it
against the replacement device. The bounded recovery is complete only after a
present-compatible swapchain is established, so repeated incompatible domains
cannot reset the attempt count. Device loss remains device recovery, not an
HDR-format rejection. Ordinary SDR swapchain creation failures use a separate
bounded presentation retry rather than waiting for unrelated window activity.

### Rendering behavior

Libplacebo keeps producing display-targeted FP16 linear-sRGB video so video,
subtitles, and Qt Quick UI can be composed in linear light. The final opaque
draw performs exactly one encoding step:

1. compose all layers in linear sRGB;
2. convert linear BT.709/sRGB primaries to linear BT.2020;
3. clamp negative light after the matrix;
4. apply inverse ST 2084, mapping working `1.0` to the 203-nit source
   reference; and
5. write opaque RGB10A2.

This step performs no output tone mapping. The compositor owns the per-output
tone/gamut mapping and calibration.

The PQ coordinate represents at most `10000 / 203` working headroom. Cap the
effective libplacebo target at that encoding boundary without changing the raw
preferred-description diagnostic value. Lower compositor-declared targets
remain lower; the cap is not a Mutter-specific policy.

`PresentationOutputState::stateChanged` schedules a frame. Target fields are
part of `RenderedVideoSurfaceDescription`, so a preferred semantic change
rerenders the retained producer surface without requiring a newly decoded
frame or a pause-specific path.

## Implementation sequence

1. Introduce explicit presentation modes and one platform controller seam.
2. Implement the long-lived version-2 manager and surface-following preferred-
   description provider.
3. Own ready managed sRGB/PQ descriptions and one color-management surface,
   leaving Qt's color request unset.
4. Select stable HDR10 independently of preferred output state and validate
   both semantic HDR10 and pass-through Vulkan formats.
5. Implement the final BT.709-to-BT.2020/PQ encoder and the PQ coordinate cap.
6. Implement render-safe complete SDR rollback and one retry per new graphics
   generation, including later resize failures.
7. Synchronize ADRs, subsystem docs, roadmap, checklist, testing matrix,
   research, and deferred physical gates.
8. Build, lint, test, obtain independent review, reassess, and record actual
   delivery evidence.

## Validation and acceptance

Automated coverage includes capability/version selection, independent missing
BT.2020 and missing-PQ cases, complete/incomplete preferred descriptions,
reference-white-relative target mapping including a 1x SDR preferred target,
stable HDR selection independent of preferred output state, rejection per
graphics generation, output-encoding coupling, lower-target preservation, the
PQ range cap, and existing retained-target invalidation.

The production shader compiles on Linux. Its analytic BT.2020/PQ pixel oracle
checks neutral `1.0` at PQ's 203-nit code, signed linear-sRGB matrix-before-
clamp behavior, and ordinary color in the real QRhi compositor test registered
only on Windows. That proves the shared shader on that backend when run but is
not Linux native-swapchain evidence. The queued protocol/swapchain transaction
is validated by code/lifetime review plus native manual testing rather than a
fake Wayland window harness.

Build and run the Linux suite with system dependencies. A headless compositor
without managed color proves only unmanaged fallback.

Real-hardware evidence remains required for v2 description readiness, raw
HDR10/pass-through acceptance, actual HDR10 diagnostics, paused target rerender,
windowed/fullscreen movement across HDR and SDR outputs without native-window
replacement, hotplug, HDR enable/disable, complete managed-SDR rollback, and
visible or measured output behavior. Implementation completion alone does not
broaden the public Linux HDR support claim.

## Delivery evidence

* `cmake --build cmake-build-debug --parallel 14` passes with the production
  shader and version-2 protocol adapter.
* `cmake --build cmake-build-debug --target all_qmllint --parallel 14` passes.
* All 26 registered Linux CTests pass together with `--parallel 14`, including
  the Wayland capability/mode and shared presentation-target regressions.
* Independent Wayland/lifecycle, simplicity/state-machine, and color-math/test
  reviews were repeated after the stable-surface rewrite. Their concrete
  readiness, commit-ordering, 1x-target, bounded-recovery, diagnostics, and
  test findings were fixed; the narrow confirmation pass found no remaining
  defects in protocol timing, target/math, or shader coverage. Its final
  recovery-counter finding moved completion to successful swapchain creation.
* The Windows-only real-QRhi PQ oracle was extended but cannot run on this
  Linux host. Native Mutter/KWin RGB10A2/WSI behavior and physical output
  remain the manual gates listed above.
