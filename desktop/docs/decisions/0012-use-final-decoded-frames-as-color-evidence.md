# 0012: Use final decoded frames as source-color truth

* Status: Accepted
* Date: 2026-08-01
* Amends:
  [0005: Retain FFmpeg frames at the decoded-frame boundary](0005-retain-ffmpeg-frames-at-the-decoded-frame-boundary.md)

## Context

Rendering needs one stable boundary describing what decoded pixels mean. FFmpeg
can obtain color fields and coded side data from the bitstream, codec
parameters, or codec context, and its decode core fills unspecified frame
fields from that context. At Sunroom's returned-frame boundary, a populated
field cannot always be attributed more narrowly than “final FFmpeg-decoded
value.”

Libplacebo also offers `pl_frame_copy_stream_props`, but using it as a blanket
merge would duplicate FFmpeg propagation and obscure which object rendering
actually consumes.

## Decision

The retained final `AVFrame` is the authoritative input for pixel storage,
scalar color fields, and side data. The existing `VideoSignalDescription`
remains a small display-only snapshot of names and component depth. Dynamic
metadata diagnostics inspect the retained frame and libplacebo's mapped frame
when needed; Sunroom does not build a parallel metadata model. FFmpeg and
libplacebo remain responsible for interpretation.

Timing, frame identity, storage, and geometry remain in their existing
`DecodedVideoFrame` components. The color description references those facts
when a policy needs them but does not become a second frame descriptor.

Diagnostics label an ordinary populated field simply as a final
FFmpeg-decoded value. Sunroom does not try to reconstruct whether the decoder
or FFmpeg's context fallback originally supplied it.

Stream state may be frozen when FFmpeg demonstrably does not propagate a
required property. The initial exception is global HDR10+ metadata: if the
decoded frame lacks it, the packet decoder attaches that one stream payload
before retaining the frame for libplacebo. There is no blanket stream copy and
no call to `pl_frame_copy_stream_props`.

Embedded source ICC bytes remain owned by the retained frame. Import
diagnostics report presence and size; the LCMS-disabled build neither validates
nor applies them. Profile parsing and application-managed ICC rendering are
deferred until a real product path requires them.

## Consequences

* The retained `AVFrame` stays the single rendering truth; diagnostics do not
  duplicate libplacebo policy.
* The one global HDR10+ fallback is attached to each decoded frame only when
  frame metadata is absent, before that frame is retained.
* Dynamic-metadata diagnostics are best-effort and may become current on a
  later frame; atomic diagnostic perfection is not a rendering requirement.
* Existing frame retention and zero-copy storage ownership remain unchanged.
* New metadata handling requires a concrete format fixture or observed
  library propagation gap rather than speculative completeness.
* Removing redundant stream-to-frame mutation can change diagnostics for
  media whose only usable metadata was not propagated by FFmpeg; such cases
  require a targeted, evidenced supporting-field rule rather than restoring a
  blanket copy.

## Alternatives considered

### Copy all stream metadata onto every frame

Rejected. FFmpeg already handles ordinary propagation, and duplicating it
creates more policy and stale-state risk than value. A narrow, evidenced copy
onto the current decoded frame before retention is acceptable.

### Replace library inference with a Sunroom metadata policy engine

Rejected. Product-significant fallback should be diagnosed, but duplicating
FFmpeg/libplacebo interpretation would be more complex and less trustworthy.

### Implement a complete metadata framework before the first fixture

Rejected. The ownership boundary is fixed now; special handling should be
added only for a concrete library gap backed by a regression scenario.
