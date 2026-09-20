# Optional Absolute PQ presentation

## Scope and decisions

Add an off-by-default Absolute PQ preference for decoded PQ, HDR10+, and
mapped Dolby Vision. First implementation supports Windows HDR scRGB with
known physical reference white and luminance range. macOS relative EDR and
Wayland perceptual PQ composition do not establish the same absolute contract;
the user explicitly accepted a Windows-only first iteration. SDR and HLG
remain unchanged. Keep source metadata, existing mappers, and composition.

Absolute is a physical luminance convention, not bitstream passthrough or a
promise that tone mapping preserves every in-range sample. On supported output,
coordinate white W replaces the adaptive source reference R: destination W*H,
normalization 203/W, then Windows composition W/80 gives mapped nits/80.
UI/subtitles remain anchored to platform white. The source reference control
is ignored only while Absolute is effective.

One backend capability authorizes the physical scRGB contract. Shared target
resolution combines it with known valid white/range (peak at least white),
then passes availability into the surface description. Availability changes
invalidate the existing surface cache. Source preference changes use the same
content revision/update path as other decoded-video controls.

Settings keeps the preference saved but greyed out on unsupported outputs,
shows an explicit adaptive-fallback message, and enables reference controls
whenever adaptive is effective. No new reload, display-identity, or mapping
subsystem. A small named-value snapshot replaces the growing positional
Settings arguments.

## Delivery

- [x] Capability resolution, shared render mapping, settings and persistence.
- [x] Unit coverage for capability gates, UI, persistence and surface reuse.
- [x] Real GPU captures for physical scaling, mode transitions and unaffected HLG/SDR.
- [x] Current documentation and decision record (ADR 0030).
- [x] Independent correctness, simplicity, and evidence reviews.
- [x] Build and affected checks outside the sandbox.

Windows Debug build passed. All nine affected CTest suites passed in 89.09s:
presentation-target, application-settings, settings-dialog,
rendered-video-surface, libplacebo-color-policy, media-session,
active-video-source, qrhi-compositor, and ffmpeg-first-frame. The decoded GPU
suite includes a 50-nit PQ patch checked against 0.625 scRGB, physical output
invariance across platform whites, source-reference independence, unchanged
HLG/SDR pixels, and retained-frame mode/capability changes without reimport.

Independent review identified and resolved two issues: known SDR output now
overrides a stale HDR backend capability, and the GPU test includes the
independent absolute-nit oracle rather than relying only on invariance. Targeted
review confirmed both fixes. No remaining review findings. Diff checks pass.

Physical emitted luminance and native monitor transitions remain manual checks.
No additional platform support is claimed from shared arithmetic alone.
