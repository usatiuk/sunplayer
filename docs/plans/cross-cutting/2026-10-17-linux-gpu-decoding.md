# Linux VAAPI decoding through DRM PRIME

Status: Delivered; native Intel validated, AMD and stress gates remain

## Scope and approach

Enable GPU decoding on the existing Wayland/Vulkan path. Reuse the shared
FFmpeg hardware negotiation, generation ownership, producer import contract,
and one-shot software restart. Keep Qt as Vulkan device owner. No new backend
selection, decoder pipeline, custom dma-buf importer, NVIDIA path, or Vulkan
Video implementation.

1. Research the installed Qt 6.10.2, FFmpeg 8.0.1 and libplacebo 7.360.0 APIs
   and production integrations; record the ownership/synchronization findings.
2. Request only the Vulkan extensions needed for dma-buf/modifier import.
   Pass the supported requested extensions to libplacebo without claiming an
   extension that Qt did not enable.
3. Match Vulkan's DRM render-device major/minor to the libdrm render node.
   Create FFmpeg's VAAPI device on that exact node. Unprovable identity or
   unavailable driver/import capability leaves software decoding available
   with a diagnostic reason.
4. Use libplacebo's FFmpeg mapper for direct VAAPI → DRM PRIME plane import.
   Retain each mapping until its imported textures finish GPU access; release
   completed mappings when the next mapping arrives, and drain at teardown.
   Keep retention bounded rather than allowing decoder-pool exhaustion.
5. Add production-boundary software/fallback and hardware integration cases
   for NV12/P010, pixel comparison, frame replacement, seek, stale generation
   after domain replacement, and unavailable-importer classification. Hardware
   absence must skip honestly;
   an explicit require-hardware environment switch turns absence into failure.
6. Build, run available tests and QML lint, independently review correctness,
   lifecycle/simplicity, and tests/docs, address findings, and update project
   truth. Run hardware acceptance outside a sandbox that hides DRM devices,
   with required hardware enabled; do not mistake sandbox visibility for the
   physical host's capabilities.

## Validation and delivery evidence

Debug (tests on) and Release (tests off) builds pass. All 33 CTests not
requiring an audio device pass on Weston/lavapipe, both QML lint targets pass,
and a sandbox required-VAAPI negative probe fails explicitly without hardware
access. Outside the sandbox, strict native NV12/P010 captures and turnover/seek
pass after the user installed the missing Intel iHD driver. All 35 registered
CTests pass on the actual host, including the two audio-device cases. The native
rendering suite reports 30 passed, 0 failed, with only five Metal/D3D11 skips;
VAAPI tests do not skip. AMD and asynchronous stress remain open. Full
environment, commands, outcomes, and remaining
coverage are recorded in [the evidence note](linux-port-evidence/2026-10-17-vaapi-drm-prime.md).

Three independent reviewers covered behavior/correctness,
architecture/lifetime/simplicity, and tests/evidence/docs/scope. No blocking
production findings remained. Test findings were addressed by optional
unsupported-codec skips (required hardware runs remain strict) and an explicit
keyframe-preroll timestamp/count assertion. The shared decoder intentionally
emits preroll; presentation filtering stays in playback rather than moving to
the decoder. No new decoder pipeline, descriptor mapper, shared retirement
framework, device-selection policy, or recovery state machine was added.
