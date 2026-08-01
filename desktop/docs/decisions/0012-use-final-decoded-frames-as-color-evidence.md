# 0012: Use final decoded frames as effective source-color evidence

* Status: Accepted
* Date: 2026-08-01
* Amends:
  [0005: Retain FFmpeg frames at the decoded-frame boundary](0005-retain-ffmpeg-frames-at-the-decoded-frame-boundary.md)

## Context

Rendering needs one immutable description of what decoded pixels mean. FFmpeg
can obtain color fields and coded side data from the bitstream, codec
parameters, or codec context, and its decode core fills unspecified frame
fields from that context. At Sunroom's returned-frame boundary, a populated
field cannot always be attributed more narrowly than “final FFmpeg-decoded
value.”

Sunroom currently copies selected stream metadata onto its private retained
frame. Libplacebo also offers `pl_frame_copy_stream_props`. Neither operation
defines conflict precedence, confidence, or fallback policy, and a late copy
can obscure valid frame evidence.

## Decision

The retained final `AVFrame` is the authoritative evidence boundary for pixel
storage, scalar color fields, and per-frame/coded side data. Sunroom will
derive one immutable `EffectiveSourceDescription` from it.

The description will contain typed values for representation, range, matrix,
transfer, signal primaries, target/mastering metadata, chroma location, alpha,
and relevant dynamic metadata. Each effective field records honest
provenance, confidence, and any fallback or contradiction. Unknown remains
unknown unless an explicit, documented policy supplies a fallback.

Timing, frame identity, storage, and geometry remain in their existing
`DecodedVideoFrame` components. The color description references those facts
when a policy needs them but does not become a second frame descriptor.

Provenance for an ordinary populated final-frame field is “final
FFmpeg-decoded value, possibly context-propagated.” Sunroom will not claim to
distinguish a decoder-provided value from FFmpeg's context fallback where the
public result no longer carries that distinction.

Stream state may be frozen as supporting evidence when FFmpeg does not
propagate a required property, but it is not copied wholesale onto the
retained frame. `pl_frame_copy_stream_props` is not called after policy
resolution. Importers map storage, then apply the already-resolved source
description to the libplacebo frame.

For non-transforming ICC retention, current-frame ICC side data is primary
evidence. A snapshotted stream ICC may always be retained as supporting
evidence, but it is a fallback candidate only when the final frame has no ICC
payload and never overwrites the frame. If both are present and differ,
diagnostics retain both hashes and report the contradiction. No profile becomes
applicable until an actual validator accepts it under a later source-ICC
decision.

Embedded source ICC bytes are retained and described by presence, size, hash,
provenance, and validation/application status. Their current status is
`UnvalidatedUnsupported`: the LCMS-disabled build neither semantically
validates nor applies them. Enabling source-ICC rendering requires reviewed
LCMS packaging, profile validation, a coherent ICC-versus-scalar policy, and
explicit HDR-plus-ICC tests. Initial support should be limited to validated
SDR RGB profiles because libplacebo's ICC path replaces the frame's complete
HDR metadata rather than combining mastering or dynamic HDR metadata
afterward.

## Consequences

* One immutable object becomes the source-color truth used by rendering,
  caching, and diagnostics.
* Dynamic metadata cannot leak forward from an earlier frame.
* Fallbacks and contradictions become observable instead of being hidden in a
  frame mutation or library inference.
* Existing frame retention and zero-copy storage ownership remain unchanged.
* The initial implementation may use a smaller set of typed fields driven by
  the real static-PQ fixture, but it must preserve the seam and unknown states.
* Removing redundant stream-to-frame mutation can change diagnostics for
  media whose only usable metadata was not propagated by FFmpeg; such cases
  require a targeted, evidenced supporting-field rule rather than restoring a
  blanket copy.

## Alternatives considered

### Keep mutating the retained frame

Rejected. It erases evidence boundaries and makes ordering determine the
result.

### Let libplacebo infer every unspecified value silently

Rejected. Some inference is useful, but product-visible fallback choices must
be explicit and diagnosable. In particular, libplacebo can infer a YCbCr
system from dimensions when the matrix is unknown.

### Implement a complete metadata framework before the first fixture

Rejected. The boundary and provenance rules are fixed now; fields should be
added with concrete format support and regression scenarios.
