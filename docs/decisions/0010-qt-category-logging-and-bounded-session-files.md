# 0010: Use Qt category logging with bounded session files

* Status: Accepted
* Date: 2026-07-30

## Context

SunPlayer crosses asynchronous media I/O, demux, decode, scheduling, rendering,
presentation, and platform-event boundaries. UI counters and terminal error
messages do not explain where an operation stalled or which playback
generation produced an observation. The first large-network-file seek
investigation demonstrated that packet position, decoded timestamp, normalized
timeline position, and render activity must be distinguishable.

Qt already provides thread-safe message routing, severity levels, named
categories, runtime filters, and message handlers. A second logging API would
add indirection without adding useful policy.

Always-unbounded debug logging is also unsuitable. Media paths can be private,
per-frame records are expensive, and a player may run for hours.

## Decision

Use `QLoggingCategory` and the `qC*` functions throughout SunPlayer.

The application installs one small process-wide service around
`qInstallMessageHandler`:

* The previous console or debugger destination remains active.
* A per-process session file is written by default beneath Qt's temporary
  directory.
* Info, warning, critical, and fatal records are written normally.
* SunPlayer debug categories are disabled by default and enabled explicitly with
  `--debug-log` or Qt logging rules.
* `--log-file <path>` selects an exact output path and `--no-log-file` disables
  the file sink.
* Every session file has a hard size limit. Only automatically named files in
  the temporary directory participate in bounded retention; a user-selected
  path never causes sibling files to be pruned.
* The Qt handler only formats and enqueues records into a byte-and-count
  bounded buffer. A dedicated sink thread owns all file writes; queue overflow
  produces an aggregate dropped-record marker instead of blocking playback.
  Concurrent flushes share a sequence watermark instead of adding control
  records to the bounded queue. A terminating record may evict ordinary queued
  records but remains subject to both the queue byte limit and file-size cap.
* UNC custom paths are rejected syntactically on Windows. Ordinary and
  extended drive-letter roots are classified with `GetDriveTypeW` without
  resolving the requested path, so an unavailable mapped drive cannot stall
  the safety check. Extended local drive paths remain valid.

Logs use stable event names and `key=value` fields where a record may be
processed or compared. Asynchronous operations include their playback or
device generation. Normal info records avoid full source paths; opt-in debug
records may contain the path required to reproduce a local I/O failure.

High-frequency observations are counters or periodic progress records. They
are not logged once per packet, decoded frame, render, or presentation.

Logs and current diagnostics snapshots have different roles:

* Logs reconstruct lifecycle and causality over time.
* Snapshots expose the current effective state to UI, tests, and bug reports.
* Performance traces and physical measurements remain separate tools.

## Consequences

The default session file makes failures diagnosable even when SunPlayer is
started as a GUI executable without a terminal. Runtime category filtering
keeps normal playback quiet, while an opt-in debug run can expose operation
boundaries without rebuilding.

The application owns a small amount of file-retention, bounded queue, and
formatting code. That code must remain bounded, thread-safe, and independently
tested. It must not grow into a competing logger, telemetry service, or
database.

Logs may contain sensitive details when debug logging is enabled, so a future
bug-report export must preview and redact them deliberately rather than upload
them automatically.

A reparse point located beneath an otherwise local drive can still redirect
file access. Resolving that target during startup would reintroduce the remote
I/O risk this policy avoids. Custom log paths are therefore an explicit
advanced override; automatic logs remain under the process-local temporary
location.

## Alternatives considered

### Use only debugger or stderr output

Rejected because the Windows GUI target commonly has no terminal and many
failures are reported after the process exits.

### Add a third-party logging framework

Rejected. Qt already provides the required category, filter, severity, and
handler contracts.

### Write full debug logs by default

Rejected because of performance, storage, and privacy costs.

### Keep only a current diagnostics snapshot

Rejected because a snapshot cannot reconstruct cancellation, fallback, seek,
or device-recovery ordering.
