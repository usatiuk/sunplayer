# 0040: Configure playback tone mapping by active output

* Status: Accepted
* Date: 2026-10-24
* Amends ADR 0039's fixed operator choice for decoded HDR playback.

## Decision

Persist separate playback algorithm choices: SDR offers BT.2446A (default),
Spline and BT.2390; HDR offers Spline (default) and BT.2390. One settings editor
follows the actual presentation output and edits only that output's saved value.
The canonical resolved VideoColorTarget carries the existing active-HDR fact,
including at headroom one; do not infer output identity from headroom or monitor
capability. Target equality already invalidates paused output after a change.

DecodedVideoSource owns one typed settings value. Its existing content revision
and update request apply live edits without recreating the producer/importer.
SDR-source and HDR Lab operator selection remains unchanged. Source-authored
HDR10+ ST2094-40 precedence, scene-only HDR10+ selection and mapped Dolby
representation/metadata handling remain unchanged. The selected generic curve
does not force an override of authored guidance.

One shared peak-detection preference defaults on. Ordinary PQ static/fallback
metadata permits libplacebo's measured luminance through ANY when enabled;
existing explicit dynamic and mapped-Dolby metadata selections remain explicit.
Libplacebo owns detection eligibility and its default smoothing. Diagnostics
distinguish the configured preference from a successfully measured peak.
Existing media/seek producer replacement releases history; retained producer
identity/representation discontinuities flush the library's temporal state.
Target-only rerenders do not flush it.

## Consequences

No new display epoch, metadata policy engine, platform fork or alternate
normalization. UI has one active-output editor and one detector checkbox.
Reference white remains 100–203/default100. Inverse mapping, dithering, physical
target discovery and source metadata ownership are unchanged. Spline/BT.2390
preferences do not establish physical display accuracy or solve the reported
native Windows/Wayland brightness difference.

Evidence and limitations are recorded in the
[delivery plan](../plans/video-rendering/2026-10-24-tone-mapping-controls.md) and
[upstream research](../research/2026-10-24-libplacebo-hdr-to-sdr.md).
