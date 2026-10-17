# 0033: Keep Wayland HDR mastering metadata optional

* Status: Accepted
* Date: 2026-10-17
* Amends ADR 0027's mastering requirement; preserves ADR 0021's stable PQ
  transport and ADR 0032's WCG SDR fallback.

## Context

Commit `e7abecb` made the optional mastering-display feature mandatory for
HDR. Native GNOME advertises PQ/BT.2020 but not that feature, so HDR regressed
to SDR despite an HDR-enabled output and working HDR10 Vulkan formats.
The subsequent WCG work preserved that restriction.

The protocol defines omitted mastering primaries/luminances to inherit the
primary color volume. The mastering feature controls those optional requests;
it does not determine whether a compositor supports PQ content. PQ's default
primary minimum is 0.005 nits, which also excludes our true-zero backgrounds.

## Decision

Require the existing managed base contract, named BT.2020, PQ and explicit
luminances for managed HDR10. Declare primary luminances `(0,10000,203)` to
match zero-black PQ pixels and the fixed 203-nit content reference coordinate.
The supported native baseline provides this luminance feature.

When mastering metadata is supported, keep the existing conservative composed
volume calculation and requests. Otherwise omit both mastering requests and
use the full BT.2020/0–10000-nit primary volume as the target. Absence is an
optional value in the existing description key, so changing display targets
does not recreate an identical full-volume declaration. Rendering still
reconciles target gamut, black and headroom normally.

Keep ready/pending ownership, matching declaration-before-buffer presentation,
Qt swapchain ownership and generation-bounded SDR fallback. Do not add a
compositor-specific branch, custom swapchain or simulated metadata support.

## Consequences

HDR works without optional mastering support. Missing mandatory encoding or
presentation facilities still select honest SDR. The full-volume declaration
can give the compositor less precise tone/gamut-mapping information than tight
metadata; that is not grounds to disable otherwise valid HDR. Compositor
mapping and physical luminance accuracy are not proven by protocol acceptance.

Native validation accepts PQ with XR30 buffers, no unsupported requests and
user-observed HDR. Both mastering capability variants have regression tests.
See the [delivery plan](../plans/graphics/2026-10-17-wayland-hdr-optional-mastering.md).
