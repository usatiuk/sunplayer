# Restore reference-white-adaptive HDR playback

Status: Complete

## Goal

Restore the accepted normal-playback contract across Windows, macOS, and
managed Wayland:

```text
working RGB 1.0 = active platform SDR/reference white
working highlight ceiling = usable peak / reference white
```

The change must retain the valid metadata-first HDR-to-SDR work, remove the
decoded-only Windows cancellation, preserve each platform's native output
encoding, propagate bounded macOS and Wayland target primaries, and leave the code
simpler than `5f5b737`.

The grounded evidence and rejected alternatives are recorded in
[the research note](../../research/2026-08-22-reference-white-hdr-regression.md).
The persistent delivery ledger is
[the correction checklist](2026-08-22-reference-white-hdr-correction-checklist.md).

## Completion condition

Normal decoded PQ/Dolby playback follows the platform reference-white anchor;
HDR-to-SDR still uses a nominal 100-nit destination; HDR highlight compression
still responds to available headroom; Windows, macOS, and managed Wayland keep
their distinct native serialization contracts; affected automated tests pass;
and current architecture/testing/decision documentation describes the same
behavior as the code.

## Invariants

* Source pixels and retained static/dynamic metadata remain source truth.
* Libplacebo remains the only tone and gamut mapper.
* The composed surface has one luminance meaning for video, subtitles, and UI.
* Platform presentation performs one native coordinate conversion.
* Windows normal playback does not contain a producer `203 / W` factor.
* macOS keeps final scale `1`; no synthetic physical SDR-white value is added.
* Wayland keeps final scale `1` and PQ's 203-nit source-reference declaration;
  the compositor owns source/output reference-white anchoring.
* A no-headroom PQ/Dolby target may use fixed nominal 100-nit mapping and a
  corresponding fixed `203 / 100` coordinate conversion.
* Headroom changes may alter highlight compression. Tests must not require
  whole-image invariance when the source no longer fits.

## Non-goals

* Adding an absolute/reference-monitoring user mode.
* Adding exposure, gamma lift, a second tone mapper, or a new custom viewing
  curve.
* Claiming physical emitted luminance without display measurement.
* Inferring absolute macOS luminance from relative EDR headroom.
* Treating Wayland's theoretical preferred target maximum as measured panel
  peak.
* Solving the separate Wayland exact target-volume declaration question
  without compositor evidence.
* Building an ICC parser or deriving exact macOS native xy from arbitrary
  display profiles in this change.

## Implementation slices

### 1. Remove the normal-HDR absolute-mode exception

Remove `targetPeakLuminanceKnown` from the rendered-video surface and remove
`canUseAutomaticPhysicalTarget()` from presentation policy. Delete the
`useAbsoluteTargetLuminance` decision state and its physical-HDR branch.

After source color inference, the render context derives:

```text
absoluteLuminanceSource = PQ transfer or mapped Dolby representation
nominalSdrTarget = targetPeakHeadroom <= 1 and absoluteLuminanceSource
coordinateWhite = nominalSdrTarget ? 100 : 203
targetMaximum = coordinateWhite * targetPeakHeadroom
```

Only `nominalSdrTarget` installs the fixed coordinate-normalization hook. Every
target with headroom above `1` uses the relative `203 * headroom`
construction. This one rule is shared by analytic and decoded entry points.

Keep that calculation as one small pure, production-used function at the
existing render-context boundary, returning coordinate white, target maximum,
and output-normalization scale. The function exists so hosted CPU tests can
guard the exact algebra even though hosted Windows excludes GPU tests; it is
not a new policy layer.

### 2. Preserve source policy without target-policy leakage

Keep metadata validation, provenance, fallback maxima, Dolby generation state,
and mapper selection. Gate HDR10+ source OOTF selection to the nominal-SDR case
where its source-provided target and the fixed application target share
physical units. Relative HDR keeps validated HDR10+ scene/static maxima,
mapped-Dolby source range, and the explicit 1,000-nit PQ fallback with spline;
it does not invent physical target authority.

Remove blanket “physical target unavailable” diagnostics because relative HDR
is the selected normal-playback policy. If a valid source OOTF exists, diagnose
that it was not applied on a reference-white-adaptive HDR target.

Remove diagnostic-string assertions that exist only to prove the deleted
absolute branch. Keep diagnostics focused on mapper and metadata provenance.

### 3. Unify analytic and decoded target-coordinate behavior

Both render entry points reach the same render-context target construction.
The analytic producer may choose different source metadata, but it must not
silently use a different output luminance coordinate for the same mapped
source transfer and target headroom.

### 4. Propagate bounded platform target primaries

Copy validated preferred target primaries into `DisplayState`, falling back to
the preferred primary color volume only when explicit target primaries are
absent. Preserve the current PQ-203 encoding and compositor anchoring path.

On macOS, use the already researched `NSScreen.canRepresentDisplayGamut:`
boundary: publish standard Display P3 only when the active screen reports P3
support, otherwise BT.709 when sRGB is representable. Refresh on screen color-
space changes. Keep the layer tagged extended-linear sRGB and leave ColorSync
as the sole final display-profile transform. This is a conservative usable
gamut, not exact native chromaticities.

### 5. Add behavioral regressions

Use existing test targets and fixtures; do not introduce a new test framework.

* Update a production decoded-PQ capture with fixed headroom, primaries, and
  zero/unknown target minimum so changing only reference white keeps the
  producer samples stable rather than requiring `surface * W` invariance.
* Pass those exact production surfaces through `HdrCompositor` with linear
  output and transparent UI/subtitles. At both whites assert
  `composed = surface * W / 80`, and assert the composed-value ratio equals the
  reference-white ratio.
* Retain a separate same-white case where reduced headroom compresses
  highlights, plus a separate nonzero-minimum case.
* Retain nominal-100 HDR-to-SDR pixel coverage.
* Keep the existing diagnostic compositor coverage but describe it only as
  analytic coverage; the production decoded case above is the regression gate.
* Remove unit cases for physical-target authority and add coverage for the
  simplified surface validity and typed policy decisions. Human diagnostic
  strings remain secondary evidence.
* Unit-test the production target calculation directly: PQ/Dolby at `H = 1`
  selects `R = 100`, `max = 100`, and scale `203 / 100`; PQ/Dolby at `H > 1`
  selects `R = 203`, `max = 203 * H`, and scale `1` independently of `W`;
  SDR/HLG remain relative.
* Extend Wayland preferred-description tests to require target-primary
  propagation and fallback.
* Model macOS production state without synthetic peak authority: final scale
  `1`, relative EDR headroom, no invented physical reference-white value, and
  an AppKit-confirmed conservative target gamut.

### 6. Synchronize project truth

Add an ADR that supersedes ADR 0024's normal-HDR physical-target decision while
retaining its nominal-100 HDR-to-SDR result. Restore ADR 0008's normal-playback
scope. Update ADR 0023, rendered-surface documentation, video-rendering and
testing documentation, the root/subsystem plans, deferred claims, and the
research index.

Documentation must distinguish automated numeric evidence from native display
measurement. It may claim Windows native xy, macOS's conservative P3/BT.709
lower bound, and Wayland preferred target-volume propagation, but not exact
cross-platform physical gamut.

## Validation

All build and test commands run outside the sandbox. Windows commands initialize
the Visual Studio developer environment. Before a manual CMake reconfigure,
check whether CLion is running and allow its automatic reload to settle rather
than starting a competing configure.

Planned checks:

1. Focused unit tests for presentation target, rendered surface, color policy,
   and Wayland color management.
2. Windows production FFmpeg first-frame HDR cases.
3. Windows QRhi/libplacebo compositor capture.
4. The complete configured Windows test selection appropriate to the available
   machine.
5. QML lint only if documentation/diagnostics UI text changes.
6. Platform-neutral Linux tests when the local environment is available;
   otherwise record the exact unvalidated boundary.

## Review and delivery

Before production edits, three independent lenses review this plan for
behavior, test evidence, and simplicity. After implementation, three
independent read-only reviews inspect the actual diff. Material findings are
resolved and affected checks rerun before the plan and checklist become
Complete.

No commit, push, package, or release is part of this plan without separate
authorization.

## Outcome and evidence

The deletion-first correction is implemented. Three independent pre-edit
reviews shaped the contract and test oracle; three fresh post-change reviews
checked color/platform correctness, tests/documentation, and architectural
simplicity. Their stale-documentation findings were corrected. The final
Wayland review also confirmed the already scoped outgoing target-volume gap;
the exact optional-protocol lifecycle needed to fix it remains explicitly
deferred rather than being represented as proven native behavior.

On 2026-08-22, using the existing Debug tree and a Visual Studio 2026 x64
developer environment outside the sandbox:

* the complete configured build succeeded;
* both `all_qmllint` targets passed;
* all 34 registered CTests passed, including `qrhi-compositor` and
  `ffmpeg-first-frame`;
* the focused HDR10+→SDR capture stored the pinned approximately 88.6-nit
  mapped patch at approximately `0.886`, directly exercising the retained
  nominal-100 coordinate conversion.

No manual CMake reconfigure was run. Native macOS compilation/current-screen
capability observation, managed-Wayland HDR WSI/compositor behavior, and
physical emitted luminance remain unvalidated on this Windows host.
