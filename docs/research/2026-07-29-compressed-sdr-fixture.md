# Deterministic compressed SDR fixture

* Date: 2026-07-29
* Status: Generated and locally verified

## Purpose

The original 4×4 PPM fixture proves FFmpeg/libplacebo/QRhi integration but does
not exercise a compressed container, YUV planes, limited range, chroma
subsampling, timestamps, or non-square pixels.

The next fixture is analytically generated and contains no external media:

* Matroska with native lossless FFV1.
* Three 96×64 `yuv420p` frames at 4 fps.
* Sample aspect ratio 32:27, producing a 16:9 display aspect ratio.
* BT.709 primaries, transfer, and matrix.
* Limited/TV range and left chroma location.
* Flat black, gray, white, red, green, and blue tiles with known YUV codes.

FFV1 and Matroska are available in Sunroom's existing pinned FFmpeg libraries,
so the fixture adds no runtime or build dependency. It is software-only and
does not prove D3D11VA.

## Generation evidence

The fixture manifest records the complete command and
`FFmpeg 8.1.2-full_build-www.gyan.dev`. Two independent invocations produced
the same SHA-256:

```text
5bf94e269b1a9543f34bb6f132373e1e6924121f229ef224fca378a45109d035
```

`ffprobe` reported FFV1, `yuv420p`, 96×64, SAR 32:27, 4/1 fps, BT.709
primaries/transfer/matrix, TV range, left chroma location, first PTS zero, and
first-frame duration 250 in a 1/1000 time base.

Tests consume the committed binary and verify its manifest hash. Regeneration
is a maintenance action, not part of configure, build, or test.

## Oracle

Decoded Y, U, and V samples at flat-tile centers are exact because FFV1 is
lossless. Rendered linear RGB is compared against independently calculated
limited-range BT.709 values with a 0.02 absolute per-channel tolerance.
For example, the gray tile's normalized luma is `(126 - 16) / 219`; the
BT.709 display interpretation used by FFmpeg/libplacebo follows the
BT.1886-style 2.4 power function, producing approximately `0.19165` linear.
This must not be confused with inverting the BT.709 camera OETF. Sampling
avoids chroma edges.

The later hardware-decode slice needs a separate H.264 or HEVC fixture. That
fixture must be selected deliberately because the current dependency has no
deterministic native H.264 encoder and hardware/driver output requires
tolerance-based comparison.
