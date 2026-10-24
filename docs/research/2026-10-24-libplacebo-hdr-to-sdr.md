# Current libplacebo HDR-to-SDR guidance

Read-only upstream research on 2026-10-24, performed in parallel with the local
mapping-control investigation. This is evidence, not a new accepted rendering policy.
The 20–300-nit prototype was reset; delivered controls retain 100–203/default100.
Accepted settings behavior is in ADR0040 and the delivery plan.

## Stable defaults and tradeoffs

The latest released libplacebo is 7.360.1, also the project's pinned Windows
version. Its defaults use spline, perceptual gamut mapping, ANY metadata and
peak detection. The HQ preset uses percentile 99.995 and contrast recovery;
ordinary detection uses percentile 100, smoothing 20 frames, scene thresholds
1/3 PQ-percent, black cutoff 1, and no delayed detection. These are upstream
defaults, not proof that spline is universally preferable. The current mpv
manual recommends BT.2446A for well-mastered content while gpu-next defaults
to spline. Keeping both choices permits a controlled comparison.

Sources: [stable color defaults](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/shaders/colorspace.h),
[renderer presets](https://github.com/haasn/libplacebo/blob/v7.360.1/src/renderer.c),
[mpv tone mapping](https://mpv.io/manual/stable/#options-tone-mapping),
[official peak options](https://libplacebo.org/options/#hdr-peak-detection).

Maintainer kasper93 diagnosed excessive peak smoothing in FFmpeg's integration
in issue 358: an old smoothing value of 100 made measured peaks lag. He describes
99.995 percentile as a perceptual tradeoff that can discard extreme highlights;
100 retains them but can compress more. Start with library defaults rather than
custom tuning without evidence.
[Maintainer discussion](https://github.com/haasn/libplacebo/issues/358).

## Detection is conditional and temporal

In stable hdr_update_peak, a configured detector can be skipped for existing
Dolby avg_pq_y, authored ST2094-40 curves, explicit metadata modes other than
ANY/CIE_Y, absent adaptation, or unavailable resources. ANY permits measured
statistics for ordinary HDR10; explicit HDR10 prevents their use. Keep authored
HDR10+ precedence. Configured enabled is not evidence that measurement ran;
pl_renderer_get_hdr_metadata and PL_RENDER_ERR_PEAK_DETECT can expose that later.

pl_renderer_flush_cache clears temporal peak history. Its API recommends
flushing for major source changes; mpv also flushes after seeking. Libplacebo
cleans up peak state when detection is disabled/skipped. Paused rerenders with
detection enabled can feed smoothing additional samples, so comparisons depend
on content/history as well as the current settings. Target changes alone do
not require a temporal reset.

Sources: [stable renderer implementation](https://github.com/haasn/libplacebo/blob/v7.360.1/src/renderer.c#L1093),
[flush and detected-metadata API](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/renderer.h),
[mpv seek handling](https://github.com/mpv-player/mpv/blob/master/video/out/vo_gpu_next.c).

## Development fixes beyond the latest release

The September/October 2026 development change replaces spline's polynomial
segments with hyperbolic segments. Maintainer kasper93 documents old shoulder
overshoot and flattened highlights for low target peaks with high scene
averages (example: 10,000 to 80 nits). Development code also adjusts knee
brightness matching near the output floor. This matters to the local 20-nit
reference-white experiment. Pinned spline does not include these fixes; no
dependency update was made as part of adding settings.

Sources: [release](https://github.com/haasn/libplacebo/releases/latest),
[spline fix and rationale](https://github.com/haasn/libplacebo/commit/ab96f7350222f4e02eaa1ff7c6ddb962a30720d7),
[development tone mapping](https://github.com/haasn/libplacebo/blob/master/src/tone_mapping.c).

Inverse tone mapping expands a lower-range source toward a higher-range target
and is unnecessary for this HDR-to-SDR experiment. Stable LINEAR normalization
uses 203 nits per unit; SunPlayer's reference-white setting changes the virtual
target and is not an ordinary exposure multiplier.
[Stable normalization](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/colorspace.h),
[inverse mapping](https://libplacebo.org/options/#inverse_tone_mappingyesno).

None of this establishes the cause of the native Linux/Windows brightness
mismatch. A checklist in discussion 303 was posted by SuRGeoNix, not a
libplacebo maintainer; do not attribute it to the developers. kasper93's reply
instead emphasizes stabilizing the core pipeline before black-level detection.
[Discussion](https://github.com/haasn/libplacebo/discussions/303).
