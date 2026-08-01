# 0015: Target Wayland and leave X11 unsupported

* Status: Accepted; unmanaged Wayland fallback superseded by ADR 0017
* Date: 2026-08-01
* Related:
  [0017: Require color-management-v1 for the first Linux release](0017-require-wayland-color-management-v1.md)

## Context

Sunroom needs one coherent Linux presentation target for HDR, color
management, native graphics interop, window/output changes, and packaging.
Wayland's color-management protocol and surface-oriented presentation model
can express the intended modern pipeline. X11 would require a separate,
largely unmanaged set of presentation, display-profile, output-selection, and
compatibility paths whose behavior cannot satisfy the same product contract
without substantial platform-specific policy and testing.

Supporting X11 merely as an SDR compatibility mode would still expand startup,
Qt platform selection, packaging, diagnostics, input, display observation, and
test matrices. No current product requirement justifies that work.

## Decision

The supported Linux desktop target is native Wayland. X11 and XWayland
presentation are unsupported and are not fallback targets.

Sunroom will:

* Build the Linux graphics and presentation path around Vulkan, Wayland, and
  color-management-v1 where available.
* Use an honest SDR Wayland fallback when the active Wayland compositor lacks
  HDR or color-management support. This fallback is superseded for Linux V1 by
  ADR 0017, which requires color-management-v1 and keeps SDR presentation
  inside its managed gamma-2.2 path.
* Keep Linux media, hardware decode, and audio choices independent from this
  window-system decision; PipeWire, PulseAudio compatibility, VAAPI, and DRM
  PRIME remain valid on supported Wayland desktops.
* Avoid X11-specific presentation, ICC, output discovery, packaging claims,
  automated tests, and compatibility abstractions.
* Reconsider X11 only through a later decision backed by a concrete product
  requirement and an explicit maintenance and validation budget.

## Consequences

* Linux presentation can target one modern surface and color-management model.
* The original decision allowed unsupported Wayland HDR/color-management
  capability to degrade to SDR. ADR 0017 narrows Linux V1 to managed gamma-2.2
  SDR or managed extended-linear HDR and rejects missing color-management-v1;
  it still never switches to X11 or XWayland.
* Linux packages may require a native Wayland Qt platform integration and may
  fail clearly when launched in an X11-only session.
* Cross-platform shared contracts still cover Windows, macOS, and Wayland, but
  do not need to encode legacy X11 limitations.
* Application-managed display ICC remains deferred for other unmanaged paths;
  X11 is not a motivating consumer.

## Alternatives considered

### Support X11 as an unmanaged SDR fallback

Rejected. It creates a second Linux presentation and validation path for a
legacy environment without advancing the HDR player contract.

### Run transparently through XWayland

Rejected. XWayland inherits the unsupported X11 presentation contract and is
not a substitute for native Wayland surface color management.

### Keep X11 nominally supported without testing it

Rejected. An untested support claim would be misleading and would still
distort interfaces and packaging decisions.
