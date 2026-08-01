# Ubuntu 26.04 Linux platform baseline

## Question

Can Sunroom's first Linux port use Ubuntu 26.04's system Qt, FFmpeg,
libplacebo, Vulkan/VAAPI/Wayland, and cubeb packages without maintaining a
second vendored dependency stack, and what constraints does that choice place
on the port?

This note records package and source inspection performed before the Linux
implementation. It is evidence for the execution plan, not a support claim.

## Reference environment

The inspected environment is Ubuntu 26.04 `resolute` under WSL2. Installed
development packages report:

| Dependency | Ubuntu package version | Relevant upstream/API version |
| --- | --- | --- |
| Qt Base, Declarative, Wayland | `6.10.2` Ubuntu packages | Qt 6.10.2, including private development headers |
| FFmpeg | `7:8.0.1-3ubuntu2` | `libavcodec` 62, `libavformat` 62, `libavutil` 60, `libswresample` 6 |
| libplacebo | `7.360.0-3` | API 360 |
| cubeb | `0.0~git20250401.975a727+ds-1` | distro snapshot with `cubeb::cubeb` CMake target |
| Vulkan | Ubuntu development packages | headers 1.4.341 in the inspected environment |
| libva / libdrm | Ubuntu development packages | libva 2.23, libdrm 2.4.131 in the inspected environment |

The matching patched Ubuntu sources were obtained with:

```sh
apt source qt6-base qt6-declarative qt6-wayland qt6-shadertools
apt source ffmpeg libplacebo cubeb
```

The source packages are preferable to the packaging repositories for this
investigation because they exactly match the installed binary packages and
include Ubuntu's applied patch series. Packaging Git repositories are only
needed to investigate unreleased packaging work.

The inspected WSL session has a live WSLg `wayland-0` socket and Vulkan 1.4
loader, but `vulkaninfo` exposes only Mesa 26.0.3 llvmpipe as a CPU device.
Neither `/dev/dxg` nor `/dev/dri` exists. `vainfo` reports VA-API 1.23 but
cannot find or initialize a driver. Consequently WSL2 is sufficient for
package discovery, compilation, unit tests, software-Vulkan validation, and
limited WSLg lifecycle smoke tests only if the active or nested compositor
exposes the required color-management-v1 capabilities. It is not evidence for GPU Vulkan,
native-compositor HDR, real display luminance, VAAPI/DRM PRIME modifier
behavior, device identity, or physical audio routing. Those claims need
native Ubuntu hardware.

## Build-system findings

* The current project requires a vcpkg manifest before evaluating the target
  platform and pins Qt 6.11.1 and libplacebo 7.360.1. Those are Windows build
  contracts, not valid Linux contracts.
* Ubuntu exports cubeb through `find_package(cubeb CONFIG)` as
  `cubeb::cubeb`; it does not install a `cubeb.pc` file.
* FFmpeg and libplacebo are available through pkg-config. The Linux build can
  use pkg-config imported targets without inventing project-local find
  modules.
* Ubuntu libplacebo reports Vulkan, glslang, built-in Dolby Vision, and LCMS
  support. It does not report Shaderc or D3D11. Linux dependency validation
  must therefore require a usable shader compiler backend rather than the
  Windows-specific Shaderc choice.
* The distro FFmpeg build exposes VAAPI and DRM PRIME facilities. Their
  presence at build time is not proof that a usable hardware device exists at
  runtime.
* Ubuntu's cubeb runtime links PulseAudio support along with other Linux
  backends. Sunroom can use cubeb's default Linux route for both a real
  PulseAudio server and PipeWire's PulseAudio-compatible server. Selecting a
  cubeb backend by name would add policy the application does not need.

The reference Linux contract is the Ubuntu 26.04 package family, not arbitrary
newer major versions. CMake should accept Qt `>=6.10,<6.11` on Linux because
Sunroom consumes private Qt RHI and Wayland APIs whose headers must match the
runtime packages. FFmpeg major ABIs and libplacebo API 360 should be checked
explicitly. Windows retains its existing vcpkg pins independently.

## Qt 6.10 Wayland and Vulkan findings

`QRhiVulkanInitParams` can import an existing `VkPhysicalDevice`, `VkDevice`,
and graphics queue. Qt also exposes
`preferredExtensionsForImportedDevice()`. The list includes preferences for
optional capabilities, so an external creator must prefilter it or pass
supported entries as optional rather than making every entry a hard device-
creation requirement. Qt does not take ownership of imported Vulkan objects.

The matching Ubuntu Qt Base source contains a downstream/upstreamed Vulkan
HDR-format fix. On Wayland, Qt 6.10.2 selects:

* `VK_FORMAT_R16G16B16A16_SFLOAT` with
  `VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT` for
  `QRhiSwapChain::HDRExtendedSrgbLinear`; and
* `VK_COLOR_SPACE_PASS_THROUGH_EXT` when the compositor exposes it, avoiding a
  second Vulkan-side color conversion while Qt's Wayland platform plugin owns
  the surface color declaration.

Linux `QRhiSwapChain::hdrInfo()` does not provide the Windows luminance
telemetry used by the existing display adapter. A Wayland display provider is
therefore required; swapchain format support alone is not display capability.

Qt's Wayland plugin creates a `wp_color_management_surface_v1` when a color
space is present in the window's requested `QSurfaceFormat`. For
`QColorSpace::SRgbLinear`, it creates a parametric image description with sRGB
primaries and the extended-linear transfer function. With no explicit
luminances, the protocol's sRGB defaults are 0.2-nit minimum, 80-nit maximum,
and 80-nit reference white. This agrees with Sunroom's extended-linear
working convention that `1.0` represents 80 nits.

Qt owns that surface protocol object. Creating a second
`wp_color_management_surface_v1` for the same `wl_surface` would be a protocol
error. A Sunroom adapter may instead create the distinct
`wp_color_management_surface_feedback_v1` object, parse completed preferred
image descriptions, and publish only the semantic output information Qt does
not expose through a suitable high-level API. It should not duplicate Qt's
image-description creation or surface policy.

For `QColorSpace::SRgb`, Qt 6.10 maps the surface transfer function to
color-management-v1 `gamma22`, not the piecewise sRGB OETF currently emitted
by Sunroom's SDR compositor. The same mapping remains in the upstream Qt 6.11
branch and current development branch. Qt attaches the description only after
its private image-description object reports `ready`; an early frame can
therefore precede the new declaration. That transient ordering is acceptable
under Sunroom's semantic convergence policy. Qt's private `failed` handler only
logs and does not expose a public application callback; this rare upstream
defect is not justification for shadow protocol ownership, duplicated probes,
or log parsing.

Qt 6.10's private native `QWaylandWindow` interface exposes the `wl_surface`
and surface-created/surface-destroyed signals. This is a narrow but explicitly
version-coupled access seam. It requires matching Qt private development
packages and should not leak beyond the Linux display adapter.

The declaration is independent of QRhi swapchain creation. Requesting
`SRgbLinear` and then falling back only the swapchain to SDR would leave a
linear-declared surface carrying SDR-encoded buffers. A correct fallback must
couple declaration and encoding: begin with a color-management-v1 managed
gamma-2.2 SDR surface, recreate the surface as sRGB-linear/FP16 for a managed
HDR attempt, and recreate it as managed gamma-2.2 SDR if that attempt fails.
This transition and Qt's surface recreation behavior require a real-compositor
spike.

The selected Linux V1 product scope requires the color-management-v1 global,
parametric description support, named sRGB primaries, the `gamma22` and
`ext_linear` transfer functions Qt 6.10 consumes, and preferred surface
feedback. A compositor that lacks that capability set is unsupported rather
than an unmanaged SDR fallback. SDR monitors remain supported through a
managed gamma-2.2 surface; the requirement is on the compositor stack, not on
the monitor being HDR-capable.

The relevant primary specifications and APIs are:

* [Qt `QRhiVulkanInitParams`](https://doc.qt.io/qt-6/qrhivulkaninitparams.html)
* [Qt 6.11 Wayland color-management implementation](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylandcolormanagement.cpp?h=6.11)
* [Qt development Wayland window implementation](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylandwindow.cpp?h=dev)
* [Wayland color-management-v1](https://wayland.app/protocols/color-management-v1)
* [Wayland color representation](https://wayland.freedesktop.org/docs/book/Color.html)

## Shared Vulkan-device direction

There are two plausible device-creation orders:

1. Let QRhi create the device and import it into libplacebo.
2. Create the device through libplacebo with Qt's requested extensions and
   import it into QRhi.

The accepted plan starts with the first because QRhi ownership preserves the
standard Qt lifecycle and keeps native Vulkan handles behind the existing
graphics domain. The spike must prove Qt's enabled feature and extension set
satisfies `pl_vulkan_required_features` before libplacebo imports the borrowed
device and queue. QRhi's creation API does not expose a caller-provided general
`VkPhysicalDeviceFeatures2` chain, so libplacebo-created device ownership
remains a focused fallback experiment if—and only if—the standard path fails a
measured requirement.

A window-scoped graphics context should create a Qt-owned `QVulkanInstance` at
Vulkan 1.2 or newer, install it on the `QWindow` before native-surface
creation, and outlive every recoverable logical-device generation. Once Qt's
`VkSurfaceKHR` exists, the child domain can let QRhi select/create the
present-capable device and import its device and queue into a borrowing
libplacebo Vulkan wrapper. Disable separate async compute and transfer families
for the first implementation. Imported frames, libplacebo image wrappers, and
the libplacebo device wrapper die before QRhi destroys its logical device;
the window context remains across device recovery.
Final window teardown must destroy the native surface before the context
destroys the Vulkan instance.

Both directions still require a focused proof. CPU queue exclusion does not
replace GPU synchronization. A QRhi-owned RGBA16F image wrapped by
libplacebo must cross ownership through `pl_vulkan_release_ex` and
`pl_vulkan_hold_ex`, and the resulting layout must be reported with
`QRhiTexture::setNativeLayout()`.

Qt 6.10.2's Vulkan backend assigns every externally supplied queue-wait
semaphore to `VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`. Sunroom samples
the rendered video target in the fragment shader, which may run before that
stage; wiring the libplacebo semaphore directly through QRhi's queue-submit
parameters therefore does not prove producer-to-sampler visibility. The spike
must evaluate a same-queue bridge submission/explicit barrier or another
mechanism and validate the actual fragment-read hazard. Vulkan standard and
synchronization validation must remain clean through accepted and aborted
frames, resize, surface recreation, and teardown before this becomes accepted
architecture.

## Media and color-policy findings

FFmpeg's `AVDRMFrameDescriptor` represents DRM PRIME objects, layers, planes,
offsets, pitches, and format modifiers. The intended first hardware route is
VAAPI decode, explicit mapping/export to DRM PRIME, and libplacebo's existing
FFmpeg mapping helper on the same Vulkan physical device. A retained
`AVFrame` must own the exported descriptors until GPU use completes. See the
[FFmpeg descriptor API](https://ffmpeg.org/doxygen/trunk/structAVDRMFrameDescriptor.html).

The VAAPI render node and Vulkan physical device must be proven to represent
the same adapter, preferably using Vulkan DRM device properties and the DRM
render-node identity. If they cannot be matched, hardware decode is an
unavailable capability and the existing bounded software restart applies.

Ubuntu libplacebo has LCMS enabled, unlike the current Windows build. The
current render path copies the mapped `pl_frame` into the effective source and
passes it to `pl_render_image`; an LCMS-enabled libplacebo can consume the
profile from that copy. The build feature would therefore silently change
Sunroom's renderer policy unless code clears both effective render-copy ICC
profile handles while retaining the original decoded metadata for diagnostics.
An ICC-tagged regression must prove the current cross-platform contract until
source ICC behavior is separately accepted and tested.

## Packaging and licensing consequence

The first Ubuntu artifact should consume the supported distribution's shared
libraries and declare appropriate ABI/runtime package dependencies rather than
copying Qt, FFmpeg, libplacebo, or cubeb into the application. This matches the
chosen system-dependency policy and ensures the Wayland platform plugin comes
from the same Qt package family as the private headers.

Before distributing that artifact, record the complete runtime dependency
closure and license notices for Ubuntu's actual builds. In particular, the
distro FFmpeg configuration and enabled codec libraries differ from the
current vcpkg build. This is a release gate, not a reason to create a second
Linux dependency build preemptively.

## Consequences for the plan

* Establish a system-dependency build and software-decoded SDR playback before
  adding hardware decode or claiming HDR.
* Keep one shared player, renderer, presentation engine, media reader, and
  audio sink; add only the platform domains/adapters required by existing
  seams.
* Treat managed gamma-2.2 native Wayland SDR and physically validated managed
  HDR as separate completion gates on the same required modern protocol stack.
* Keep WSL/lavapipe validation and native-hardware acceptance distinct.
* Do not clone packaging repositories, build custom dependency forks, or add
  X11/XWayland/unmanaged legacy-Wayland paths unless a later accepted decision
  changes the V1 scope.
