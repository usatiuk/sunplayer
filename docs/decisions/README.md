# Architecture decisions

Decision records describe significant accepted choices and why they were made.
They complement subsystem documentation:

* Subsystem documentation describes current architecture and behavior.
* A decision record preserves the context, alternatives, and consequences of a
  choice.
* Research notes preserve evidence and unresolved investigation; they do not
  become accepted architecture automatically.

Use sequential filenames such as `0003-short-title.md`. Each record should
state its status and date. Amend a record for clarification, but supersede it
with a new decision when the accepted choice changes materially.

## Index

* [0001: Application-owned QRhi with redirected Qt Quick](0001-application-owned-qrhi-composition.md)
* [0002: Prefer extended-linear sRGB presentation with explicit SDR-white mapping](0002-extended-linear-srgb-presentation.md)
* [0003: Normalize rendered video to one display-targeted linear surface](0003-display-targeted-video-surface.md)
* [0004: Establish one graphics-device domain and explicit video interop seams](0004-cross-platform-graphics-domain-and-video-interop.md)
* [0005: Retain FFmpeg frames at the decoded-frame boundary](0005-retain-ffmpeg-frames-at-the-decoded-frame-boundary.md)
* [0006: Open media asynchronously behind a stable active-video source](0006-asynchronous-media-session-and-stable-active-video-source.md)
* [0007: Bound continuous video and select frames on the presentation thread](0007-bound-continuous-video-and-select-on-presentation-thread.md)
* [0008: Anchor normal HDR playback to the platform reference white](0008-reference-white-adaptive-hdr-display-mapping.md)
* [0009: Use generation-scoped decode restarts for seeking and recovery](0009-generation-scoped-seek-restart.md)
* [0010: Use Qt category logging with bounded session files](0010-qt-category-logging-and-bounded-session-files.md)
* [0011: Route selected media once and separate decoded audio from device output](0011-single-pass-media-routing-and-audio-output-boundary.md)
* [0012: Use final decoded frames as source-color truth](0012-use-final-decoded-frames-as-color-evidence.md)
* [0013: Rely on system display calibration on managed presentation paths](0013-rely-on-system-display-calibration.md)
* [0014: Prefer native Metal presentation for the macOS graphics domain](0014-prefer-native-metal-presentation-on-macos.md)
* [0015: Target Wayland and leave X11 unsupported](0015-wayland-only-linux-desktop.md)
* [0016: Reconcile output changes by semantic value](0016-reconcile-output-changes-semantically.md)
* [0017: Require color-management-v1 for the first Linux release (superseded)](0017-require-wayland-color-management-v1.md)
* [0018: Support unmanaged sRGB SDR on native Wayland](0018-support-unmanaged-srgb-wayland-sdr.md)
* [0019: Import the QRhi Vulkan device into libplacebo](0019-import-the-qrhi-vulkan-device-into-libplacebo.md)
* [0020: Keep Qt-owned Wayland windows and render fallback chrome in-scene](0020-keep-qt-owned-wayland-windows-and-render-fallback-chrome-in-scene.md)
* [0021: Use HDR10/PQ for managed Wayland HDR presentation](0021-use-hdr10-pq-for-managed-wayland-hdr.md)
* [0022: Paint the Windows background only before first presentation](0022-paint-windows-background-only-before-first-present.md)
* [0023: Use a metadata-first HDR-to-SDR policy](0023-use-metadata-first-hdr-to-sdr-policy.md)
* [0024: Map PQ against absolute target luminance (superseded for normal HDR playback)](0024-map-pq-against-absolute-target-luminance.md)
* [0025: Keep normal HDR reference-white adaptive](0025-keep-normal-hdr-reference-white-adaptive.md)
