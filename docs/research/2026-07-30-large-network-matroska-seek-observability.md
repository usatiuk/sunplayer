# Large network Matroska seek investigation

* Date: 2026-07-30
* Status: Complete

## Report

One approximately 520 MB 1080p HEVC Matroska file seeks successfully from an
SMB share. Two approximately 17 GB and 21 GB UHD HEVC Dolby Vision/HDR
Matroska files remain in the Seeking state while the decoded-frame counter,
GPU decode load, and network reads continue.

The same files seek normally in established players. They are treated as valid
inputs and useful compatibility cases, not blamed as malformed media.

## Read-only probes

The three selected streams all use a `1/1000` time base and start at zero, so
the retained-origin conversion does not explain the difference.

At representative targets, FFmpeg's packet probe reached these preceding
keyframes:

| Case | Target | First keyframe | Byte position | Probe time |
| --- | ---: | ---: | ---: | ---: |
| Working 1080p | 600 s | 596.262 s | 262,175,665 | 0.07 s |
| UHD case A | 3000 s | 2991.071 s | 7,304,736,492 | 0.19 s |
| UHD case B | 3000 s | 2992.792 s | 10,126,384,453 | 0.18 s |

The UHD anchors are only about seven to nine seconds before the target and
their byte positions are consistent with the requested portion of each file.
Decoding a bounded 300-packet window produced monotonic decoded timestamps
from those anchors through and beyond the target. Software decode took about
10–11 seconds for each UHD window, which is expected to be materially slower
than the application's D3D11VA path but confirms usable timestamps.

## Pinned FFmpeg seek semantics

Sunroom's current `avformat_seek_file` request uses the selected video stream's
time base and requests the closest dependency-safe point at or before the
target. In FFmpeg 8.1.2, adding `AVSEEK_FLAG_BACKWARD` would not change this
call; `avformat_seek_file` ignores that flag. `AVSEEK_FLAG_ANY`, byte seeking,
or frame seeking would weaken or change the contract and are not appropriate
fixes for accurate inter-frame video seeking.

Matroska uses FFmpeg's older seek hook, so the lower bound of a seek range is
not a reliable hard preroll limit. That matters for robust product policy, but
the measured files do provide nearby usable cue/keyframe anchors.

## Root cause

The evidence ruled out these primary causes for the reported files:

* Unseekable SMB input.
* Missing or grossly sparse Matroska cues.
* A seconds/milliseconds/stream-time-base error in the requested target.
* A genuinely file-length dependency chain.
* An old playback generation incrementing the current frame counter.

The visible decoded counter increments before normalized seek-preroll
admission. Rejected preroll frames never reach the frame mailbox or libplacebo.
Therefore high GPU use during this state is most likely decoder work, not
rendering each rejected frame.

The first production debug trace exposed the divergent boundary:

```text
requested target = 3,000,000,000 microseconds
computed stream target = 0
post-seek byte position = beginning of file
first selected packet PTS = 0
```

Sunroom used `av_add_stable()` to add the normalized microsecond offset to the
timeline origin. That function is designed for stable repeated increments. In
FFmpeg 8.1.2 it multiplies the increment into an `AVRational`; the rational's
numerator is a 32-bit `int`. A microsecond position after
`INT32_MAX` microseconds (about 35:47.483) therefore narrows before the final
rescale. The representative 3000-second request collapsed to zero.

The shorter working file is about 1333 seconds, so every possible seek remains
below that boundary. The UHD/HDR properties were correlated but irrelevant;
long playback position was the differentiator.

## Fix

Convert the absolute normalized position once with `av_rescale_q()` and add it
to the rescaled origin with checked 64-bit arithmetic.

`av_add_stable()` remains appropriate for accumulated clock-step calculations;
it is not the correct API for one arbitrarily large absolute position.

## Regression and production validation

A pinned Matroska fixture contains only two FFV1 intra frames, at zero and
3000 seconds. The real FFmpeg decode test seeks to 3000 seconds and requires
that the first decoded frame is the second frame. This crosses the original
32-bit-microsecond boundary without a large or slow fixture.

The regression passed with the fixed production call. Restoring only the old
`av_add_stable()` call made it fail as intended:

```text
actual first sought PTS: 0
expected first sought PTS: 3,000,000,000 microseconds
```

After restoring the fix, both reported real files were exercised through
Sunroom's D3D11VA application pipeline at 3000 seconds:

| Case | Stream target | First packet | Preroll | Completion |
| --- | ---: | ---: | ---: | ---: |
| UHD case A | 3,000,000 | 2991.071 s | 215 frames | 1.66 s |
| UHD case B | 3,000,000 | 2992.792 s | 174 frames | 1.24 s |

Both reached their expected multi-gigabyte offsets and admitted the requested
frame. Real SMB/UHD cases remain valuable opt-in compatibility coverage; the
small pinned fixture is the per-change regression.
