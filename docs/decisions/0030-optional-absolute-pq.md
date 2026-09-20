# 0030: Optional Absolute PQ on Windows HDR

* Status: Accepted
* Date: 2026-09-20
* Extends ADR 0029; adaptive playback remains the default from ADR 0025.

## Decision

Add one persistent, default-disabled Absolute PQ preference. It applies to
decoded PQ, HDR10+, and mapped Dolby Vision only on a Windows HDR scRGB output
with known physical white and luminance range. SDR output retains normal
conversion. HLG, SDR input, HDR Lab, UI, and subtitles retain their behavior.
macOS relative EDR and Wayland perceptual PQ presentation do not establish this
physical contract and are unsupported in this iteration.

For physical platform white W and available headroom H, use W*H as libplacebo's
destination maximum and 203/W as its existing output normalization. Windows
composition then multiplies by W/80: mapped video luminance L becomes L/80 in
scRGB. This replaces the adaptive source reference R only when Absolute is
effective. It does not modify metadata, add a mapper, or patch libplacebo.
Existing tone and gamut mapping still apply: this is neither encoded passthrough
nor a promise to preserve every sample unmodified.

The backend advertises physical luminance support only for active Windows HDR
linear scRGB. Shared presentation-target resolution combines it with known,
valid physical white and range, including peak at least white. The resolved
capability is carried into the existing surface description so output changes
invalidate cached rendering. Preference edits use the source's existing content
revision and update request, including while paused.

Settings disables the source reference-white controls only while Absolute is
effective. On unsupported outputs the Absolute checkbox is disabled, the
preference remains saved, and adaptive controls remain available. Returning to
supported HDR restores the selected mode. There is no separate reload path.

## Evidence and limits

Unit coverage exercises capability, persistence, settings, and coordinate math.
Decoded GPU tests cover final composition, source-reference independence,
retained-frame transitions, and unaffected formats. These validate application
pixels; physical emitted luminance and native monitor transitions still require
display testing.

The [Microsoft HDR guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
defines scRGB 1.0 as 80 nits and the SDR-white scale used by the existing
compositor. The [Wayland color-management contract](https://wayland.freedesktop.org/docs/book/Color.html)
does not make SunPlayer's current perceptual surface an equivalent absolute
presentation path.
