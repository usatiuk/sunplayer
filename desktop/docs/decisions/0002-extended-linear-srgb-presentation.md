# 0002: Prefer extended-linear sRGB presentation with explicit SDR-white mapping

* Status: Accepted
* Date: 2026-07-28
* Amended by:
  [0013: Rely on system display calibration on managed presentation paths](0013-rely-on-system-display-calibration.md)

## Context

A desktop player must present SDR UI and SDR or HDR video through operating
systems whose HDR mode, SDR-white level, display headroom, and window output
can change while the application is running.

Operating-system display telemetry and the graphics path do not change
atomically. HDR telemetry may already describe a new state while the active
swapchain still uses the previous format. Correct final encoding therefore
cannot be selected from asynchronous display metadata alone.

HDR10 swapchain presentation is possible on some platforms, but it moves the
desktop compositor boundary into a nonlinear, display-referred signal and
requires explicit metadata and encoding. An extended-linear desktop format is
better suited to composing HDR video with SDR UI before the operating system
maps the result to the physical display.

## Decision

* Prefer QRhi's extended-linear sRGB swapchain format when the active backend
  and output support it.
* Fall back to an SDR sRGB swapchain when extended-linear presentation cannot
  be created.
* Treat the successfully created swapchain format as authoritative for final
  shader encoding.
* Treat operating-system HDR and luminance information as target-selection,
  diagnostic, and invalidation input rather than proof of the active graphics
  path.
* Express SDR UI and current diagnostic content relative to SDR/reference
  white.
* For scene-referred scRGB, use the standard numerical convention in which
  `1.0` represents 80 nits and scale SDR white explicitly.
* Combine QRhi swapchain HDR information with a narrow platform display
  adapter. On Windows the adapter uses Advanced Color information from WinRT.
* Recreate presentation after relevant display-mode or output changes and
  rerender any display-targeted content.

The current priority order is:

1. Use valid Windows HDR luminance values while Windows reports active HDR.
2. Otherwise use valid QRhi swapchain luminance information.
3. Otherwise use QRhi current/potential component headroom.
4. Fall back conservatively to SDR white and no extra headroom.

## Consequences

Benefits:

* The final compositor can combine video and SDR UI in linear light.
* On a correctly tagged managed path, the operating system remains responsible
  for mapping the desktop extended-linear signal through active display
  calibration to the physical display.
* SDR fallback has an explicit clamp and sRGB encoding path.
* Asynchronous telemetry cannot make the shader encode for a swapchain format
  that has not actually been created.
* Window movement and dynamic HDR changes have defined invalidation behavior.

Costs and limitations:

* SDR-white and peak information can be missing, delayed, or inconsistent
  between the operating system and QRhi.
* Display confidence and provenance are not yet represented in the public
  state.
* A display-targeted video texture may need rerendering after output changes,
  including while paused.
* Windows is the only implemented platform adapter.
* Ordinary Windows DirectX SDR presentation with Advanced Color inactive is an
  unmanaged sRGB-assumed fallback, not a color-calibrated path.
* The current diagnostic shader's tone mapping is temporary; libplacebo must
  own real video color processing.

## Alternatives considered

### Select encoding directly from operating-system HDR state

Rejected because telemetry can lead or lag swapchain recreation. The active
swapchain is the only reliable source for the shader's immediate output
contract.

### Prefer an HDR10 swapchain

Not selected as the desktop default. It complicates linear UI composition and
makes the application responsible for a nonlinear display signal and metadata.
It may be revisited for a platform where extended-linear presentation is
unavailable or demonstrably unsuitable.

### Treat `1.0` as an unspecified brightness

Rejected because video, UI, subtitles, tone mapping, diagnostics, and
cross-display rerendering require an explicit reference-white convention.

## Not decided here

This decision does not settle:

* How arbitrary decoded source color spaces map into the rendered-video
  boundary. The consumer-side surface contract is now fixed by
  [0003](0003-display-targeted-video-surface.md); libplacebo integration will
  define the source-side mapping.
* How macOS EDR and Linux compositor protocols supply equivalent state.
* User overrides or confidence policy for unreliable display metadata.

Display-calibration ownership is decided by
[0013](0013-rely-on-system-display-calibration.md): Sunroom relies on the OS
or compositor on managed paths, and application-managed display ICC remains
deferred.
