# 0020: Keep Qt-owned Wayland windows and render fallback chrome in-scene

* Status: Accepted
* Date: 2026-08-02
* Related:
  [0001: Application-owned QRhi with redirected Qt Quick](0001-application-owned-qrhi-composition.md),
  [0015: Target Wayland and leave X11 unsupported](0015-wayland-only-linux-desktop.md),
  [0018: Support unmanaged sRGB SDR on native Wayland](0018-support-unmanaged-srgb-wayland-sdr.md)

## Context

Qt 6.10 disables its Wayland client-decoration plugin for Vulkan windows.
WSLg and GNOME-style environments may not provide server-side decorations, so
a normal Qt Vulkan window can otherwise be left without usable move, resize,
or window buttons.

Sunroom already has exactly one Qt-owned `QWindow`, `wl_surface`, xdg-shell
toplevel, `VkSurfaceKHR`, redirected Qt Quick scene, and color-management
surface owner. Libdecor is not a decoration attachment API for an existing xdg
toplevel: it creates and manages the xdg surface/toplevel, configure
acknowledgements, geometry, commits, and destruction. Libadwaita supplies GTK
windows and widgets rather than a service for foreign Qt surfaces.

The xdg-decoration global indicates that negotiation is available, but Qt has
no public API for the compositor's final selected decoration mode. Solving
that rare mismatch exactly would require Qt-private integration or duplicate
Wayland state.

## Decision

Qt remains the sole native-window and presentation-surface owner.

Before creating the native window, the Linux context inventories Wayland
globals:

* If `zxdg_decoration_manager_v1` is advertised, Sunroom leaves the normal Qt
  window flags unchanged and assumes the supported modern compositor supplies
  server decoration.
* If the global is absent, Sunroom sets `Qt::FramelessWindowHint` before native
  creation and enables one application-chrome layer inside the existing
  redirected QML scene.

This is deliberately a small happy-path policy, not a second negotiation
engine. Sunroom does not inspect private Qt decoration state, create its own
xdg-decoration object, probe with another toplevel, maintain compositor
allowlists, or switch decoration ownership after mapping. A compositor that
advertises the protocol but nevertheless leaves Qt's Vulkan window
undecorated is a documented V1 limitation.

The application chrome:

* uses only public `QWindow::startSystemMove()`, `startSystemResize()`, and
  ordinary minimize/maximize/restore/close operations;
* is one logical module independent of playback, Wayland, Vulkan, and color
  management;
* overlays the complete redirected scene;
* hides the titlebar with the existing playback activity policy without moving
  an active video's full-window viewport;
* reserves titlebar height only when non-player/empty content needs a stable
  inset;
* disables resize chrome while maximized or fullscreen;
* uses a black titlebar, fixed Sunroom foreground tint, system symbolic icon
  names when available, and the existing bundled Lucide subset as fallback;
  and
* does not create a second window, subsurface, swapchain, or presentation
  owner.

V1 does not create an external client-side shadow. A real exterior shadow
would require alpha swapchain behavior, transparent buffer margins, correct
xdg window geometry, input/opaque regions, and validated HDR blending. Qt does
not expose the required public Wayland frame-geometry control for this
architecture.

## Consequences

* WSLg and compositors without xdg-decoration receive in-scene Vulkan-window
  controls wired to public Qt operations with no native ownership conflict;
  native interaction evidence remains pending.
* Compositors offering the normal server-decoration path retain native
  decorations and shadows.
* The fallback geometry and interactions are testable through the shared QML
  boundary and could later be reused by an explicitly scoped Windows custom-
  chrome feature.
* Theme icons influence glyph shape only. Sunroom controls tint so a light
  desktop icon theme cannot make controls unreadable on the black titlebar.
* Exact desktop button order, KDecoration/Adwaita styling, exterior shadows,
  and the rare advertised-but-client-decorated negotiation result are not V1
  correctness requirements.

## Alternatives considered

### Integrate libdecor

Rejected. It would compete with Qt for the same xdg-shell role and configure,
geometry, commit, and destruction lifecycle.

### Integrate libadwaita or GTK

Rejected. These toolkits cannot decorate a foreign Qt-owned surface and would
add a second UI stack without solving ownership.

### Observe the final decoration mode through Qt private APIs

Rejected for V1. It would couple application behavior to more private Wayland
implementation detail solely to cover an uncommon compositor choice. The
simple global-based policy handles the supported happy paths.

### Always force application chrome

Rejected. It would discard good compositor-native decoration and shadows on
systems where Qt can negotiate them normally.

### Add transparent client-side margins and shadows

Deferred. The required window geometry and HDR alpha behavior are not exposed
or validated through the selected high-level Qt boundary.
