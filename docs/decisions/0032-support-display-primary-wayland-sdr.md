# 0032: Support display-primary SDR independently of Wayland HDR

Status: Accepted

## Context

The supported native compositor exposes preferred display primaries and
parametric SDR descriptions without the mastering feature required by our
HDR10 surface. Coupling wide gamut to that HDR gate unnecessarily clips SDR
output to sRGB. Existing extended linear BT.709 FP16 composition can already
represent wide-gamut colors.

## Decision

Add managed display-primary gamma-2.2 SDR to the existing surface contract.
Keep one compositor and Qt-owned window/swapchain. After linear composition,
use libplacebo's relative-colorimetric matrix (CAT16 white adaptation), clamp
in the output basis, and gamma encode. Declare the same explicit primaries
with zero-black, 80-nit reference/max SDR content coordinates. Physical display
black, white and calibration remain compositor responsibilities.

Use preferred target gamut with headroom one, independently of HDR telemetry.
Prepare immutable descriptions asynchronously through the existing ready/
pending ownership model; attach the matching ready description immediately
before buffer presentation. Reconcile semantic target changes at the normal
render boundary, including retained video. Keep canonical managed-sRGB startup
and fallback. Bound enhanced-mode rejection by graphics generation.

Prefer HDR10 when its existing complete contract is available; otherwise use
this SDR mode when explicit primaries/luminances are supported. Extend ADR 0021
without changing its stable PQ transport or the physical-gamut mapping of
ADR 0027. Avoid a second ICC transform, custom presentation backend, speculative
profile service or new target epochs.

## Consequences

WCG works on SDR displays and on compositors without HDR mastering support.
The current Qt SDR selection is native XRGB8888 on the baseline host: correct
encoding with 8-bit precision. Dedicated 10-bit SDR presentation and physical
colorimeter/multi-monitor validation remain deferred. See the
[research](../research/2026-10-17-wayland-wcg-sdr.md) and
[delivery plan](../plans/graphics/2026-10-17-wayland-wcg-sdr.md).
