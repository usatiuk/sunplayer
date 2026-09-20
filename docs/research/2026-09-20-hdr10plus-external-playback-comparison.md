# HDR10+ external playback comparison

## Observations

On 2026-09-20, the user reported the same visible brightness jumps in a
dual-format source through SunPlayer's HDR10+ path and through mpv. The mpv
build used was `v0.41.0-dev-g0b7ed670f`, with these options:

```text
--vo=gpu-next
--vf=format=dolbyvision=no:hdr10plus=yes
--tone-mapping=st2094-40
--hdr-compute-peak=no
```

These options retain HDR10+ metadata, disable Dolby Vision metadata, select
ST2094-40 mapping, and disable measured peak detection. The user reported
that the Dolby Vision rendition did not show the same jumps. A separate
hybrid test sample looked normal through both SunPlayer paths.

This is a user-reported visual comparison, not an instrumented capture or
a comparison with matched render targets and dependency versions. Media
names and locations are intentionally omitted.

## Interpretation and scope

The symptom is reproduced outside SunPlayer. Treat the source metadata or
the shared libplacebo interpretation as the remaining investigation, rather
than compensating for this clip in SunPlayer. Both players use libplacebo;
the comparison does not prove that the file is malformed or identify an
upstream defect. It also does not establish that their complete rendering
paths are identical.

Retain the default-disabled HDR10+ preference and the library-owned mapping
described in [ADR 0028](../decisions/0028-prefer-hdr10plus-on-compatible-dual-format-video.md).
Do not add metadata repair, brightness smoothing, source-peak substitution,
dependency patches, or file-specific selection to conceal these jumps.

## Focused code review

Correctness, simplicity, and tests/documentation reviews found no remaining
jump-specific workaround in the current importer, color policy, or producer.
Authored HDR10+ curves pass to libplacebo unchanged. Existing regressions
retain curves independently of scene-average availability and exercise
representation changes; they do not require brightness to remain constant
across changing authored metadata.

Keep compatibility checks, supported-curve validation, the representation
latch across missing metadata, and paused-frame invalidation: these protect
normal format handling and the explicit preference, not this clip's appearance.
No rendering code or dependency changes resulted from this review.
