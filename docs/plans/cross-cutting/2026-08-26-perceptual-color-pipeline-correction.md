# Correct unknown-SDR black without redesigning HDR presentation

Status: Complete — automated validation and independent review passed; native
comparison items remain explicitly deferred below

## Goal

Fix the confirmed Windows SDR shadow regression at the libplacebo target
boundary, add regressions that exercise the real unknown-minimum state, and
leave currently accepted HDR presentation behavior unchanged.

This correction intentionally separates three evidence grades:

* **confirmed fix:** an unknown no-headroom SDR minimum must reach libplacebo
  as numeric `0`, allowing its documented SDR contrast inference;
* **protected baseline:** Windows HDR, managed Wayland HDR, nominal SDR
  normalization, and tone-map selection do not change in this patch;
* **investigation:** macOS ordinary-video reference `100` versus `203` requires
  a diagnostic A/B and native comparison before it becomes production policy.

Grounded evidence and open questions are recorded in
[the research note](../../research/2026-08-26-sdr-black-and-hdr-reference-policy.md).
Progress and acceptance evidence are tracked in
[the checklist](2026-08-26-perceptual-color-pipeline-correction-checklist.md).

## Completion condition

The production Windows SDR state is verified as minimum-unknown; unknown SDR
minimum reaches libplacebo as `0`; known zero and positive minima retain their
existing meaning; a pinned numerical oracle and the production GPU path cover
meaningful PQ near-black separation with an explicitly selected mapper; the
existing Windows HDR producer and final `W / 80` behavior remain unchanged and
tested; affected and complete configured checks pass; and current documentation
matches the bounded change.

## Non-negotiable boundaries

* Do not change the Windows HDR `203 * H` producer mapping or its single final
  whole-composition `W / 80` scale. Windows HDR currently looks correct and is
  the native acceptance baseline for this repair.
* Do not change managed Wayland's PQ-203 image-description contract.
* Do not change the nominal 100-nit HDR-to-SDR target or its `203 / 100`
  producer normalization in this patch. These are retained SunPlayer policies,
  not requirements imposed by Windows.
* Do not change BT.2446A/spline selection while changing target-black input.
* Do not add an absolute-video-anchor field to the rendered-surface model yet.
  The macOS policy it would carry is unresolved.

## Implementation

### 1. Verify the production state

Confirm from code, diagnostics, and a targeted presentation test that the
actual Windows SDR path produces:

```text
targetPeakHeadroom = 1
targetMinimumLuminanceKnown = false
targetMinimumLuminanceNits = 0
```

SunPlayer currently reads `MinLuminanceInNits`, but only marks the range
authoritative in Windows HDR mode. Therefore the claim is narrowly that the
current Windows SDR path has no authoritative target minimum—not that Windows
can never expose one.

### 2. Preserve minimum-luminance semantics

Keep the existing `known + value` representation and change only its
libplacebo translation:

```text
unknown and H <= 1  -> 0, letting libplacebo infer SDR contrast
unknown and H > 1   -> PL_COLOR_HDR_BLACK, preserving current behavior
known zero          -> PL_COLOR_HDR_BLACK
known positive      -> existing physical-range conversion
```

The unknown-HDR result and known-zero result intentionally converge at the
libplacebo boundary. The H>1 rule is conservative preservation pending better
display data; target transfer is currently linear, so passing unknown there
would otherwise infer `targetPeak / 1000` as an HDR/EDR black point too.

Expose only the smallest production-used calculation needed for a hosted unit
test. Do not introduce another policy object.

### 3. Add regressions that observe the bug

Extend the existing focused coverage rather than creating duplicate policy
tests:

* target-minimum cases for unknown SDR, unknown HDR/EDR, known zero, and known
  positive physical minimum;
* fold pinned `pl_color_space_nominal_luma_ex` coverage and a BT.2446A oracle
  at 0.01, 0.05, 0.1, 0.5, 1, and 5 nits into the existing independent EETF
  coverage; distinguish inferred 0.1-nit SDR black from known zero rather than
  checking only monotonicity;
* add production GPU coverage using an in-memory high-bit-depth neutral PQ
  `AVFrame`, an explicit unknown-minimum argument in the existing capture
  helper, and MaxCLL metadata that explicitly selects BT.2446A; do
  not add or modify a persistent binary media fixture;
* retain the existing metadata-less PQ coverage that asserts its spline
  fallback, so the BT.2446A ramp cannot silently exercise the wrong mapper;
* presentation coverage proving Windows SDR requests an unknown target range;
* retain/extend existing no-regression coverage for stable Windows HDR producer
  values as `W` changes and `W / 80` exactly once at composition.

### 4. Synchronize project truth

Update only current documents that encode the affected claim: the video
rendering README/plan, testing documentation, and root plan where necessary.
Add one small amending ADR and mark ADRs 0003/0008/0025 as amended where they
encode the old minimum translation. Do not rewrite their historical rationale
as though it never happened.

## Independent investigations after the confirmed repair

### macOS `100` versus `203`

Keep normal playback unchanged and final EDR scale `1`. Run a temporary
diagnostic experiment that can render the same absolute PQ patches under
reference choices `100` and `203`, including 50/100/203/400-nit patches and the
near-black ramp. Retain a product control only if later evidence justifies it.
Compare both choices with QuickTime/AVFoundation on the same display,
brightness setting, content, crop, and frame.

Automated readback can prove only surface coordinates. It cannot prove emitted
luminance or perceptual parity. Apple documents a legitimate 100-nit HDR10/PQ
video convention, while BT.2408 and newer Apple HAGC guidance also recognize
203-nit HDR reference white. Production policy follows the native experiment,
not an assumption that either number is universal.

### BT.2446A versus spline

After the black fix is physically tested, compare the two curves as a separate
variable. Do not add a preference, custom curve, or automatic policy change in
this patch.

## Validation and review

All build and test commands run outside the sandbox after initializing the
PowerShell Visual Studio developer environment. Before any manual configure,
check whether CLion is running; if it is, wait for its reload and do not start a
competing configure.

Run focused presentation-target, color-policy, compositor, and FFmpeg-frame
tests, then the complete configured build/test selection. Obtain three
independent reviews of this exact revised plan before production edits and
three post-change reviews covering color correctness, architecture/failure
risk, tests/docs/scope, and explicit simplicity/anti-overengineering.

No commit, push, package, or release is authorized by this plan.
