# Optional HDR10+ preference

## Scope

Add a persistent, default-disabled preference for HDR10+ over Dolby Vision.
Use existing settings, decoded-source revision, importer remapping, and rendering
policy. A known HDR10-compatible base is required to bypass Dolby reconstruction.
Selection is independent of display brightness and stays stable across missing
metadata. Supported authored curves remain library-rendered on SDR and HDR.
No custom normalization, dependency patch, smoothing, or file-specific policy.

## Delivery

- [x] Persistent preference and live paused-frame invalidation.
- [x] Display-independent compatible-base selection.
- [x] Single mapped-curve validation, independent of average availability.
- [x] Dual-format source labels and post-presentation output diagnostics.
- [x] Independent correctness, simplicity, and evidence reviews.
- [x] Remove experimental rendering changes and restore original dependency.
- [x] Default preference to off; explicit stored values remain respected.
- [x] Final validation.
Commit and push authorized by the user after validation.

## Validation

Coverage includes persistence, UI editing, source labels, compatible/incompatible
bases, missing and late metadata, version-specific curve limits, zero-average
curve retention, paused remapping, real decoding, and final composition.
Native Settings-open visual refresh, macOS/Wayland output, and physical luminance
remain manual validation limits. No claim of full HDR reference equivalence.

Final Windows validation: all nine affected CTest suites passed (57.87 seconds).
After removing the redundant dialog initialization, settings-dialog and native
application playback checks passed again (4.81 seconds). The decoded source owns
the default; the window supplies its live state to the dialog before display.
Three independent reviews covered correctness, simplicity, and evidence.
