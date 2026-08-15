# Qt Metal EDR declaration across macOS screen transitions

Date: 2026-08-02

## Finding

Qt 6.11.1 can replace an existing Metal presentation layer's color-space
declaration when a window changes screens. If an application-owned QRhi
swapchain skips `createOrResize()` because its format and pixel size are
unchanged, the layer remains EDR-enabled but no longer carries QRhi's selected
extended-linear sRGB declaration. Video highlights and Qt Quick colors are then
interpreted through the wrong presentation color space, and the error persists
after moving the window back until QRhi configures the layer again.

This matched the observed SunPlayer failure: AppKit headroom diagnostics updated,
content still exceeded SDR white, but highlights clipped and native-style UI
colors became unusually deep after a monitor move.

## Pinned-source evidence

The inspected Qt sources are the `v6.11.1` tags of `qtbase` and
`qtdeclarative`.

* `qtbase/src/plugins/platforms/cocoa/qnsview_drawing.mm` implements
  `viewDidChangeBackingProperties()` by calling `propagateBackingProperties()`.
  For a `CAMetalLayer`, that function assigns `metalLayer.colorspace` from the
  view/window color space.
* `qtbase/src/gui/rhi/qrhimetal.mm` assigns
  `kCGColorSpaceExtendedLinearSRGB` and enables
  `wantsExtendedDynamicRangeContent` for
  `QRhiSwapChain::HDRExtendedSrgbLinear`, but does so inside
  `QMetalSwapChain::createOrResize()`.
* The same QRhi function explicitly keeps the swapchain object alive when the
  window is unchanged, so re-running `createOrResize()` is the supported
  in-place surface-configuration path; full device or presentation-resource
  teardown is unnecessary.

SunPlayer previously called `createOrResize()` for a resize only when the surface
pixel size changed. Its semantic display reconciliation correctly retained an
unchanged extended-linear format, but therefore missed this native layer-state
change.

## Resolution and scope

On macOS only, an actual `QWindow::screenChanged` marks the Metal presentation
surface dirty. At the next engine-owned render boundary, SunPlayer first
reconciles the desired swapchain format. If the format is unchanged, it calls
the existing QRhi `createOrResize()` path on the same swapchain, which reapplies
the extended-linear sRGB/EDR declaration. If the format changed, the normal
swapchain replacement path remains authoritative.

The change does not add display identities to rendered-video reuse, rebuild the
graphics device, recreate the Qt Quick or video textures, or introduce a
parallel color transform. Windows and Wayland behavior is unchanged.

## Evidence and remaining boundary

The Apple-M2/macOS-26 Debug build succeeds, the final post-review registered
suite passes 26/26 in 24.55 seconds, and the user confirmed that moving the
window between displays and back no longer corrupts video, UI color, or EDR
mapping.

An automated test can cover shared target calculations, but not the complete
physical event with the current public test boundary: CTest cannot create a
second physical `NSScreen` or require AppKit to deliver its real backing-color
space transition. Injecting the `screenChanged` signal or directly modifying
the layer from test-only production code would only verify the injection.
Keep the unlike-display scenario as a recorded native-hardware check unless a
controlled multi-display runner provides the real event.
