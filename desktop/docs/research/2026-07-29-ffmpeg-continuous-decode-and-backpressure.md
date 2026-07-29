# FFmpeg continuous decode and backpressure

* Date: 2026-07-29
* Scope: FFmpeg 8.1, local selected-video playback, software and D3D11VA
* Resulting decision: [ADR 0007](../decisions/0007-bound-continuous-video-and-select-on-presentation-thread.md)

## Question

How should the first-frame proof evolve into continuous playback without
dropping packets on `EAGAIN`, losing delayed B-frames, exhausting hardware
surfaces, blocking cancellation, or creating an unbounded Qt event queue?

## Primary API findings

FFmpeg's send/receive contract is a strict state machine:

* A successful `avcodec_send_packet()` consumes the complete packet.
* Send-side `AVERROR(EAGAIN)` means output must be received and the exact
  packet retained for retry.
* `avcodec_receive_frame()` runs until `EAGAIN` requests more input.
* Send and receive cannot both return `EAGAIN` without progress.
* At demux EOF, one null packet enters drain mode and receive continues until
  `AVERROR_EOF`; `EAGAIN` after a successful flush is not normal completion.

Sources:

* [FFmpeg 8 send/receive overview](https://ffmpeg.org/doxygen/8.0/avcodec_8h_source.html)
* [FFmpeg decoding API](https://ffmpeg.org/doxygen/8.0/group__lavc__decoding.html)
* [FFplay decoder pump](https://www.ffmpeg.org/doxygen/8.0/ffplay_8c_source.html#l00571)
* [FFmpeg demux packet API](https://ffmpeg.org/doxygen/8.0/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61)

`AVFrame::best_effort_timestamp` remains heuristic and may be absent,
negative, or repeated; duration zero remains unknown. Timestamp and time base
should stay integer until one controlled `av_rescale_q` conversion. Frame
identity remains independent of PTS.

Sources:

* [AVFrame timing fields](https://ffmpeg.org/doxygen/8.0/frame_8h_source.html#l00515)
* [FFmpeg packet rescaling](https://ffmpeg.org/doxygen/8.0/group__lavc__packet.html)
* [FFplay frame-duration policy](https://www.ffmpeg.org/doxygen/8.0/ffplay_8c_source.html#l01572)

`av_frame_clone()` retains every `AVBufferRef`. For D3D11VA this pins the
texture-array surface and slice until the last retained frame reference is
released. FFmpeg's default hardware-frame parameter sizing includes only one
caller-retainable frame, so application queue/current/in-flight retention must
shape `extra_hw_frames`.

Sources:

* [AVFrame reference/clone API](https://ffmpeg.org/doxygen/8.0/frame_8h_source.html#l00795)
* [AVHWFramesContext](https://ffmpeg.org/doxygen/8.0/structAVHWFramesContext.html)
* [D3D11VA frame mapping](https://ffmpeg.org/doxygen/8.0/hwcontext__d3d11va_8c_source.html#l00348)
* [Hardware frame parameter sizing](https://ffmpeg.org/doxygen/8.0/group__lavc__decoding.html#ga2b1aef8ff4155feab6e2d1b09de88751)

The `AVIOInterruptCB` state must outlive `AVFormatContext` teardown. Stop must
also wake every packet/frame wait; the callback alone cannot cancel a thread
blocked on an application queue.

Source:

* [AVFormatContext interrupt callback](https://www.ffmpeg.org/doxygen/8.0/structAVFormatContext.html)

## Queue findings

* Packet buffering should have an aggregate byte bound as well as a count
  bound. Permit one oversize packet only when otherwise empty.
* End of stream is durable control state ordered after existing packets, not a
  capacity-consuming packet that can be lost when full.
* Decoded video should use a small hard frame count. FFplay uses three video
  pictures and releases a frame before signaling capacity.
* Backpressure should propagate presentation → frames → decoder → packets →
  demux.
* No FFmpeg call, D3D execution scope, Qt notification, or thread join should
  occur while holding a queue mutex.
* A frames-available Qt notification should be coalesced; actual retained
  frames remain in the bounded mailbox.

Source:

* [FFplay packet/frame queues and read throttling](https://www.ffmpeg.org/doxygen/8.0/ffplay_8c_source.html)

## Implementation consequences

Sunroom uses one demux owner, one decoder owner, a selected-video channel
bounded to 64 packets/four MiB, and a three-frame decoded mailbox. The
continuous decoder is also the implementation behind the focused first-frame
adapter. The registered hardware test decodes and retains all three H.264
D3D11VA frames, while focused queue tests prove backpressure and stop/
generation wakeups.

This slice intentionally does not claim position-preserving decoder recovery.
That needs a seek to a preceding keyframe followed by decode-to-anchor.
