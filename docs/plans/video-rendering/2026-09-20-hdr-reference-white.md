# HDR reference-white setting

## Scope

Add one cross-platform decoded-video preference: source HDR reference white,
100–203 nits, default 100. Playback Settings provides a slider, numeric field,
and 100/203-nit presets. Reuse source content revision for paused-frame updates
and existing application persistence. Leave composition white, subtitles,
platform encoding, source metadata, and mapper selection untouched.

## Rendering contract

For PQ and mapped Dolby (including HDR10+ on the compatible base), give
libplacebo destination maximum R times headroom and normalize its resulting
linear pixels by 203/R. The working ceiling remains headroom. R=100 preserves
the previous nominal SDR endpoint; R=203 preserves the previous adaptive HDR
convention. This is a presentation preference, not source metadata repair.
HLG stays unchanged by user decision: its native OOTF is target-relative.
Analytic HDR Lab rendering retains its existing diagnostic convention.

## Delivery

- [x] Settings, persistence, and shared source invalidation.
- [x] Library target mapping and bounded coordinate conversion.
- [x] Unit and real decoded-frame regression coverage.
- [x] Current subsystem documentation and decision amendment.
- [x] Correctness, simplicity, and evidence reviews.
- [x] Build and affected test validation outside the sandbox.

No dependency patch, platform-specific gain, separate reload path, or unrelated
refactoring is in scope. Native emitted luminance remains a manual validation
limit; GPU readback verifies application calculations.

## Validation

Windows Debug build passed. All nine affected CTest suites passed:
application-settings, settings-dialog, rendered-video-surface,
libplacebo-color-policy, media-session, active-video-source, ffmpeg-first-frame,
qrhi-compositor, and application-audio-first-playback. The first seven completed
in 86.80 seconds; compositor/native application checks completed in 10.26 seconds.

The new GPU tests cover 203→150→100→203 on a retained source with one import,
unchanged SDR/HLG output, bounded highlights, and the uncompressed PQ ratio.
Both preset values also run headroom-one continuity and composition sweeps.
Review requested the round-trip and new-default continuity coverage; both
were added and passed. No implementation defects remained after independent
correctness, simplicity, and evidence reviews.

An initial blanket brightening assertion failed for the authored HDR10+ fixture.
Pinned libplacebo's target-adaptation formula independently explained the
response, and fresh-renderer captures matched retained-renderer captures.
The test and UI wording now respect authored mapping rather than imposing
uniform exposure semantics. The Dolby fixture's absolute neutrality tolerance
was also corrected to scale with the tested headroom.

Commit and push were explicitly authorized after validation. Native macOS and
Wayland execution and physical display measurements were not performed here.
