# Wayland CSD shadows, rounded corners, and window states

Date: 2026-10-17
Status: Research retained; the later shadow-only implementation is linked below.

Implementation update: the later shadow-only choice is accepted in
[ADR 0034](../decisions/0034-isolate-wayland-csd-shadows.md), with current
implementation/evidence in the [delivery plan](../plans/ui/2026-10-17-wayland-csd-frame-effects.md).
The original combined-effects investigation below is retained as research.

Appearance evidence: [libadwaita's window stylesheet](https://github.com/GNOME/libadwaita/blob/main/src/stylesheet/widgets/_window.scss)
uses centered 15%/14px-blur/5px-spread and 10%/5px-blur/2px-spread black layers.
The implementation borrows those layers, omitting the extra 1px edge because
SunPlayer already has an outline. [CSS specifies Gaussian sigma as half the blur radius](https://www.w3.org/TR/css-backgrounds-3/#shadow-blur).
Separable blurred-rectangle coverage naturally reduces corner opacity; a
nearest-distance Gaussian instead produced an overly heavy corner halo.

Scope update, 2026-10-17: after this investigation the user deferred client-side
corner rounding. Keep the body square and opaque; the current proposal concerns
shadows and window-state handling only. Rounding findings below are retained
as research evidence. See the updated execution proposal for active scope.

## Question and baseline

Can SunPlayer provide exterior shadows and rounded corners on its CSD path,
including correct fullscreen, maximize, tiling, restoration, and fractional
scaling, without duplicating Qt's native-window ownership?

Inspected the installed Qt 6.10.2 private headers, upstream Qt v6.10.2 sources,
current upstream protocol definitions, and production GTK, VLC, libdecor and
SDL sources. Moving upstream sources describe practice, not a pinned dependency
or runtime validation. No native rendering experiment was performed here.

Current truth remains [ADR 0020](../decisions/0020-keep-qt-owned-wayland-windows-and-render-fallback-chrome-in-scene.md).
The [August investigation](2026-08-02-qt-wayland-vulkan-window-decorations.md)
correctly identifies missing public Qt geometry controls. This investigation
adds evidence for a private native-interface route that the project already
uses elsewhere; it does not establish that every required behavior works.

Managed HDR still follows [ADR 0021](../decisions/0021-use-hdr10-pq-for-managed-wayland-hdr.md)
and its amendments, including [ADR 0033](../decisions/0033-keep-wayland-hdr-mastering-optional.md).
The HDR10 content surface remains PQ even on SDR outputs. Video source type
and monitor HDR status therefore cannot decide whether decoration alpha is safe.

## Protocol contracts

* `xdg_surface.set_window_geometry` describes the visible body, including its
  titlebar and border, excluding shadow extents. Configure dimensions refer to
  that geometry, not the larger buffer. Essential controls cannot live in the
  shadow area, which the compositor may clip.
* Input regions and opaque regions are independent, double-buffered surface
  state. Transparent pixels still receive input unless excluded. Never mark
  partially transparent corner/shadow pixels opaque.
* Fullscreen/maximized/tiled appearance must follow the compositor's configured
  state. Requests can be refused. Qt must retain configure acknowledgement and
  surface commit ownership.
* Tiled edges indicate where exterior decoration should disappear. Newer
  xdg-shell v7 constrained-edge states separately instruct clients not to resize from those
  edges. Do not assume every tiled edge is unresizable or that every Qt baseline
  exposes the newest protocol states.
* Suspended state is about repaint scheduling, not window shape.

Sources: [xdg-shell XML](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/xdg-shell/xdg-shell.xml),
[core surface contracts](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_surface).

There is no general upstream corner-radius notification or shadow service in
the inspected wayland-protocols tree. The background-effect protocol concerns
background blur. ChromeOS's vendor aura-shell does send desired window corner
radii; that is real but does not solve GNOME/KDE support. An application-owned
CSD radius is the practical portable policy. Compositors can also apply their
own effects independently; there is no general negotiation preventing all
such visual interaction.

Sources: [upstream protocols](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/tree/main),
[background-effect XML](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/ext-background-effect/ext-background-effect-v1.xml),
[aura-shell XML](https://chromium.googlesource.com/chromium/src/+/main/components/exo/wayland/protocol/aura-shell.xml).

## Production practice

### GTK

GDK separates shadow extents from configured and saved window sizes and from
minimum/maximum constraints. GtkWindow distinguishes fullscreen, maximized,
tiled and edge constraints. This supports keeping body geometry separate from
buffer geometry rather than treating padding as application content. GTK also
uses some exterior area for resize interaction: excluding all shadows from
input is a product choice, not a universal toolkit rule. For SunPlayer, retain
the existing inside-body resize zones initially.

Sources: [GDK Wayland toplevel](https://gitlab.gnome.org/GNOME/gtk/-/blob/main/gdk/wayland/gdktoplevel-wayland.c),
[GtkWindow](https://gitlab.gnome.org/GNOME/gtk/-/blob/main/gtk/gtkwindow.c),
[window documentation](https://docs.gtk.org/gtk4/class.Window.html).

### VLC / Qt

VLC's Wayland compositor uses a transparent Qt Quick view, the private Qt
`setCustomMargins` native interface behind a build feature check, and
`QWindow::setMask` to exclude most shadow area from input. Its QML enables
extended margins only when windowed and supplies a rounded-shadow shader.
Inspected VLC master revision: `11bcb6f56d59b43cf589556f426c7a5cbd460fdd`.

That windowed check handles maximized/fullscreen but does not detect Wayland
tiling, which can remain `Window.Windowed`. It is not a complete tiling precedent.

This is strong evidence for the geometry approach, not proof for SunPlayer's
HDR path: VLC's inspected arrangement has separate UI and video surfaces.

Sources: [VLC Wayland compositor](https://github.com/videolan/vlc/blob/11bcb6f56d59b43cf589556f426c7a5cbd460fdd/modules/gui/qt/maininterface/compositor_wayland.cpp),
[VLC main interface](https://github.com/videolan/vlc/blob/11bcb6f56d59b43cf589556f426c7a5cbd460fdd/modules/gui/qt/maininterface/qml/MainInterface.qml).

### libdecor / SDL

libdecor's Cairo plugin uses border subsurfaces and a cached shadow tile.
Fullscreen removes decoration; maximized removes shadows; tiling has a
distinct decoration policy. Separating decoration buffers from application
buffers is established practice, rather than intrinsically a bad design.
SDL delegates geometry to libdecor when using that path.

It is not a drop-in attachment for our Qt-owned toplevel. Adopting a decoration
subsurface would still require coordinated geometry, buffers, scaling, commits,
and teardown. It would address shadow alpha precision but would not by itself
round opaque HDR video corners. Compare that complete cost before selecting it.

Sources: [libdecor Cairo plugin](https://gitlab.freedesktop.org/libdecor/libdecor/-/blob/master/src/plugins/cairo/libdecor-cairo.c),
[SDL Wayland guidance](https://github.com/libsdl-org/SDL/blob/main/docs/README-wayland.md).

## Qt feasibility

Qt v6.10.2's `QNativeInterface::Private::QWaylandWindow` exposes
`setCustomMargins`. SunPlayer already obtains this interface in
`LinuxWaylandWindowContext`. Qt includes custom margins in shadow/content
geometry and adjusts the window size when margins change. This is the first
route to test; do not independently write xdg geometry behind Qt's back.

`QWindow::setMask` updates Wayland input regions. It does not render smooth
corners. Requesting an alpha-capable surface prevents Qt from declaring the
whole surface opaque; beginning with an empty opaque region is correct, though
potentially less efficient than declaring an opaque interior.

Tiling remains a gate: the concrete private QWaylandWindow exposes tiling
flags; the narrower native interface has no getter or notification. Inspected
Qt applies the flags before state/resize processing and sends expose updates
after later configures, including unchanged sizes. Observing Paint/Expose and
initial attachment is a concrete probe candidate: Qt can translate repeated
exposure into Paint and skip Expose if accepted. Reconcile after the event stack
because applying margins can resize synchronously. This private behavior needs
same-size idle-tiling validation. Do not add a polling timer or infer tiling from
screen dimensions; consider a Qt hook only if existing notifications cannot work.

Sources: [native interface](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/kernel/qplatformwindow_p.h),
[Wayland window](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandwindow.cpp),
[full private header](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandwindow_p.h),
[configure application](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/plugins/shellintegration/xdg-shell/qwaylandxdgshell.cpp),
[expose processing](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/kernel/qguiapplication.cpp).

## Rendering: the important HDR constraint

The current compositor writes alpha 1.0 after composing video, subtitles and
UI. A rounded QML rectangle cannot clip those separately composed layers.
Whole-window rounding must cover the final composition.

QRhi supplies `SurfaceHasPreMulAlpha`; its documentation also requires a
nonzero requested QWindow alpha-buffer size. Vulkan composite-alpha capability
must be checked: a flag request alone does not establish the actual mode.
Qt's implementation can choose another alpha mode when the requested one is
unavailable.

More significantly, Qt's HDR10 format selector uses RGB10A2 formats: alpha has
only four representable levels. A soft black shadow cannot be smoothly encoded
in that alpha channel. Rounded-edge antialiasing is also quantized, though its
visual acceptability needs separate assessment. Neither the FP16 intermediate
texture nor a better blur shader fixes final-buffer precision.

The default Wayland alpha convention is premultiplication after transfer
encoding (`premultiplied_electrical`). For that convention the final result is
`vec4(encode(straightColor) * coverage, coverage)`, not encoding an already
coverage-darkened linear color. A transparent black shadow contributes zero
RGB and nonzero alpha; compose coverage consistently at rounded edges.
The color-representation protocol documents this default; it need not be added
just to request default behavior.

Sources: [QRhi flags](https://doc.qt.io/qt-6.10/qrhiswapchain.html),
[Qt Vulkan format/alpha selection](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/rhi/qrhivulkan.cpp),
[color-representation XML](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/color-representation/color-representation-v1.xml).

Consequently a one-surface alpha design is a plausible SDR candidate, not yet
a complete HDR solution. A high-alpha-precision HDR WSI route would have to
preserve our declaration/encoding contract and be supported by Qt and target
compositors. Do not silently switch to SDR, substitute an incompatible linear
description, add noisy alpha dithering, or create a custom swapchain merely to
make shadows appear. A separate SDR shadow subsurface is an alternative for
explicit comparison if same-surface HDR cannot meet quality requirements.

## Recommended direction and unresolved work

The original combined-effects direction below is superseded in scope by the
user's rounding deferral. For shadows only, a separate SDR shadow subsurface
deserves a narrower comparison: it could preserve the existing opaque HDR body
without needing high-precision alpha in that body. Geometry and Qt-coordinated
lifecycle still need proof; this is an alternative, not an accepted change.

Prefer one Qt-owned surface if feasibility proves it adequate. Keep the scene
surface-sized, one inset body rectangle, and one derived geometry value used
by layout, final coverage, Qt margins and native input shape. Retain existing
fullscreen ownership and inside-body resize zones. Use a fixed application
radius and shadow initially. A cached shadow texture or one analytic shader
is sufficient; choose after the transport issue, without a blur framework.

Floating CSD gets rounding/shadow; fullscreen and maximized get no external
margins or rounding. Initially any tiling gets a square, margin-free body;
per-edge exposed-corner styling can wait. Tiled does not automatically mean
unresizable. SSD remains compositor-owned.

The next step is the [gated execution proposal](../plans/ui/2026-10-17-wayland-csd-frame-effects.md),
not production implementation. Outstanding gates are HDR alpha transport and
quality, actual configure/state notifications, margin changes during configure,
restoration/minimum-size semantics, and native scaling/input behavior.
