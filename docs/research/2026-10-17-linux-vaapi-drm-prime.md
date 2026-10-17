# Linux VAAPI / DRM PRIME integration research

Environment: Ubuntu 26.04.1, Qt 6.10.2, FFmpeg 8.0.1 (libavutil
60.8.100), libplacebo 7.360.0, libdrm 2.4.131, libva 1.23.0.
The initial sandbox hid `/dev/dri`; that was not the physical host's state.
Outside the sandbox the host has Intel Arrow Lake-P [8086:7d51], i915,
`/dev/dri/renderD128`, Mesa 26.0.8 and GNOME Shell 50.1 on native Wayland.

## Findings and selected approach

* [Qt 6.10.2 Vulkan backend](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/rhi/qrhivulkan.cpp)
  filters requested device extensions against the selected physical device and
  enables supported names. Keep QRhi device creation; request external-memory
  fd, dma-buf and DRM format modifier extensions, then report the supported
  requested subset to libplacebo. Vulkan 1.3 already supplies their promoted
  core dependencies. No external semaphore extension is needed for this path.
* `VK_EXT_physical_device_drm` supplies the selected Vulkan adapter's render
  major/minor. Use libdrm's `drmGetDeviceFromDevId` and verify the render node's
  character-device identity. Do not guess `/dev/dri/renderD128`, choose by
  vendor/device ID, or let FFmpeg select a default GPU.
* [FFmpeg 8.0.1 VAAPI backend](https://github.com/FFmpeg/FFmpeg/blob/n8.0.1/libavutil/hwcontext_vaapi.c)
  opens the named DRM node. Its read-only DRM export waits for VA decode with
  `vaSyncSurface`, exports separate layers, and retains the source frame in
  the mapping descriptor. That is a host-side decode-completion wait, not a
  CPU pixel transfer. Direct mapping forbids a transfer fallback.
* The installed [libplacebo 7.360.0 FFmpeg helper](https://github.com/haasn/libplacebo/blob/v7.360.0/src/include/libplacebo/utils/libav_internal.h)
  already derives DRM PRIME from VAAPI with READ | DIRECT, copies frame
  properties, checks per-plane formats/modifiers, imports dma-bufs and fixes
  P010 bit alignment. Use it rather than recreating its descriptor/import or
  HDR metadata handling. Unsupported export/modifier/import returns failure
  through the existing typed whole-operation software restart.
* `pl_unmap_avframe` releases its retained AVFrame immediately, even if the
  imported texture's Vulkan allocation survives asynchronously. Releasing a
  decode surface early would let FFmpeg reuse it while the GPU samples it.
  Poll every imported plane before unmapping. Retain at most one retired
  mapping alongside the current mapping; wait only when that oldest mapping
  is still busy at replacement. This fits the existing extra-frame reserve
  and avoids unbounded queues or a pool-sizing policy.
* [mpv's VAAPI integration](https://github.com/mpv-player/mpv/blob/master/video/out/hwdec/hwdec_vaapi.c)
  likewise exports separate DRM layers and synchronizes decode completion.
  SunPlayer can use the pinned FFmpeg/libplacebo helpers instead of mpv's
  custom plane mapper. No new fence protocol is warranted for this initial
  read-only path.

## Limits

The supported initial path targets VAAPI-capable same-device Vulkan/DRM
systems (Intel and AMD validation lanes). NVIDIA-specific APIs and cross-GPU
copies remain out of scope. Native Intel NV12/P010 import, pixel agreement,
24-frame turnover and seek now pass outside the sandbox. AMD, asynchronous
busy-surface stress, physical power/performance and device-loss injection
remain unvalidated.


## Native failure investigation and best-practice reassessment

A strict host run reproduced `Could not initialize VAAPI on
/dev/dri/renderD128: Input/output error`. Vulkan identity matching succeeded;
FFmpeg failed in `vaInitialize`, before codec selection or DRM plane import.
Host inspection found neither Intel driver package nor `iHD_drv_video.so`.
The user installed Ubuntu's `intel-media-va-driver-non-free` 26.1.2+ds1-1;
the same binary then passed NV12/P010 and turnover/seek tests without code or
environment overrides. The missing runtime driver was the reproduced cause.

The additional primary-source review confirmed these practices:

* [Intel media-driver documentation](https://github.com/intel/media-driver/blob/master/README.md)
  lists Arrow Lake under MTLx and distinguishes the full-feature and free-kernel
  distributions. Ubuntu's corresponding packages are
  [intel-media-va-driver-non-free](https://packages.ubuntu.com/resolute/intel-media-va-driver-non-free)
  and [intel-media-va-driver](https://packages.ubuntu.com/resolute/intel-media-va-driver).
  Libva development libraries and a Vulkan driver do not install the Intel
  media driver. Treat the VA driver as a GPU-specific runtime prerequisite,
  not a new compiled dependency or a reason to force a driver name in code.
* [libva DRM driver selection](https://github.com/intel/libva/blob/master/va/drm/va_drm_utils.c)
  is responsible for choosing the media backend. Keep its normal selection and
  supported environment overrides; the native passing run used neither
  `LIBVA_DRIVER_NAME` nor `LIBVA_DRIVERS_PATH`.
* [FFmpeg's pinned VAAPI backend](https://github.com/FFmpeg/FFmpeg/blob/n8.0.1/libavutil/hwcontext_vaapi.c)
  converts VA initialization failure to an I/O error. That string alone does
  not prove a decoder, dma-buf or Vulkan defect. Inspect the native runtime
  driver before changing import or synchronization code.
* [libva surface export contract](https://intel.github.io/libva/group__api__core.html)
  requires completion synchronization for exported read access. Keep FFmpeg's
  read-only direct map and `vaSyncSurface`, then retain the source until Vulkan
  sampling completes. Zero pixel transfers do not mean zero host waits.
* [Intel's compression-sharing guidance](https://github.com/intel/media-driver/wiki/Compression-Info-Sharing)
  describes explicit DRM modifiers and auxiliary compression planes. Do not
  assume all dma-bufs are linear or interchangeable. The pinned libplacebo
  mapper owns supported per-plane format/modifier acceptance; actual import
  failure retains the existing software restart, rather than introducing an
  untested custom layout conversion.
* [mpv's VAAPI mapper](https://github.com/mpv-player/mpv/blob/v0.41.0/video/out/hwdec/hwdec_vaapi.c)
  uses DRM export and decode synchronization, while
  [mpv's hardware decode guidance](https://github.com/mpv-player/mpv/blob/v0.41.0/DOCS/man/options.rst)
  documents codec-dependent support and software fallback. Those practices
  support the existing small integration; they do not justify adding a backend
  chooser, cross-device copy, forced modifiers, or firmware tuning for this
  missing-driver failure.

Hardware tests must run with host DRM access and `SUNPLAYER_REQUIRE_VAAPI=1`.
A sandbox-only skip is not evidence that the host lacks a GPU. Installing a
runtime driver affects newly created capabilities: restart SunPlayer after
installation, or use a new graphics-device generation through existing retry.
