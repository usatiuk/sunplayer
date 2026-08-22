# Reference-white HDR correction checklist

Status: Complete

## Purpose and update rules

This is the persistent TODO and evidence ledger for
[the correction plan](2026-08-22-reference-white-hdr-correction.md).

Statuses are `Pending`, `In progress`, `Needs native hardware`, `Blocked`, and
`Done`. Keep IDs stable. An item becomes Done only when its acceptance condition
is implemented and its evidence is recorded here.

## Gates

| ID | Gate | Status | Acceptance and evidence |
| --- | --- | --- | --- |
| RESEARCH-1 | Regression and platform contract | Done | The research note reconstructs the exact Windows cancellation, separates valid nominal-100 mapping from invalid normal-HDR policy, verifies macOS EDR and Wayland PQ anchoring, and records the test/CI failure. |
| PLAN-1 | Independent pre-edit review | Done | Behavior, test-evidence, and simplicity lenses approved a deletion-first repair. Review corrections now require an inferred shared render-context rule, fixed-headroom/two-white production composition evidence, separate compression/minimum tests, realistic macOS state, and no new platform abstraction. |
| CODE-1 | Remove physical-HDR cancellation | Done | Normal HDR uses `203 * headroom`; all producer `203 / W` plumbing and automatic physical-target authority were deleted. |
| CODE-2 | Preserve nominal-100 HDR-to-SDR | Done | The production-used target calculation selects 100 nits and fixed `203 / 100` only for PQ/mapped-Dolby at headroom one; SDR/HLG remain relative. |
| CODE-3 | Remove superseded state and split behavior | Done | `targetPeakLuminanceKnown`, `canUseAutomaticPhysicalTarget`, and `useAbsoluteTargetLuminance` are removed; analytic and decoded rendering share one small target calculation after source inference. |
| CODE-4 | Propagate managed-Wayland target primaries | Done | Explicit preferred target primaries reach `DisplayState`; absent target primaries fall back to the preferred primary color volume without changing PQ-203 anchoring. |
| CODE-5 | Propagate conservative macOS target gamut | Done | The active `NSScreen` publishes P3 only when `canRepresentDisplayGamut:` confirms it, otherwise sRGB/BT.709 when representable; color-space change notification refreshes state and ColorSync remains the final transform. |
| TEST-1 | Production reference-white regression | Done | Static PQ at fixed headroom and zero minimum keeps producer samples stable at 160/240-nit whites; those exact surfaces cross `HdrCompositor`, with each output matching its platform scale, Windows ratio 1.5, and macOS ratio 1. Reduced-headroom and positive-minimum cases remain separate. Pinned HDR10+→SDR output directly locks the fixed nominal-100 conversion at approximately `0.886`. |
| TEST-2 | Platform serializer contracts | Done | The production test encodes distinct Windows/macOS scale laws; pure policy covers relative EDR target propagation; Wayland unit cases cover explicit and fallback primaries while the existing compositor tests retain PQ-203 encoding. |
| TEST-3 | Focused and full validation | Done | Existing Debug tree first built five affected targets and passed focused CTest 5/5. After review fixes, the complete tree built outside the sandbox with VS 2026 x64, both QML lint targets passed, and CTest passed 34/34 including the GPU and hardware-decode labels. macOS/Wayland native boundaries remain recorded under `NATIVE-1`. |
| DOC-1 | Decisions and subsystem truth | Done | ADRs, plans, READMEs, testing claims, deferred items, historical research amendments, and the research index agree with implemented behavior and bounded evidence. |
| REVIEW-1 | Independent post-change review | Done | Three read-only reviewers checked platform/color correctness, tests/docs, and architecture/simplicity. Stale research/testing claims and the missing direct HDR10+ nominal-SDR pixel lock were resolved. The outgoing managed-Wayland target-volume finding is explicitly deferred with its required protocol lifecycle and native evidence. |
| NATIVE-1 | Physical output measurement | Needs native hardware | Emitted luminance and perceptual equivalence remain explicitly unclaimed until measured on representative Windows HDR, macOS EDR, and managed-Wayland HDR systems. |

## Explicitly deferred or rejected

| Item | Disposition | Reason |
| --- | --- | --- |
| Absolute/reference-monitoring playback | Deferred | It is a separate explicit viewing intent, not a repair for normal playback. |
| Additional viewing curve or compositor compensation | Rejected | It would stack image transforms instead of restoring the shared surface contract. |
| Synthetic macOS physical-white telemetry | Rejected | Current AppKit evidence is relative EDR headroom only. |
| Application-side Wayland `W / 203` scale | Rejected | It would duplicate the compositor's required reference-white anchoring. |
| Exact Wayland target-volume declaration | Deferred | The stable BT.2020/PQ description still defaults to a BT.2020/10,000-nit target after application mapping. Aligning it requires optional mastering-feature tracking, asynchronous preferred-description-driven replacement, rejection fallback, and native compositor evidence. |
| Exact macOS ICC-derived target chromaticities | Deferred | AppKit's P3/sRGB capability lower bound is implemented. Reducing arbitrary matrix/LUT display profiles to exact raw primaries is a separate grounded design. |
| SDR-only macOS wide-gamut presentation | Deferred | The ordinary sRGB swapchain cannot carry P3 merely because the screen can represent it; a managed wide-gamut output encoding is separate work. |
