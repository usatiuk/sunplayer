# Playback subsystem

## Status

No playback session, scheduler, queues, seek implementation, or audio clock
exists yet. The decoded-frame boundary now carries the identity and timing
values those components require. This document records the intended clock and
recovery contract so the first scheduler does not accidentally hard-code a
wall-clock-only model.

## Clock ownership

The playback core owns the media timeline. Audio and platform backends report
timestamped observations; they do not mutate session position directly.

During ordinary playback with an audio track, the estimated position already
rendered by the audio device is the master observation. It combines submitted
sample counts, device/backend presentation position, output latency, monotonic
sample time, playback rate, and the current seek/pause anchor.

Conceptually:

```text
audio presentation observation
        ↓
media-clock snapshot
        ↓
video scheduler compares normalized frame PTS
        ↓
wait / present / drop / discard stale generation
```

Muted playback keeps the audio stream advancing at zero gain when practical,
preserving the same clock. Media without an audio track uses a monotonic-clock
master.

Small drift may be corrected gradually. Seeking, suspend/resume, device
replacement, or another large discontinuity re-anchors the clock explicitly
rather than hiding the jump through prolonged drift correction.

## Audio-device recovery

User intent and temporary ability to advance are distinct state:

* User pause freezes the clock until the user resumes.
* Audio-device loss enters `RecoveringAudio`, not `Paused`.
* Video presentation and media-clock advancement stop while the required audio
  stream is recreated.
* The new stream receives bounded preroll, reports a trustworthy position and
  latency, and re-anchors the media clock.
* Playback resumes automatically only if user intent still says playing.

This policy covers default-device changes and Bluetooth disconnect/reconnect
without allowing video to run silently ahead or presenting old queued audio
after recovery.

A short audio underrun writes silence and lowers clock confidence. A sustained
underrun participates in the unified buffering state and freezes progression.
The real-time audio callback only reads prepared PCM, applies trivial gain,
writes silence, and updates atomic counters; it never decodes, allocates,
seeks, logs synchronously, or calls into Qt.

## Frame scheduling

Every decoded frame has a playback generation independent of its PTS. PTS may
be missing or repeated and is not frame identity.

The video scheduler uses predicted presentation time:

* Early frame: retain.
* On-time frame: present.
* Slightly late frame: present immediately.
* Materially late frame: drop and report.
* Old generation: discard regardless of timestamp.

Thresholds remain policy to validate against refresh cadence, renderer latency,
and hardware. They should not be fixed from intuition before continuous
playback measurements exist.

## Verification direction

Focused tests will use a controlled monotonic clock and audio-sink observation
edge while retaining real queues and scheduling. Required cases include
pause/resume, underrun, latency changes, device replacement, seek generation,
missing/repeated timestamps, and large discontinuities.

Later physical verification uses synchronized audio impulses and visual flashes
to measure actual speaker-to-display output timing. Software timestamps alone
cannot prove Bluetooth, operating-system, display, and acoustic latency.
