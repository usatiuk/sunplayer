# 0005: Retain FFmpeg frames at the decoded-frame boundary

* Status: Accepted
* Date: 2026-07-29

## Context

FFmpeg represents software-decoded planes and hardware-decoded surfaces through
the same reference-counted `AVFrame` API. On Windows, a D3D11VA frame identifies
an FFmpeg-owned texture array and slice; on other platforms the native backing
and synchronization model differ.

Copying pixels into a Sunroom-owned generic framebuffer would make ownership
simple but force a CPU transfer for hardware decoding. Copying native pointers
out of `AVFrame` would lose the references that keep decoder pools, side data,
and hardware contexts alive.

Scheduling also needs stable application values that must not depend on mutable
decoder or stream contexts: session generation, frame identity, time base,
geometry, storage kind, and diagnostic metadata.

## Decision

`DecodedVideoFrame` is an immutable Sunroom boundary backed by a cloned or
referenced `AVFrame`.

The retained `AVFrame` remains authoritative for:

* Software buffers, strides, and exact pixel format.
* Hardware surfaces, array slices, and `hw_frames_ctx`.
* HDR10+, Dolby Vision, ICC, film-grain, and other side-data payloads.
* The lifetime of every FFmpeg-owned allocation referenced by the frame.

Sunroom snapshots stable values needed outside FFmpeg:

* Playback generation, decoder revision, and unique frame identifier.
* PTS, duration, and explicit time base.
* Coded size, crop, visible size, sample aspect ratio, and display rotation.
* Software or hardware storage kind and graphics-device generation.
* Human-readable signal diagnostics.

Stream-level display matrix, mastering-display, content-light, and HDR10+
properties are copied onto the private retained frame when absent there. A
published frame therefore does not depend on a live `AVStream`,
`AVFormatContext`, or `AVCodecContext`.

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

The initial implementation exercises software `AVFrame` retention and upload.
D3D11VA direct import remains the next backend implementation.
