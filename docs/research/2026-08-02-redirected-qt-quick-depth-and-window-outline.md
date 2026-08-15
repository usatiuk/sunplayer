# Redirected Qt Quick depth and the application-chrome outline

Date: 2026-08-02

## Question

Why did a full-window QML outline disappear over SunPlayer's titlebar and empty
page, while the same outline survived only beside active video or HDR Lab
content, and what is the smallest correct fix?

This investigation starts from commit `379faf6`, after all experimental
perimeter treatments had been removed. It targets the shared redirected Qt
Quick path rather than Wayland decoration ownership: the same Quick render
target is used by the Windows D3D11 and Linux Vulkan presentation engines.

## Reproduced symptom

The observed broken output was:

```text
SunPlayer                         _  □  ×
  no outline around the titlebar
│                  video                  │  outline only beside active content
└────────────────────────────────────────┘
```

The intended outline is one cohesive inner perimeter around the complete
client area, including the top and both titlebar-side edges. It must remain
visible with no media loaded and must not depend on the video viewport.

The user identified the surviving content-side pixels as SunPlayer's own chrome
outline: its appearance changed with the chrome styling. No texture readback
was made that would justify attributing those pixels to another render pass.

## Exact Qt and SunPlayer boundary

The inspected Ubuntu source packages are Qt 6.10.2. SunPlayer constructs one
`QRhiTextureRenderTarget` containing only an RGBA16F color attachment and gives
that complete target to Qt through
`QQuickRenderTarget::fromRhiRenderTarget()`.

Qt's official 6.10.2 `rendercontrol_rhi` example instead creates a matching
`QRhiRenderBuffer::DepthStencil`, attaches it to the texture render target,
and then passes that target to `fromRhiRenderTarget()`.

This difference is material:

* `fromRhiRenderTarget()` borrows the supplied target as-is. Its resolver
  clears Qt's implicit buffers and adopts the caller's target; Qt does not add
  a depth/stencil attachment around it.
* Qt Quick enables depth-buffer use for ordinary 2D scenes by default.
* Opaque scene-graph batches are ordered front-to-back, with higher scene order
  first. Depth testing and writing preserve those higher items when lower
  opaque items are drawn afterward.
* A transparent-fill `Rectangle` with an opaque border is classified as
  opaque by Qt's rectangle material, so its stacking participates in that
  depth-assisted path.

SunPlayer was therefore supplying a render target incompatible with Qt Quick's
normal renderer configuration. A high-z outline could be drawn first and then
overwritten by the lower opaque titlebar or page because the attachment that
enforces the ordering was absent. Which outline fragments survived could then
depend on the lower opaque nodes present in the current page and media state.

## Geometry findings

Qt 6.10.2's `QQuickRectangle` implementation contradicts the earlier
boundary-clipping theories:

* The scene-graph node receives the exact item rectangle.
* For straight corners, the outer rectangle remains unchanged and the inner
  rectangle moves inward by the complete pen width.
* The public QML documentation explicitly says the border is rendered within
  the rectangle's boundaries.
* Redirected rendering uses the complete target pixel size for both the device
  rectangle and viewport. It does not create a content-only or titlebar-
  excluding viewport.

A full-root `Rectangle` is consequently the appropriate primitive. It needs
neither a half-pixel inset nor four independently managed edge bars.

## Physical-pixel width

With the default `border.pixelAligned: true`, Qt rounds the requested logical
width after effective-DPR scaling. `border.width: 1` therefore means one
logical pixel, which becomes two physical pixels at several common scales.

The exact one-physical-pixel expression is:

```qml
border.pixelAligned: false
border.width: 1.0 / renderDevicePixelRatio
```

Disabling pixel alignment is required for this expression. `QQuickPen`
validates a pixel-aligned logical width through `qRound(width)`, so a width
below `0.5` can otherwise become invalid at high DPR before scene-graph
geometry is created.

The attached QML `Screen.devicePixelRatio` is not authoritative for this
redirected scene. `QQuickScreenAttached` follows the offscreen
`QQuickWindow::screen()`, whereas `QQuickWindow::effectiveDevicePixelRatio()`
uses `QQuickRenderControl::renderWindow()` and therefore SunPlayer's native
presentation window. The final implementation should pass the DPR already
validated by `QuickUiLayer::ensureRenderTarget()` into the QML root. This
keeps one render-target authority and adds no decoration-specific native
observer.

## Clean-slate experiment

Before production implementation, one intentionally narrow prototype changed
only two things:

1. It attached a matching single-sample depth/stencil render buffer to the
   existing RGBA16F Qt Quick target and released it with that target.
2. It added one full-root inward QML rectangle border above the chrome scene.

The Debug build and focused QML/package checks passed. The user ran the real
native-Wayland Vulkan application under WSLg and confirmed that the previously
missing complete outline rendered correctly. The prototype was then fully
reverted, and the worktree was returned to clean commit `379faf6` before this
research and implementation pass.

This is production-boundary visual evidence for the combined fix. It is not a
pixel-readback regression and does not prove fractional-DPR behavior or a
native compositor matrix.

## Selected implementation

* Make a same-size, same-sample-count depth/stencil render buffer a mandatory
  owned part of `QuickUiLayer`'s custom render target.
* Recreate and release the color texture, depth/stencil buffer, render target,
  and compatible render-pass descriptor as one lifetime group.
* Pass `QuickUiLayer`'s validated render DPR into the QML root and from there
  into `ClientSideWindowChrome`.
* Render exactly one full-root, non-antialiased `Rectangle` with an opaque
  inward border, one physical pixel wide.
* Keep its visibility equal to the existing application-chrome availability:
  enabled outside fullscreen, independent of page and media state.

The depth/stencil attachment corrects the redirected Qt Quick contract for the
whole application. The outline remains a small visual child of the existing
chrome module; it does not become a graphics-backend or compositor feature.

## Rejected alternatives

* **Four edge rectangles or separate titlebar edges:** duplicate geometry and
  still rely on the same scene-graph ordering contract.
* **Inset or half-pixel outline:** Qt's rectangle border is already inward;
  this would add a gap rather than repair the render target.
* **Qt Quick `Shape`:** adds stroke tessellation and antialiasing without a
  behavior the rectangle cannot express.
* **Final-compositor shader border:** leaks window-chrome policy into graphics,
  duplicates fullscreen state, and conceals the incomplete Quick target.
* **Disable Qt Quick's 2D depth path:** is a supported special configuration
  but reduces batching efficiency and diverges from Qt's official redirected-
  QRhi example when SunPlayer can provide the required attachment directly.
* **Texture readback in production:** useful as a temporary discriminator, but
  not needed after the exact attachment change passed the real visual path.
  A permanent capture test would require a larger test seam than this fix.

## Remaining evidence gaps

* Automated QML coverage can protect outline ownership, geometry, visibility,
  and DPR-derived width, but it cannot catch omission of the native depth
  attachment.
* A permanent production-texture readback test is deferred until redirected
  Qt Quick itself has a reusable capture harness.
* Fractional scale, mixed-DPR monitor movement, Windows D3D11, native Linux
  GPUs, and non-WSLg compositors still require their existing platform matrix.

## Primary sources

Exact Qt 6.10.2 sources inspected from the Ubuntu source packages:

* `qt6-declarative-6.10.2+dfsg/examples/quick/rendercontrol/rendercontrol_rhi/main.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/items/qquickrendertarget.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/items/qquickgraphicsconfiguration.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/items/qquickrectangle.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/items/qquickwindow.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/items/qquickscreen.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/scenegraph/qsgbasicinternalrectanglenode.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/scenegraph/qsgdefaultinternalrectanglenode.cpp`
* `qt6-declarative-6.10.2+dfsg/src/quick/scenegraph/coreapi/qsgbatchrenderer.cpp`

Stable upstream references:

* [Qt redirected QRhi example](https://doc.qt.io/qt-6.10/qtquick-rendercontrol-rendercontrol-rhi-example.html)
* [`QQuickRenderTarget`](https://doc.qt.io/qt-6.10/qquickrendertarget.html)
* [`QQuickGraphicsConfiguration`](https://doc.qt.io/qt-6.10/qquickgraphicsconfiguration.html)
* [`Rectangle` QML type](https://doc.qt.io/qt-6.10/qml-qtquick-rectangle.html)
* [`QQuickWindow::effectiveDevicePixelRatio()`](https://doc.qt.io/qt-6.10/qquickwindow.html#effectiveDevicePixelRatio)

The accepted ownership around the surrounding Wayland chrome remains
[ADR 0020](../decisions/0020-keep-qt-owned-wayland-windows-and-render-fallback-chrome-in-scene.md).
