# Windows D3D11VA edge-corruption diagnosis

> Status: implemented and validated, 2026-08-23.

## Conclusion

The scene-dependent green, cyan, or magenta line at the bottom of some Windows
videos is caused by sampling decoder-surface padding, not by the source file,
Dolby Vision, HDR metadata, or SunPlayer's color policy.

Before this repair, SunPlayer wrapped the decoder-owned NV12/P010 texture array
directly.
The texture is commonly larger than the active frame. Although the libplacebo
frame crop describes the active rectangle, libplacebo documents that scaler
filters may sample outside that crop. The resulting edge texels can therefore
contain undefined decoder padding. Their color changes with the surface's
contents and is easiest to see when the video is enlarged.

The minimal reliable correction is the conventional Windows one-copy path:
copy the visible, chroma-aligned rectangle into one cached, exact-size
same-device D3D11 texture and let libplacebo sample that texture. Hardware
decoding remains enabled; the transfer stays on the GPU; there is no second
decode, CPU transfer, color conversion, tone mapper, or synchronization wait.

## Evidence

The affected set includes 3840x1608, 3840x1606-visible/1608-coded, and
3840x2160 HEVC Main 10 titles with several Dolby Vision profiles. Other Dolby
Vision titles did not visibly reproduce it. This rules out a useful
Dolby-specific discriminator.

Two 3840x2160 desktop captures place the corruption in the final active video
rows. In the Deadpool capture, the last active row averages approximately
`RGB(0, 210, 0)`; the corresponding Arrival row is much weaker and greener.
The following row is the intended black letterbox. Scene-dependent color and
severity are consistent with uninitialized or decoder-owned chroma padding.

At 00:10:00 in the affected Deadpool file, FFmpeg software decode and D3D11VA
decode followed by `hwdownload` produced byte-identical hashes for the bottom
eight active rows:

```text
software:  1e1179627070a1436643c94f8789ce25
D3D11VA:   1e1179627070a1436643c94f8789ce25
```

The decoder therefore produced the correct active pixels. The corruption is
introduced after decode, at SunPlayer's direct texture-sampling boundary.

This matches an independently documented D3D11VA failure mode. mpv defaults to
an exact-size one-copy path and makes zero-copy opt-in; its implementation notes
that video decoder textures may contain padding. libplacebo 7.360.1 separately
states that scaler filters may sample outside a logical frame crop.

## Why tests missed it

The existing Windows integration test uses a 640x360 8-bit NV12/H.264 fixture,
asserts the direct-import/zero-copy diagnostic, and compares only six interior
pixels. It never checks the last row or right edge. P010/P012/P016 capture and a
hardware-versus-software edge differential were explicitly deferred. The test
therefore proved that ordinary interior NV12 samples worked while encoding the
unsafe implementation choice as expected behavior.

## Implementation and acceptance checklist

* Cache one shader-readable NV12/P010/P012/P016 texture per active aligned
  extent and resource format.
* Copy from the visible crop origin on the shared immediate context, normalize
  the copied frame's crop, and reject a non-chroma-aligned origin or odd visible
  extent for software retry rather than copying another padding edge.
* Wrap the exact-size copy's luma and chroma planes, preserving the original
  visible geometry, color metadata, and Dolby mapping decision.
* Report `SameDeviceGpuCopy`, zero CPU transfers, and one known GPU copy.
* Replace the interior-only Windows oracle with 2× hardware/software comparisons
  that include all four edges, especially the final luma and chroma rows, plus
  an explicit nonzero crop and odd-extent rejection.
* Validate affected P010 files manually and retain an ordinary NV12 regression.

Manual playback of affected production content after the repair no longer
showed the scene-dependent bottom-edge line. The deterministic NV12/P010
hardware-versus-software regressions pass with D3D11VA required. The complete
first-frame suite passes on Windows with 18 passing tests and only four
expected macOS-specific skips.

## Non-goals

Do not hide or crop the last row, clear decoder padding, change chroma siting,
add a Dolby Vision branch, or modify HDR luminance/gamut policy. Do not add a
user-facing zero-copy option in this repair. Hardware decode and an experimental
zero-copy toggle may be exposed later as advanced options after the safe path is
the established default.

## Primary references

* [libplacebo 7.360.1 `pl_frame.crop` contract](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/renderer.h#L587-L594)
* [mpv D3D11VA importer: default one-copy and opt-in zero-copy](https://github.com/mpv-player/mpv/blob/master/video/out/d3d11/hwdec_d3d11va.c)
* [mpv issue 10719: green bottom edge under D3D11VA zero-copy](https://github.com/mpv-player/mpv/issues/10719)
* [Microsoft `CopySubresourceRegion1` contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-copysubresourceregion1)
