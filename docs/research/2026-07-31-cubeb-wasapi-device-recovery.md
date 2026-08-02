# Cubeb WASAPI device-recovery semantics

> Historical result: the pinned-source findings remain valid, but the
> fail-closed product policy is superseded by
> [the 2026-08-01 reconciliation](2026-08-01-display-audio-migration-reconciliation.md)
> and [ADR 0016](../decisions/0016-reconcile-output-changes-semantically.md).
> Sunroom now accepts cubeb-stream-level logical continuity for ordinary
> default-route migration instead of requiring a physical-endpoint epoch for
> every hidden WASAPI client.

## Question

Can Sunroom treat cubeb's automatic default-device migration as a trustworthy
audio-output epoch, or must playback own device replacement and clock
re-anchoring?

This investigation covers the project-pinned cubeb commit
`ef47ae581df7c2f76058d554b3edde17f9ee7cba` on Windows. The project overlay
corrects install metadata and carries one narrow WASAPI runtime patch described
below.

The conclusions were cross-checked against Microsoft's WASAPI recovery and
clock contracts and upstream cubeb. On 2026-07-31, upstream `master` resolved
to the same immutable commit Sunroom pins, so the source findings below do not
mix project behavior with a moving newer revision.

## Findings

Sunroom now enumerates the enabled multimedia default, passes its explicit
`cubeb_devid` to `cubeb_stream_init`, and requests
`CUBEB_STREAM_PREF_DISABLE_DEVICE_SWITCHING`. Cubeb's public contract says an
explicit device stays on that device, whereas a null device may follow the OS
default. Both choices prevent migration to a different physical endpoint under
the old epoch identity. The project overlay additionally makes every backend
reconfigure event fail when switching is disabled, so a same-device WASAPI
client replacement cannot silently preserve the old epoch either.

The WASAPI backend does not implement
`cubeb_stream_register_device_changed_callback`. Sunroom's available context
collection callback is not stream-specific: it may report an unrelated output
device addition, removal, state change, or default change. It is evidence to
re-enumerate devices, not proof that the active stream changed.

With a null device and switching enabled, cubeb can react to default changes,
session disconnection, and `AUDCLNT_E_DEVICE_INVALIDATED` by asynchronously
stopping, closing, reopening, and starting the WASAPI client. A successful
internal reconfiguration has no distinct success callback. The pinned backend
also routes session-disconnect notifications through its reconfigure event
even when default switching is disabled, which is why the preference alone is
insufficient. Supplying an explicit device prevents that path from selecting a
different endpoint. Sunroom's overlay patch closes the remaining same-endpoint
hole in the render loop: when either stream direction disables switching, a
reconfigure event exits with `CUBEB_STATE_ERROR` before closing or reopening
the WASAPI clients. Presentation progress still needs observation for stalls
which do not produce a reconfigure event.

Cubeb's WASAPI stream position is logical and monotonic within one cubeb
stream. During migration it rolls all frames written to the old endpoint into
the accumulated position, including an outstanding tail that the old device
may never present. The position can therefore advance across discarded audio.
Monotonicity is not an acoustic-continuity guarantee.

A newly created cubeb stream starts its device position at zero. Sunroom's
current sink reset also clears its PCM queue and output ledger. Replacing the
device therefore creates a new raw output-clock epoch, even when the playback
generation and media timeline remain intact.

Microsoft's recovery guidance requires releasing the invalidated WASAPI client
and activating a client on the current endpoint; it does not require reopening
the media source. A new `IAudioClock` begins at zero and is monotonic within
that stream. Default-device and session-disconnect notifications are
asynchronous and may arrive in different orders, so they are invalidation
facts rather than a ready-to-resume signal.

Relevant pinned source locations:

* [`include/cubeb/cubeb.h`](https://github.com/mozilla/cubeb/blob/ef47ae581df7c2f76058d554b3edde17f9ee7cba/include/cubeb/cubeb.h):
  null versus explicit device semantics.
* [`src/cubeb.c`](https://github.com/mozilla/cubeb/blob/ef47ae581df7c2f76058d554b3edde17f9ee7cba/src/cubeb.c):
  unsupported stream-device callback wrapper.
* [`src/cubeb_wasapi.cpp`](https://github.com/mozilla/cubeb/blob/ef47ae581df7c2f76058d554b3edde17f9ee7cba/src/cubeb_wasapi.cpp):
  device notification, reconfiguration, position, and default-device
  enumeration behavior.
* `vcpkg-ports/cubeb/fail-disabled-device-reconfigure.patch`: fail-closed
  handling for a disabled-switching reconfigure event.
* `src/audio/CubebAudioSink.cpp`: Sunroom's explicit multimedia-device
  selection, collection revision, queue, ledger, and reset behavior.

The extracted dependency source is under the active vcpkg build tree and is
not a repository source of truth. These findings should be rechecked when the
pinned cubeb revision changes.

Primary-source cross-checks:

* [Microsoft: recover from an invalid device](https://learn.microsoft.com/en-us/windows/win32/coreaudio/recovering-from-an-invalid-device-error)
* [Microsoft: relevant stream-routing notifications](https://learn.microsoft.com/en-us/windows/win32/coreaudio/relevant-device-notifications-for-stream-routing)
* [Microsoft: IAudioClock::GetPosition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition)
* [Microsoft: OnDefaultDeviceChanged](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immnotificationclient-ondefaultdevicechanged)

## Consequences

Do not treat a collection revision, `CUBEB_STATE_STARTED`, or monotonic cubeb
position alone as proof of successful device recovery. Do not silently repair
the result with a clock offset. The overlay's fail-closed patch is part of the
reviewed dependency contract and must be revalidated or retired when cubeb is
updated.

The first shared recovery boundary therefore exposes facts rather than
platform policy:

* Output hold-silence is explicit and freezes media time.
* A sustained hold becomes `Buffering`.
* User play intent remains independent from Buffering.
* Sustained loss of an established presentation observation and explicit
  current-generation sink failure remain terminal until output-epoch
  replacement is implemented; neither falls back to a provisional clock.

The next Windows production slice should:

1. Treat the collection callback as a reason to re-enumerate the explicit
   multimedia device. Ignore unrelated
   changes while the selected device remains the enabled multimedia default,
   and deduplicate the per-role notifications Windows may emit.
2. Freeze at the last confident presented media position when that device is
   lost or replaced.
3. Start a new audio-output epoch without reopening, reparsing, or duplicating
   the shared FFmpeg pipeline. Reset device-dependent PCM/resampler state and
   the output ledger while preserving the media timeline and video decoder.
4. Preroll the replacement stream, anchor its first trustworthy presentation
   observation to the frozen media position, and resume only if user play
   intent still requests it.
5. Bound retries and expose failure rather than repeatedly reopening a source
   while no device exists.

If buffered PCM cannot be reconciled exactly, the audio worker may discard and
regenerate audio from already-owned packet/timeline state. Replacing the full
playback generation and reopening the media source is a fallback for an
unrecoverable discontinuity, not the normal device-change design. This avoids
rereading network or non-seekable media merely because an output endpoint
changed.
