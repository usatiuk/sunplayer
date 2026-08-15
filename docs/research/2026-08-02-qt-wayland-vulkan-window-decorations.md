# Qt Wayland decorations for a Vulkan presentation window

Date: 2026-08-02

## Question

How should SunPlayer provide an operable native Wayland window when Qt owns the
toplevel and Vulkan presentation surface, Qt's built-in Wayland client-side
decorations are unavailable for Vulkan, and some compositors do not provide
server-side decorations?

The investigation also covered whether libdecor or libadwaita can decorate the
existing window, how much of xdg-decoration negotiation Qt exposes, whether a
real client-side shadow is practical, and how window-control icons should fit
the existing application style.

## Supported context

The implementation target is Ubuntu 26.04 with system Qt 6.10.2, native
Wayland, Vulkan/QRhi presentation, and one redirected Qt Quick scene. X11,
XWayland, alternate presentation backends, and broad desktop emulation are out
of scope.

SunPlayer's existing ownership is:

```text
Qt PresentationWindow
  wl_surface
    xdg_surface
      xdg_toplevel
  VkSurfaceKHR
  Qt color-management-v1 surface, when managed color is selected
  redirected Qt Quick scene
```

Any decoration solution must preserve that single ownership chain.

## Findings

### Qt's Vulkan limitation

Qt's Wayland plugin normally negotiates xdg-decoration and can load a client-
decoration plugin when client-side decoration is required. Qt commit
`2d4b0775` disabled that decoration path for `QSurface::VulkanSurface` because
the current decoration implementation assumes raster backing. The guard and
its TODO are present in the inspected Qt 6.10.2 source.

Qt 6.10 creates the xdg-decoration object but does not expose its final mode
through public API. The compositor retains the final choice. Qt stores the
configured result in private Wayland classes,
but public `QWindow` API does not report a reliable `Unknown`, `ClientSide`, or
`ServerSide` result. `frameMargins()` is not a protocol-mode substitute on
Wayland.

This does not justify a parallel decoration state machine. The relevant normal
cases are simple: WSLg/GNOME-style environments without the global need
application chrome, while modern KWin-style environments with server
decoration should be left to Qt and the compositor. The remaining mismatch is
a limitation rather than a V1 architecture axis.

### Libdecor cannot attach to Qt's toplevel

Although `libdecor_decorate()` accepts a `wl_surface`, libdecor then creates
the corresponding `xdg_surface` and `xdg_toplevel`, installs configure
listeners, acknowledges configure serials, sets window geometry, commits the
parent surface, and destroys those objects with its frame.

A `wl_surface` can have only one xdg-shell role. Qt already owns that role and
its configure/commit state. Separate Wayland event queues would not resolve
the ownership conflict. Making libdecor accept an externally managed
`xdg_toplevel` would require a fork and ongoing coordination with Qt's private
geometry, configure, input, mapping, and destruction behavior.

Libdecor is therefore compatible with Vulkan only when it owns the native
toplevel. It is not compatible with decorating this Qt-owned window.

### Libadwaita is not a decoration service

Libadwaita provides GTK window and widget classes. An Adwaita header bar lives
inside a GTK-owned application window and cannot be attached to a Qt Quick
scene or foreign `wl_surface`. Adding GTK merely for theme inspection would
still not supply compositor geometry or ownership and would introduce a large
second toolkit dependency.

### In-scene QML chrome fits the current architecture

Qt exposes public system move and resize requests on `QWindow`. Calling
`startSystemMove()` or `startSystemResize()` from the initiating pointer press
lets Qt retain the required Wayland input serial and delegates the interactive
operation to the compositor. Ordinary `showMinimized()`, `showMaximized()`,
`showNormal()`, and `close()` cover the remaining actions.

The existing redirected scene can render the titlebar, buttons, and eight
resize hit regions as one top-level layer:

```text
AppShell
  active page and full-window video viewport
  playback controls
  ClientSideWindowChrome
    optional overlay titlebar
    minimize / maximize-or-restore / close
    edge and corner resize regions
```

No second `QQuickWindow`, Wayland surface, Vulkan surface, or swapchain is
needed. Logical geometry stays in QML coordinates; the redirected Quick
texture already follows the effective device-pixel ratio.

The titlebar may fade with the playback controls. Opacity is visual state, not
layout state: active video keeps its full-window viewport while the chrome
overlays it, and empty/non-player content can request a stable titlebar inset.
### Decoration selection policy

The smallest policy available before native-surface creation is:

```text
xdg-decoration manager advertised
  -> normal Qt window; assume compositor server decoration

manager absent
  -> FramelessWindowHint; enable application QML chrome
```

Advertisement is not proof of the final compositor selection, but adding a
private listener, a hidden probe toplevel, compositor-name allowlists, runtime
mode switching, or raw xdg-decoration ownership would be disproportionate.
The supported happy paths are covered and the rare mismatch remains visible.

No user-facing `Auto | System | Application` preference is needed for V1.
Such an option would expose implementation policy before ordinary supported
systems demonstrate a need.

### Icons and color

Freedesktop icon themes can provide symbolic window-control glyphs through
Qt Quick Controls' `icon.name`. The minimize/maximize/restore symbolic names
are widely shipped but are not all mandatory core naming-specification icons,
so packaged fallbacks remain necessary.

The existing repository already vendors a small exact Lucide 1.28.0 subset and
lists each asset explicitly in its QML resources. Adding the upstream Lucide
minus, square, and copy glyphs follows that established boundary. The system
theme supplies a glyph shape when available; `icon.source` supplies the
bundled fallback.

SunPlayer's titlebar is deliberately black, independent of the desktop palette.
The application must therefore tint both icon sources with its own light
foreground. Asking for a hypothetical light icon variant or independently
reading `SystemPalette.windowText` could select a dark glyph from a light
desktop theme. No general palette or theme-token layer is needed until the
application actually supports multiple themes.

### Exterior client-side shadows are not a V1 feature

A true shadow beyond the window geometry needs a larger alpha-capable buffer,
premultiplied-alpha rendering, compositor-supported Vulkan composite alpha,
transparent margins, `xdg_surface.set_window_geometry()` excluding those
margins, and matching input and opaque regions. Maximized, tiled, and
fullscreen states must remove the margins. The partially transparent pixels
would also share the surface's HDR image description and require compositor-
specific validation of color-managed HDR blending.

Qt does not expose enough stable public Wayland frame-geometry control for
that design. VLC's more elaborate extended-frame path conditionally uses Qt
private custom-margin support, which reinforces rather than removes this
constraint. V1 uses compositor shadows only when server decoration supplies
them.

## Resulting implementation boundary

`WindowChromeController` exposes only enabled/fullscreen/maximized state and
public move, resize, minimize, maximize/restore, and close operations. QML owns
appearance and hit regions. Neither side exposes Wayland, xdg-shell, Vulkan,
libplacebo, color management, or playback generations.

The Linux capability inventory observes the decoration-manager global before
native-window creation. It does not bind or own an xdg-decoration object. Qt
continues to own the complete native toplevel and presentation lifecycle.

The accepted decision is recorded in
[ADR 0020](../decisions/0020-keep-qt-owned-wayland-windows-and-render-fallback-chrome-in-scene.md).

## Sources

Primary sources inspected or used for API contracts:

* Qt 6.10.2 `src/plugins/platforms/wayland/qwaylandwindow.cpp`
* Qt 6.10.2 xdg-shell and xdg-decoration integration under
  `src/plugins/platforms/wayland/shellintegration/xdg-shell/`
* [Qt commit disabling Wayland CSD for Vulkan](https://github.com/qt/qtbase/commit/2d4b07753407af18e05f53e7e39431a67dfc9fe9)
* [Qt `QWindow` system move/resize API](https://doc.qt.io/qt-6.10/qwindow.html)
* [Qt Quick Controls icon API](https://doc.qt.io/qt-6.10/qtquickcontrols-icons.html)
* [xdg-decoration protocol](https://wayland.app/protocols/xdg-decoration-unstable-v1)
* [xdg-shell window geometry](https://wayland.app/protocols/xdg-shell)
* libdecor `src/libdecor.c` from the Ubuntu-matching source package
* [libadwaita `AdwApplicationWindow`](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/class.ApplicationWindow.html)
* [VLC QML CSD move/resize precedent](https://github.com/videolan/vlc/blob/master/modules/gui/qt/widgets/qml/CSDMouseStealer.qml)
* [Freedesktop icon naming specification](https://specifications.freedesktop.org/icon-naming-spec/latest/)
