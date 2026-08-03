# 0013: Rely on system display calibration on managed presentation paths

* Status: Accepted
* Date: 2026-08-01
* Related:
  [0002: Prefer extended-linear sRGB presentation with explicit SDR-white mapping](0002-extended-linear-srgb-presentation.md)

## Context

Content color processing and physical-display calibration are separate axes.
Libplacebo must interpret source color, adapt HDR luminance, and gamut-map to a
reproducible target. The final display ICC/profile transform calibrates the
whole composed surface—including video, UI, and subtitles—to the actual
monitor.

Modern desktop color pipelines normally offer a managed presentation path:
Windows Advanced Color, macOS ColorSync/EDR, and Wayland compositors supporting
color-management-v1. Applying the display profile in Sunroom as well as in the
OS would transform colors twice. Applying it in libplacebo before QRhi
composition would calibrate only video.

## Decision

The accepted presentation policy distinguishes two color-management modes:

* `SystemManaged`: Sunroom declares the final surface encoding accurately and
  relies on the OS/compositor to convert it through the active display profile
  and calibration exactly once.
* `UnmanagedSrgb`: no trustworthy managed path is available, so Sunroom emits
  SDR sRGB/Rec.709, assumes corresponding display behavior, restricts
  headroom to one unless independently proven, and reports low confidence.

Application-managed display ICC is deferred. If added later, it becomes a
third explicit mode and applies a destination transform to the entire final
composition, not only the video surface.

The current implementation makes the Windows choice implicitly from the
active swapchain and Advanced Color state. A later presentation-state slice
will expose the mode and revisions explicitly; this decision does not claim
that typed mode already exists in code.

The current Windows FP16/scRGB path is `SystemManaged` while Advanced Color is
active and the surface is tagged correctly, but its coordinate conversion
depends on the active output mode. On HDR Advanced Color displays, scRGB is
scene-referred with `1.0 = 80 nits`, so the final whole-composition scale is
`referenceWhiteNits / 80`. On SDR Advanced Color/WCG displays, FP16 is
display-referred and working white maps to scRGB `1.0`; that path must not
reuse the HDR scale. Ordinary DirectX SDR output with Advanced Color inactive
is `UnmanagedSrgb`; Windows assumes sRGB but does not automatically apply a
display-profile transform to that swapchain path.

The macOS EDR and Wayland color-management-v1 paths are `SystemManaged`.
macOS declares extended-linear EDR. Managed Wayland HDR declares BT.2020/PQ
under [ADR 0021](0021-use-hdr10-pq-for-managed-wayland-hdr.md), after the
complete composition has been encoded once, and does not request a second
platform media tone mapper after libplacebo.
Native Wayland without the usable managed-SDR capability is
`UnmanagedSrgb` under [ADR 0018](0018-support-unmanaged-srgb-wayland-sdr.md).
X11 and XWayland are not unmanaged fallbacks; they are unsupported by
[ADR 0015](0015-wayland-only-linux-desktop.md).

Display primaries and luminance capabilities may still guide libplacebo's
target gamut and HDR mapping. That perceptual content mapping is not the same
as applying the monitor's calibration profile.

Source ICC is independent of this decision. Embedded source profiles are
preserved and diagnosed but explicitly removed from the render-local
libplacebo frame on every platform, as recorded in ADR 0012.

## Consequences

* The ordinary modern path contains one display-calibration owner: the
  operating system or compositor.
* Sunroom does not need an explicit display-ICC implementation for the first
  correct HDR/color milestone.
* Presentation state must expose the mode, surface encoding, calibration or
  presentation-description revision, and confidence.
* An OS-managed calibration/profile change is presentation-environment
  diagnostic state and does not by itself invalidate a display-targeted video
  surface. Only changes to libplacebo inputs—such as target gamut, reference
  white, usable peak—or to the surface encoding advance the video-target
  revision. A platform presentation-description change may recreate or rebind
  presentation separately.
* Windows SDR without Advanced Color and native Wayland without usable color
  management are honest unmanaged fallbacks, not claims of calibrated output.
* A future application-managed ICC path must include Qt Quick and subtitles
  and therefore belongs after composition.

## Alternatives considered

### Always apply the monitor ICC in libplacebo

Rejected. It would calibrate video separately from the rest of the composed
surface and can double-transform managed output.

### Treat every correctly tagged desktop surface as automatically managed

Rejected. In particular, ordinary DirectX SDR output with Windows Advanced
Color inactive is not automatically display-profile managed.

### Implement application-managed display ICC now

Deferred. It is useful for legacy/unmanaged environments, but it is not needed
to establish the managed HDR path and would add a post-composition transform,
profile lifecycle, and validation matrix before the active source/display
contracts are complete.
