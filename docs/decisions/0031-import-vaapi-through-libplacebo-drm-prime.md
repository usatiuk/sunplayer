# 0031: Import Linux VAAPI surfaces through libplacebo DRM PRIME

* Status: Accepted
* Date: 2026-10-17
* Builds on: [0019](0019-import-the-qrhi-vulkan-device-into-libplacebo.md)

## Decision

Keep QRhi's Vulkan device owner and the existing cross-platform decode/import
contracts. Request the external-memory fd, dma-buf and DRM modifier extensions;
report the supported requested subset that Qt 6.10 enables to libplacebo.

Advertise VAAPI only when the imported libplacebo GPU can import dma-buf
textures and Vulkan's DRM properties identify an accessible matching render
node. Resolve that major/minor through libdrm, verify character-device
identity, and pass its explicit path to FFmpeg. Do not guess an adapter or
let FFmpeg select a different default device.

Use `pl_map_avframe_ex` for VAAPI-to-DRM PRIME export/import. Its READ | DIRECT
mapping delegates decode-completion waiting, descriptor ownership, separate
planes, modifiers, P010 alignment, and retained metadata to FFmpeg/libplacebo.
The host may wait for VA decode; no pixel download/upload is introduced.

Retain each mapped `pl_frame` until all its imported textures report completed
GPU access. The importer holds one retired mapping alongside the current
mapping; replacement waits only when that retired mapping remains busy.
Teardown drains it before destroying the GPU. This bounded ownership fits the
existing extra decoder-frame reserve and prevents premature surface reuse.

Use the existing generation check and one-shot software re-decode at the
logical playback position for unsupported native import. Missing capability
leaves software decoding available with a diagnostic reason. No per-frame
CPU transfer, cross-GPU copy, new recovery state machine, or backend picker.

## Why

The pinned library stack already implements the difficult export/import work.
Reimplementing it would duplicate plane/modifier/metadata logic and descriptor
lifetime. A custom Vulkan device creator is unnecessary because Qt can enable
the needed extensions. Releasing a mapped frame immediately is unsafe:
libplacebo defers native texture destruction but frees the AVFrame at unmap,
allowing the decoder to reuse a surface still being sampled.

## Limits

The implementation builds and its shared software Vulkan path passes in the
sandbox. Outside that sandbox, native Intel Arrow Lake NV12/P010 import,
pixel agreement, continuous turnover and seek pass after the missing iHD
runtime driver was installed. AMD modifier import and asynchronous surface
reuse stress still need real-device evidence. NVIDIA-specific APIs, Vulkan Video,
non-VAAPI DRM producers and cross-device import are outside this slice.

See the [research](../research/2026-10-17-linux-vaapi-drm-prime.md) and
[delivery evidence](../plans/cross-cutting/2026-10-17-linux-gpu-decoding.md).
