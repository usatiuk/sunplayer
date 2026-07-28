# 0001: Application-owned QRhi with redirected Qt Quick

* Status: Accepted
* Date: 2026-07-28

## Context

Sunroom needs to combine display-targeted HDR video, subtitles, Qt Quick UI,
and diagnostics before the final SDR or HDR presentation transform. The
application must know which graphics device and swapchain are active, control
the final output encoding, and eventually share textures with libplacebo and
hardware decoders.

The original Windows prototype used a monolithic native D3D11 renderer beside
Qt Quick. That made the presentation experiment possible but leaked
platform-specific rendering throughout the window and made shared device,
resource, and UI composition ownership unclear.

Allowing an ordinary onscreen `QQuickWindow` to own final presentation would
simplify basic UI rendering, but it would hide the final HDR/SDR composition
point and make UI luminance policy and display-targeted video composition less
explicit.

## Decision

For one presentation domain:

* The application owns the QRhi device, visible swapchain, and final render
  pass.
* Qt Quick adopts that QRhi through `QQuickGraphicsDevice::fromRhi()`.
* `QQuickRenderControl` renders the UI into an application-provided transparent
  RGBA16F texture.
* The application compositor samples the Qt Quick texture together with video,
  subtitles, and diagnostics and writes the swapchain.
* Resource creation, mutation, and destruction occur at engine-controlled
  render points on the owning thread.
* A presentation domain uses one graphics device so Qt Quick, the compositor,
  libplacebo, and compatible decoded surfaces can share native resources.
* QRhi and Qt private integration remain confined to the graphics subsystem.

The current implementation realizes this decision on Windows with D3D11 and
one window. Those are current capability limits, not cross-platform
architecture requirements.

## Consequences

Benefits:

* Sunroom controls the final color, luminance, geometry, and presentation
  operation.
* Qt Quick and custom GPU rendering share one device without an extra
  cross-device copy.
* Video, subtitle, and diagnostic layers gain a clear composition boundary.
* Swapchain and device loss are handled by one owner.
* The model maps to other QRhi backends without making the entire player
  backend-specific.

Costs:

* The application must drive Qt Quick polishing, synchronization, rendering,
  animation, input forwarding, and invalidation.
* Redirected Qt Quick requires a full-window intermediate texture and an
  additional GPU pass.
* QRhi is a private Qt API with a narrower compatibility guarantee. The build
  must pin and validate the Qt version, and integration must stay isolated.
* Destruction ordering and device-generation tracking become explicit
  correctness requirements.
* Flattening the whole UI before linear-light conversion limits the
  colorimetric accuracy of overlapping translucent content.

## Alternatives considered

### Qt-owned onscreen scene graph

Rejected for the current architecture because it does not provide the required
explicit final composition and swapchain-encoding boundary.

### Raw D3D11 presentation

Replaced because it tied presentation and UI integration to Windows and did
not provide the intended shared cross-platform graphics abstraction.

### A separate Vulkan, OpenGL, or wgpu renderer

Not selected as the presentation owner. A second abstraction would introduce
device and texture interop before profiling or a platform requirement
justifies it. libplacebo may still use backend-native facilities behind the
shared graphics boundary.

## Not decided here

This decision does not fix:

* The canonical display-targeted video color space.
* The exact libplacebo native-device integration.
* One graphics device per process versus per window.
* Multi-window or multi-display mirroring behavior.
* Platform backend selection outside the current Windows D3D11 path.
