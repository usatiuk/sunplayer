# 0017: Require color-management-v1 for the first Linux release

* Status: Accepted
* Date: 2026-08-01
* Related:
  [0013: Rely on system display calibration on managed presentation paths](0013-rely-on-system-display-calibration.md),
  [0015: Target Wayland and leave X11 unsupported](0015-wayland-only-linux-desktop.md),
  [0016: Reconcile output changes by semantic value](0016-reconcile-output-changes-semantically.md)

## Context

ADR 0015 selected native Wayland and allowed an unmanaged sRGB fallback when a
Wayland compositor lacked color management. That fallback still creates a
second Linux presentation contract: different startup/capability behavior,
surface declaration, display confidence, transition handling, diagnostics,
packaging requirements, and acceptance tests. It also cannot advance the
player's defining managed HDR goal.

The first Linux release can target a modern Wayland stack instead. Supporting
both SDR and HDR monitors does not require supporting unmanaged compositors:
color-management-v1 can declare a managed gamma-2.2 surface for SDR
presentation and a managed extended-linear surface for HDR presentation.

## Decision

Linux V1 supports only native Wayland compositors exposing the
color-management-v1 capabilities Sunroom uses:

* the color-manager global and parametric image-description creation;
* named sRGB primaries plus the `gamma22` and `ext_linear` transfer functions
  used by Qt 6.10 for managed gamma-2.2 and extended-linear declarations;
* perceptual rendering intent, which Qt uses when applying the description;
* per-surface color-management ownership through Qt's Wayland integration; and
* preferred surface-description feedback and immutable description
  information used by Sunroom's display adapter.

This is a capability contract, not a compositor brand or release-number
allowlist. The implementation will report the exact missing capability and
fail during startup when the contract is unavailable. It will not run an
unmanaged legacy-Wayland surface.

Qt remains the sole owner of the color-management object attached to its
`wl_surface`. An SDR monitor remains supported: Sunroom requests
`QColorSpace::SRgb`, Qt declares sRGB primaries with `gamma22`, and Sunroom's
final compositor emits the matching power-2.2 SDR encoding. When the output
and Vulkan WSI support the HDR path, Sunroom requests
`QColorSpace::SRgbLinear` and couples Qt's `ext_linear` declaration with the
FP16 swapchain. Failure of that optional HDR transition recreates a managed
gamma-2.2 SDR surface; it does not fall back to `UnmanagedSrgb`.

Qt installs the description asynchronously after its private image-description
object reports `ready`. Sunroom does not gate the first buffer or take over
surface ownership to make this transient ordering exact; it reconciles the
latest semantic presentation mode at a safe boundary. Qt does not expose
failure of its private per-surface request through a public API. Sunroom treats
that rare path as an upstream defect covered by native integration evidence,
not as a product state that justifies duplicated description logic, a second
ownership model, or brittle log parsing.

Windowed/fullscreen movement between SDR and HDR outputs reconciles between
these two managed modes using the latest complete semantic target. The media
operation and logical graphics device remain intact unless actual device loss
or replacement-surface incompatibility requires bounded recovery.

This decision supersedes only ADR 0015's unmanaged Wayland SDR fallback. ADR
0015's native-Wayland-only and no-X11/no-XWayland decisions remain accepted.
ADR 0013's `UnmanagedSrgb` mode remains valid for other explicitly supported
platform paths, but it is not a Linux V1 presentation mode.

## Consequences

* The first Linux implementation has one compositor color-management owner and
  one managed surface protocol across SDR and HDR monitors.
* Startup, packaging, diagnostics, and tests must expose and enforce the
  required protocol capabilities.
* CI or WSLg presentation tests require a compositor exposing the capability
  set; otherwise those environments remain useful for build/unit validation
  only.
* HDR still requires FP16 WSI support and physical display evidence. Requiring
  the protocol does not turn every monitor into an HDR target.
* Older or incomplete Wayland compositors are unsupported for V1 even if they
  could display an unmanaged SDR window.
* A later compatibility release may add unmanaged Wayland only through a new
  decision with an explicit product need and validation budget.

## Alternatives considered

### Keep an unmanaged SDR fallback for older Wayland compositors

Rejected for V1. It broadens startup, presentation, diagnostics, transition,
and test behavior before the modern managed path is proven.

### Require an HDR monitor

Rejected. The modern protocol supports managed gamma-2.2 SDR presentation,
and SDR monitors remain part of the intended Linux product behavior. Qt's
declaration is specifically gamma 2.2, so Sunroom matches that steady-state
transfer.

### Name specific supported compositors or minimum release numbers

Rejected. Protocol capability is the behavior Sunroom depends on; a brand or
version allowlist is both less precise and more brittle.
