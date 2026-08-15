# 0005: Retain FFmpeg frames at the decoded-frame boundary

* Status: Accepted
* Date: 2026-07-29
* Amended by:
  [0012: Use final decoded frames as source-color truth](0012-use-final-decoded-frames-as-color-evidence.md)

## Context

FFmpeg represents software-decoded planes and hardware-decoded surfaces through
the same reference-counted `AVFrame` API. On Windows, a D3D11VA frame identifies
an FFmpeg-owned texture array and slice; on other platforms the native backing
and synchronization model differ.

Copying pixels into a SunPlayer-owned generic framebuffer would make ownership
simple but force a CPU transfer for hardware decoding. Copying native pointers
out of `AVFrame` would lose the references that keep decoder pools, side data,
and hardware contexts alive.

Scheduling also needs stable application values that must not depend on mutable
decoder or stream contexts: session generation, frame identity, time base,
geometry, storage kind, and diagnostic metadata.

## Decision

`DecodedVideoFrame` is an immutable SunPlayer boundary backed by a cloned or
referenced `AVFrame`.

The retained `AVFrame` remains authoritative for:

* Software buffers, strides, and exact pixel format.
* Hardware surfaces, array slices, and `hw_frames_ctx`.
* HDR10+, Dolby Vision, ICC, film-grain, and other side-data payloads.
* The lifetime of every FFmpeg-owned allocation referenced by the frame.

SunPlayer snapshots stable values needed outside FFmpeg:

* Playback generation, decoder revision, and unique frame identifier.
* PTS, duration, and explicit time base.
* Coded size, crop, visible size, sample aspect ratio, and display rotation.
* Software or hardware storage kind and graphics-device generation.
* Human-readable signal diagnostics.

The final FFmpeg-decoded frame is the authoritative source-color boundary.
FFmpeg has already applied its decoder and codec-context propagation rules.
SunPlayer does not maintain a second metadata policy engine or blanket-copy
stream fields. It snapshots and supplies only stream-level HDR10+ side data,
which the pinned FFmpeg version does not otherwise propagate, and only when a
decoded frame has no frame-local value. A published frame therefore does not
depend on a live `AVStream`, `AVFormatContext`, or `AVCodecContext`; ADR 0012
defines this deliberately narrow exception.

Importers borrow the retained storage. Software frames use libplacebo's FFmpeg
mapping helper and reusable upload textures. Native importers wrap or copy a
hardware surface through a backend-specific implementation. Import results
report CPU downloads/uploads, GPU copies, synchronization, and failure reason.
Fallback policy remains above the importer; an importer must not silently
download a hardware frame.

## Consequences

* Decoder pools cannot reuse a hardware surface while a published/imported
  frame still retains it.
* Software and hardware frames share scheduling and metadata semantics without
  pretending their storage and synchronization are identical.
* Target-only rerenders can reuse one imported source frame.
* Queues must remain bounded because retained frames reserve software memory or
  decoder-pool surfaces.
* A hardware frame from an old graphics-device generation must be rejected.
* Backend synchronization must prove when GPU reads have been ordered before a
  retained hardware frame is released.
* Device recreation supersedes in-flight old-generation decoding and
  hardware-backed frames, then re-decodes after the replacement capability is
  available. Ready software frames remain valid across device generations.
* A native import rejection is a typed presentation failure; playback may
  consume one retry with software decoding without teaching the importer
  session policy. Repeated typed failures become session errors.

The implementation exercises software `AVFrame` retention/upload and Windows
D3D11VA retention/direct import. A pinned H.264 scenario proves that a retained
NV12 texture-array slice can be rendered after decoder teardown without an
input CPU transfer or GPU copy. The session tests cover typed software
re-decode after import rejection and generation replacement after graphics
invalidation. Other platform importers and non-direct GPU/CPU copy fallbacks
remain future implementations.
