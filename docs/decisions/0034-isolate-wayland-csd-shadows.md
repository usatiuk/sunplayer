# 0034: Isolate CSD shadows in the Linux Wayland integration

* Status: Accepted
* Date: 2026-10-17
* Amends ADR 0020's exterior-shadow and single-surface restrictions.

## Context

GNOME-style CSD needs a shadow, while the existing managed PQ presentation
uses RGB10A2 with only four alpha levels. Cosmetic effects do not justify
changing HDR transport or spreading frame margins through QML, viewport and
pointer geometry. Rounded corners are explicitly deferred.

## Decision

The Linux context owns one concrete private `WaylandWindowShadow`, created
only when application CSD is selected. It attaches one passive ARGB8888 SHM
subsurface below the unchanged opaque Qt-owned window. No other platform,
shared interface, rendering pipeline or application-layout change is needed.

Qt remains the sole toplevel/configure/parent-commit owner. The child uses
synchronized commits, empty input/opaque regions and a fixed 24-DIP exterior
extent. Qt's explicit xdg window geometry excludes the child. Applied native
window states suppress shadows in fullscreen, maximized and any tiled state;
existing QML already suppresses the titlebar and outline in fullscreen.

Use two restrained centered layers based on libadwaita, with no extra edge,
theme service, activation styling, appearance preference or per-edge effect
framework. Cache by size and integer-ceiling DPR. Two immutable SHM buffers
bound resize pressure; exhausted slots temporarily hide the shadow and a
release event retries the latest geometry. Tear down the child before its
parent and rebuild after native-surface replacement.

Missing subcompositor is an optional external capability failure. Storage and
mapping failures leave playback running without a shadow for that geometry;
changed geometry or native-surface recreation bounds retries. Broken native
contracts and impossible internal sizes fail fast.

## Alternatives and consequences

Same-surface transparent margins would couple cosmetics to alpha precision,
HDR encoding, scene layout and input geometry. A shadow-only child preserves
those existing contracts and follows libdecor's use of decoration subsurfaces.
Do not attach libdecor itself: it would compete with Qt for the toplevel.

There is one extra surface and a small Linux-native buffer owner, but still
one application window, media renderer, graphics device and swapchain. The
private Qt integration stays behind the existing Linux build boundary.
Fractional-monitor visual quality and physical HDR blending remain native
acceptance checks rather than conclusions from a software compositor test.

See the [research](../research/2026-10-17-wayland-csd-shadows-and-rounding.md)
and [delivery plan](../plans/ui/2026-10-17-wayland-csd-frame-effects.md).
