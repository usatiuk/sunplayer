# HDR input acceptance

Status: Complete

## Goal

Make SDR, HDR10/PQ, HLG, HDR10+, and Dolby Vision inputs pass through the
single production FFmpeg/libplacebo renderer with explicit, reproducible
policy and representative behavioral regression coverage. Preserve the best
available FFmpeg/libplacebo result rather than introducing SunPlayer-authored
format decoders or tone mappers.

## Grounded current state

* Playback already performs one FFmpeg open/demux/decode operation and gives
  the retained decoded `AVFrame` to the production libplacebo importer.
* Static SDR and analytic PQ captures validate reference-white-relative output
  through the real D3D11 target and final compositor.
* libplacebo currently inherits its pinned spline tone mapper and perceptual
  gamut mapper defaults. Inverse tone mapping, temporal peak detection, and
  dithering are explicitly disabled.
* The importer reports mapped HDR10+ scene-luminance metadata and distinguishes
  mapped Dolby Vision reshaping, parsed metadata with a base-layer result, and
  raw RPU-only input.
* Existing tests synthesize metadata edge cases but do not decode a
  representative mastered-PQ, HLG, HDR10+, or Dolby Vision fixture.
* The current virtual target is analytically validated for static PQ. Pinned
  libplacebo also uses an HDR destination maximum while inferring HLG display
  behavior, so HLG needs an observed target-response experiment before the
  same target can be claimed correct.

## Fixed decisions

* Keep one media operation; tests and production must not reopen or parse the
  same source through a second media pipeline.
* FFmpeg and libplacebo continue to own source decoding, HDR metadata mapping,
  Dolby Vision reshaping where supported, HLG rendering, tone mapping, and
  gamut mapping.
* Make spline tone mapping and perceptual gamut mapping explicit without
  changing the current visual policy.
* Keep inverse tone mapping, peak detection, and dithering disabled. Do not add
  temporal reset machinery while no temporal renderer feature is active.
* Validate user-visible rendered behavior and honest diagnostics, not private
  helper call sequences or a parallel metadata policy engine.
* Treat a valid HDR10 base layer as an accepted Dolby Vision fallback when the
  decoded profile/path cannot supply supported reshaping.

## Research result

The reproducible fixture and HLG decisions are recorded in
[HDR input fixtures and HLG target behavior](../../research/2026-08-01-hdr-input-fixtures-and-hlg.md).
Generate small 10-bit BT.2020 HEVC patch streams with FFmpeg/libx265. Inject a
hand-authored two-scene HDR10+ sequence with pinned `hdr10plus_tool` and a
generic Profile 8.1 sequence with pinned `dovi_tool`; neither tool becomes a
project dependency.

For V1, characterize and retain libplacebo's current display-relative HLG
response. Do not add a second HLG stage or dependency patch. The accepted claim
is functional display-relative playback, not absolute-reference HLG
monitoring; later physical evidence may motivate a focused upstream API.

## Intended implementation slices

1. Pin the current renderer policy explicitly and test the configured policy
   through a public renderer diagnostic or capture boundary.
2. Add small fixture manifests with hashes, provenance, encoded color facts,
   and expected FFmpeg/libplacebo path.
3. Decode every fixture once through the production frame boundary, render it
   through the persistent libplacebo D3D11 path, and capture RGBA16F output.
4. Add format-specific behavioral and diagnostic assertions, including seek
   or multi-frame progression for dynamic metadata where the fixture supports
   it.
5. Fix only failures demonstrated by the corpus, preferring pinned-library
   configuration or an upstream issue over a custom format pipeline.
6. Synchronize the video-rendering roadmap, accepted behavior, testing record,
   deferred gaps, and root progress summary.

## Acceptance

* Representative SDR, mastered PQ, HLG, HDR10+, and Dolby Vision inputs open,
  decode, and render through the production single-pass path. Production
  keyframe seek remains protected by the existing real inter-frame fixture
  rather than redundantly seeking each four-frame color sample.
* The selected output is finite, bounded by the target headroom, and responds
  monotonically to controlled luminance patches where an analytical oracle is
  available.
* Static PQ preserves the existing reference-white and no-unnecessary-
  expansion behavior.
* HLG target behavior is either validated and accepted or remains functional
  with a precise documented limitation and upstream action; it is not replaced
  by a SunPlayer HLG implementation.
* HDR10+ does not carry stale scene metadata across frames or generations.
* Dolby Vision diagnostics state whether reshaping was mapped or an available
  base-layer result was displayed.
* No second media open, probe, parser, decoder, or post-libplacebo tone mapper
  is introduced.

## Validation

* Focused integration tests use real pinned FFmpeg, libplacebo, QRhi, and D3D11
  where available.
* Fixture hashes and declared source facts are checked before rendering.
* Tests assert analytical properties and independent metadata observations
  rather than exact goldens generated solely by the code under test.
* The full registered CTest suite passes from the existing CLion build without
  running another configure.

## Commit boundaries

Use judgement after research. Prefer one fixture/policy foundation commit and
one acceptance/fixes/documentation commit if the corpus produces material code
changes; otherwise ship one coherent reviewed milestone.

## Actual outcome

The existing shared renderer accepted every required input family without a
format-specific SunPlayer decoder, parser, or tone mapper. The delivered change:

* explicitly selects spline tone mapping and perceptual gamut mapping while
  retaining disabled inverse mapping, peak detection, and dithering;
* publishes that renderer policy through production diagnostics and HDR Lab;
* adds deterministic four-frame Main10 HEVC fixtures for static PQ, HLG,
  two-scene HDR10+, and Dolby Vision Profile 8.1;
* decodes each fixture once through the production FFmpeg operation and renders
  the retained frames through the production importer/libplacebo/RGBA16F
  boundary;
* verifies static mastering/content-light metadata and analytical PQ values,
  HLG target-dependent response, frame-local HDR10+ progression and unchanged
  target metadata, and parsed/mapped Dolby Vision reshaping.

No temporal renderer reset hook was added because every temporal feature is
disabled. Existing tests continue to own real container seeking, target-only
rerender, final QRhi composition, and the single Windows output conversion.

## Validation evidence

* Two clean executions of the recorded generation commands produced identical
  SHA-256 hashes for all four checked-in streams.
* The complete configured Debug build succeeded through CLion's bundled CMake.
* All 24 registered CTest targets passed, including the four new HDR rows in
  `ffmpeg-first-frame` and the existing QRhi compositor/application scenarios.

## Remaining gaps

* HLG is accepted for display-relative playback but is not an absolute-
  reference monitoring claim.
* Dolby Vision evidence is scoped to the checked-in Profile 8.1 sequence;
  additional profiles, enhancement-layer residuals, and target trims remain
  unclaimed.
* BT.2020 SDR, broader range/bit-depth/chroma combinations, P010/P012/P016
  capture, actual target-gamut propagation, and physical display measurement
  remain later roadmap work.

Resulting commit subject: `Validate HDR input formats`.
