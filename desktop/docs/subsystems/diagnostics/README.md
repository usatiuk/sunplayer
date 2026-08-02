# Diagnostics and observability

## Status

Sunroom uses Qt logging categories and writes a bounded per-session application
log by default. The initial instrumentation covers application lifetime and
the open, probe, seek, demux, decode, normalized-preroll, and completion
boundaries. Existing graphics, video, presentation, and platform messages use
the same shared categories.

Current pipeline properties exposed in the UI remain subsystem-owned
diagnostic snapshots. The audio boundary now contributes a common low-rate
snapshot for backend/device/format, PCM occupancy, submitted/presented frames,
latency and device position when available, underruns, device revision,
audio-output epoch, and clock reliability; `MediaSession` adds the active clock
source. On Linux this same snapshot exposes the backend selected by system
cubeb; WSLg reports `pulse`. There is not yet one exportable support report,
performance trace, or A/V synchronization
report. Playback logs transitions into and out of its initial audio
`Buffering` state and reports sustained clock loss as a structured session
failure, but physical device-replacement transactions are not implemented yet.
The session file starts after `QGuiApplication` construction, so failures while
Qt is locating or initializing its platform plugin remain outside this sink.
The packaged-QML startup test catches the common deployment failure, but it is
not a substitute for pre-Qt bootstrap diagnostics.

The accepted logging policy is recorded in
[ADR 0010](../../decisions/0010-qt-category-logging-and-bounded-session-files.md).

## Logging contract

Use Qt's logger directly:

```cpp
qCInfo(sunroomLogPlayback)
    << "event=playback.seek_start"
    << "generation=" + QString::number(generation)
    << "positionUs=" + QString::number(position);
```

Category names follow subsystem ownership:

```text
sunroom.application
sunroom.media.io
sunroom.media.decode
sunroom.playback
sunroom.graphics
sunroom.video
sunroom.presentation
sunroom.platform
```

Add a narrower category only when it needs an independently useful runtime
filter. Do not encode severity, class names, or implementation layers into the
category hierarchy.

Event records should:

* Lead with a stable `event=<domain.action>` field.
* Carry the relevant generation or operation identity.
* State units in field names, such as `positionUs`, `elapsedMs`, or
  `bytesRead`.
* Record effective decisions and outcomes, not private method-call sequences.
* Use periodic progress or aggregate counts across hot paths.
* Avoid full media paths at normal info level.

Info records describe lifecycle milestones and outcomes. Debug records explain
how an outcome was reached. Warnings describe recoverable unexpected states.
Critical records describe failures that prevent the requested operation or
application from continuing.

## Session files and configuration

By default, each process writes an info-level session file named like:

```text
<Qt temporary location>/Sunroom/logs/sunroom-<UTC timestamp>-<pid>.log
```

The file is capped at 8 MiB and at most ten automatic session files are
retained. When the cap is reached, the sink records one truncation marker when
space permits and stops writing. A custom `--log-file` remains capped but never
prunes sibling files. On Windows, UNC paths and mapped drive-letter roots are
rejected without resolving the requested file path. A reparse point beneath a
local root can still redirect elsewhere and is not followed during this
preflight. Console or debugger delivery continues.

The Qt message handler does not perform file I/O. It enqueues records into a
256 KiB, 1024-record buffer and immediately resumes the calling thread. One
sink worker owns the file and drains the queue. When producers outrun it,
records are dropped and summarized by `event=log.records_dropped`; media,
presentation, and cancellation threads never wait for the file sink. Explicit
`flush()` calls coalesce on a sequence watermark, and final application
shutdown drains the accepted queue. A fatal record can evict ordinary queued
records so its bounded prefix survives saturation; the hard file-size cap
still takes precedence.

Runtime controls:

```text
sunroom --debug-log
sunroom --log-file <path>
sunroom --no-log-file
```

`--debug-log` enables `sunroom.*.debug` records. Qt's standard
`QT_LOGGING_RULES` remains authoritative for additional category filtering,
and every record it admits reaches the session file as well as the previous
handler. `QT_MESSAGE_PATTERN` continues to control console formatting.

## Current seek trace

A generation-scoped seek can now establish:

```text
requested normalized position
→ retained origin and stream time base
→ FFmpeg target timestamp
→ post-seek byte position and bytes read
→ first selected packet PTS/DTS/key flag
→ first decoded raw PTS
→ first normalized timeline position
→ bounded periodic preroll progress
→ first admitted frame or intentional end fallback and elapsed time
→ completion, cancellation, or failure
```

This separation is intentional. A valid demux seek can still be followed by a
decoder timestamp or normalization problem; a rising decoded count does not
mean frames reached libplacebo or presentation.

## Diagnostics snapshots

Snapshots should remain typed subsystem state rather than parsed log messages.
The long-term support snapshot should compose:

* Source/container, selected streams, retained frame signal, and timeline.
* Decoder backend, frame storage, adapter/device generation, and fallbacks.
* Queue depths, clock source, selected/dropped frames, and underruns.
* libplacebo backend, input and output transfer/copy paths, and target state.
* Presentation backend, semantic display target, output format, and recovery.
* Relevant operation progress and last structured failure.

Color diagnostics inspect the retained final FFmpeg frame and, when available,
libplacebo's mapped result. They report best-effort signal names, dynamic
metadata path, and source ICC presence/size/application status without
reconstructing a second metadata policy. Presentation state distinguishes
`SystemManaged` from `UnmanagedSrgb` and records the surface encoding,
target-gamut source, reference white, selected usable peak, last update reason,
and last invalidation reason. Source ICC application remains false because the
render-local libplacebo frame explicitly suppresses both ICC representations;
diagnostics must not infer application from the linked library's LCMS feature.

Snapshots publish at bounded/coalesced points. Worker threads must not drive
the UI or allocate a new object for every frame.

## Privacy and support export

Normal info logging should be useful without source paths or content-derived
metadata that is unrelated to a failure. Opt-in debug logging may contain
local paths, adapter names, and detailed media metadata.

A future support bundle should be a user-initiated action that previews:

* Session log.
* Current diagnostics snapshot.
* Application and dependency versions.
* Relevant platform/backend capabilities.

It must not include media data, credentials, or automatic uploads.

## Verification

The file sink has focused tests for formatting, its hard size bound, queue
overflow, concurrent flush coalescing, fatal-record preservation, custom-path
isolation, and Windows local/remote path policy.
Subsystem tests should assert typed decisions and behavioral outcomes; they
should not parse prose logs as their correctness oracle.

Scenario tests may capture logs to improve failure reports. A regression test
that requires a particular event should usually also have a typed observation
or invariant so renaming a diagnostic field does not change application
behavior.
