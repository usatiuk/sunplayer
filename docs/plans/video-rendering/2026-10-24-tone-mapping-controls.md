# Output-specific tone mapping and peak detection

## Scope and accepted behavior

Reset the local brightness experiments, update the Windows/macOS Qt pin to
6.12.0, and add two independently persisted playback algorithm preferences:
SDR offers BT.2446A (default), Spline, BT.2390; HDR offers Spline (default),
BT.2390. One editor shows the preference for the actual active presentation
output, including live output changes. HDR capability or headroom alone does
not select the preference. Keep authored HDR10+ precedence and Dolby
representation/metadata handling unchanged. One shared default-enabled peak
detection switch permits libplacebo measurement where usable dynamic guidance
does not already govern mapping. Reference white returns to 100–203/default100.

No new exposure, contrast, percentile, dithering, display-profile, mapper,
fallback or dependency policy beyond the Qt update. Linux keeps its supported
system Qt contract. No commits, pushes or external mutations.

## Implementation plan

1. Restore tracked files to remove the brightness experiments. The temporary
   backup/test-media directory was subsequently removed at user request.
2. Ground contracts in subsystem docs, ADR0039, existing settings, output
   reconciliation and pinned libplacebo. Keep the existing headroom selector
   for SDR input and HDR Lab. Decoded HDR uses actual output intent for its
   user choice, including HDR with H=1.
3. Add a small typed settings value containing the two algorithms and detector
   preference, owned by DecodedVideoSource. Reuse content revision/update for
   paused changes and existing application settings storage. Validate stored
   enum strings once. Preserve unrelated settings independently on invalid input.
4. Carry actual HDR output in the existing resolved target; make it canonical
   rather than adding a duplicate rendering mode. UI and producer consume that
   same value. A target change invalidates retained output through existing
   description equality.
5. Resolve the chosen algorithm through existing libplacebo policy. Preserve
   source-authored ST2094-40 and usable dynamic metadata; permit ANY for detected
   ordinary HDR10. Keep inverse mapping and dithering unchanged. Reset temporal
   detector state at existing source identity discontinuities, not target changes.
6. Update Qt pins and Windows CI paths/cache/install/runtime contract. Build
   with CLion cmake-build-debug and Visual Studio Developer PowerShell; record
   exact verified commands in ignored AGENTS.override.md.
7. Add regression coverage through public settings/UI/policy and decoded GPU
   boundaries: independent persistence, invalid choices, active-editor changes,
   authored priority, HDR H=1, paused edits and peak measurement/state reset.
8. Synchronize subsystem docs, decisions, research, PLAN.md and testing evidence.
   Review implementation with three independent read-only lenses, including
   simplicity. Fix demonstrated findings, rerun affected checks and reassess.

## Validation and review evidence

- Git restore completed; the temporary experiment backup and generated media
  directory were removed at user request.
- User explicitly authorized builds/tests and requested CLion's build directory,
  PowerShell developer shell, documentation, review loop and a tracked goal.
- Normal vcpkg configure succeeded outside the sandbox with CLion's existing
  Qt prefix/toolchain and VCPKG_MANIFEST_INSTALL=ON. Do not keep the temporary
  install bypass used during the initial sandbox diagnosis.
- App/all test targets build on Windows/MSVC with Qt6.12.0. QML lint succeeds
  with Qt6.12 renamed-type warnings in existing QML; no unrelated QML rewrite.
- Focused presentation-target, application-settings, settings-dialog and
  libplacebo-color-policy tests pass outside the sandbox.
- Three independent implementation reviewers covered correctness, explicit
  KISS/DRY/YAGNI/module boundaries, and tests/evidence/scope. Two findings fixed:
  preserve the combo model during unchanged-output refresh, and remove an
  unnecessary detector setting before an existing fixed-revision SDR assertion.
- Full Windows Qt6.12.0 CTest run: 41/42 pass (313.97s), including QML styles,
  packaged QML, real audio/video playback, compositor, software/hardware decode,
  metadata priority and settings. application-fullscreen times out at stage6
  (fullscreen-from-maximized); an isolated rerun reproduces it. Its stage6
  predicate checks fullscreen/new frame/other-display readiness, not cursor
  hiding. The timeout's cursor fields do not establish a cursor defect.
- User confirms manual fullscreen and cursor hiding work while using the
  desktop. Desktop/focus interference is plausible but not established; record
  the automation limit without changing product behavior or weakening the test.
- After final review fixes, settings-dialog and qrhi-compositor pass again.
  The extended toneMappingSettingsRerenderRetainedFrame GPU case passes directly,
  including measured peak, paused updates/import reuse and fresh-output
  equivalence after both decoder and playback-generation discontinuities.
- All three reviewers rechecked fixes and final scope/contracts. No remaining
  actionable findings; no new metadata engine, output epoch or extra controls.
- clang-format applied; git diff --check passes. Concise ignored
  AGENTS.override.md records the verified local workflow. No commits or external
  application/repository mutations were performed.

## Known limits

Native Linux/macOS output transitions and physical luminance cannot be proven
by Windows GPU readback. Record actual host evidence without claiming other
platform execution. The pinned libplacebo lacks later spline fixes; changing
it remains outside this change.
The isolated fullscreen automation failure and existing Qt6.12 renamed-type
lint warnings are tracked in DEFERRED.md. Native output movement and hosted
Qt6.12 packaging/installation remain unverified here.
