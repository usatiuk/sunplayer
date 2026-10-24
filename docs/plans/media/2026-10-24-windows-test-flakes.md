# Windows decoder and QML test reliability

## Scope

Investigate and remove the recurring ffmpeg-media-decoder failure and the
qml-shell-windows overall timeout. Keep behavioral assertions and supported
platform contracts; do not mask failures with retries or disable coverage.
The user subsequently added the observed MediaSession diagnostic flake and
whole-suite CI reporting, then authorized committing and pushing all changes.
Do not rerun the failed hosted job; validate the new commit through normal CI.

## Evidence and approach

- CI run 40095307171 fails decoder after 2.11 s with no captured assertion and
  Windows QML at its exact 30 s CTest limit. Linux and AppImage jobs pass.
- Decoder also failed in Windows jobs 120109440981 (October 22), 117795332395
  and 117793758388 (October 18), before Qt 6.12. Its Qt Test output is absent
  from hosted logs, unlike shell tests whose JUnit reports are printed.
- Windows QML passed in 28.93 s (120109440981) and 29.02 s (120018246085).
  October 18 job 117798938198 instead failed a QML-warning assertion, a distinct
  issue fixed by subsequent palette/scrollbar work.
- Reproduce decoder locally with retained file logs, including concurrent load.
  Fix only an established cause. Preserve failure output in CI.
- Give complete shell suites a bounded budget reflecting their actual runtime;
  retain individual observable-state waits and all three control styles.
- Build and stress the affected targets with normal Developer PowerShell/CLion
  paths, review with correctness/simplicity/evidence lenses, record results.

## Delivery

The decoder passed 20 isolated and 60 unconstrained concurrent runs, but failed
2/4 runs immediately when four processes shared two logical CPUs. The failed
assertion was `cancelsWhileAudioSubmissionIsBackpressured(): result.isCancelled()`.
The aggregate classifier required video cancellation even when video had already
reached EOF before audio was cancelled. It now classifies its own cancellation
state without rewriting the completed stream result. A deterministic result
regression covers that contract. All 80 post-fix constrained decoder runs passed.
This establishes a reproduced failure mode; absent hosted assertion text prevents
attributing every historical decoder failure to it.

The three QML style suites now have a 60-second overall CTest budget instead of
30 seconds. Individual state waits and behavioral assertions are unchanged.

Report handling applies to every registered test. Installed Qt 6.12 sources
(`qplaintestlogger.cpp` and `qlogging.cpp`) confirm that Qt Test can send stdout
text to OutputDebugString without an attached console. The existing
`QT_FORCE_STDERR_LOGGING=1` setting restores captured output; append it once for
all tests. CTest's built-in JUnit report and temporary logs are uploaded on both
Windows and Linux regardless of success/failure. The former shell-specific Qt
JUnit arguments and hand-picked report-printing block are removed.

The first full CI-selected local run passed 33/34, including both targeted suites.
Captured output and the saved JUnit report exposed a separate media-session
failure in `mutedPlaybackKeepsPresentedAudioAsMaster`: it asserted the cached
audio-clock diagnostic immediately after advancing controlled playback. This
diagnostic is sampled every 100 ms. The user explicitly added this flake to scope;
the assertion now waits up to one second for the same value. Forty isolated
constrained executions before the fix passed, consistent with its intermittent
scheduling dependency. Production sampling and clock behavior remain unchanged.

## Final validation and review

- All Windows targets build in CLion's existing `cmake-build-debug`, using VS
  Developer PowerShell and normal dependency caches outside the sandbox.
- The five initially affected CTests passed with parallelism four (31.40 s).
  The final complete Windows CI-selected suite passed **34/34** with parallelism
  four (29.16 s). This excludes device/GPU tests exactly as hosted Windows CI does.
- The original constrained decoder failure was followed by **80/80** constrained
  post-fix passes. The corrected MediaSession case passed **40/40** constrained
  executions. These loops were investigation/validation, not CI retry policy.
- Native CTest JUnit reports contain all 34 selected tests and captured stdout.
  Verified that the first failed report includes the actual MediaSession
  `audioClockReliable()` assertion; the final report has zero failures.
  Logs/reports are retained under the existing build directory.
- Three independent reviews covered correctness, simplicity/boundaries and
  tests/evidence/scope. The final MediaSession wait and whole-suite reporting
  revisions received the same review; no actionable findings remain.
- `git diff --check` passes. Hosted Windows/Linux execution of the final workflow
  changes remains pending the new push. No native Linux/macOS run was performed
  locally; the previous failed run's watcher was deleted after completion.
