# Subtitle composition brightness

Add one persistent 0–100% subtitle brightness control, default 80% (current
rendering), for SDR and HDR. SubtitleAppearanceValues owns the default. Apply
brightness only to decoded linear subtitle RGB, preserving alpha, authored
colors and the video/UI layers. Reuse overall opacity's settings-change and
composition-only path; no new renderer, tone mapper, or invalidation mechanism.
Style presets preserve brightness; Restore defaults restores 80%.

- [x] Existing settings, persistence, UI and compositor wiring.
- [x] Regression coverage: persistence, UI, no raster invalidation, final pixels.
- [x] Build, affected tests, independent review and documentation.

Windows Debug application and affected targets built successfully. Six CTest
suites passed: subtitle-settings, application-settings, settings-dialog,
qrhi-compositor, subtitle-renderer, and application-audio-first-playback
(10.77 seconds). GPU tests check 0/25/80/100% across all output encodings and
independent opacity/layer ordering. Three independent reviews found no blockers.
No native macOS/Wayland or physical luminance measurements are claimed.
