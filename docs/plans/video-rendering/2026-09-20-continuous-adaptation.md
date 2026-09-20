# Continuous HDR adaptation and truthful Wayland output

Status: implemented and independently reviewed; available automated checks
passed except the fullscreen smoke precondition described below. Native managed
Wayland and physical display acceptance remain explicit follow-up gates.

Scope: separate SDR compatibility from adaptive HDR headroom; consistent generic
PQ policy for equivalent metadata/fallback; declare Wayland composed target volume.
Preserve reference-white adaptation and nominal-100 SDR. Exclude peak detection,
comfort limits, dithering, source ICC, and new user controls.

Plan reviewed independently for correctness/HLG, Wayland lifecycle, and simplicity.
Use one mode in the existing rendered surface, existing policy and renderer,
and one pending native description rather than new strategy/transaction layers.

Implementation and validation:
- [x] Explicit rendering mode, including paused reuse and WCG distinction.
- [x] Extend libplacebo's existing virtual-peak HLG model to H=1 explicitly.
- [x] Controlled SDR curve comparison; equivalent-input PQ regression.
- [x] Real decoded HDR headroom sweep and final composition capture.
- [x] Capability-aware Wayland composition volume and async pre-present declaration.
- [x] Focused checks, independent implementation review, current docs and final diff.

Native macOS and managed Wayland physical-display validation require their hosts;
Windows GPU readback establishes application behavior, not emitted luminance.

## Validation evidence (2026-09-20)

- Windows Debug full build passed. Full CTest run: 37/38 passed, including
  decoded HDR, compositor, hardware decode, media, and surface reuse tests.
- `application-fullscreen` timed out twice at stage 0 before sending F11:
  `cursorHidden=0`, `cursorShape=0`. The user manually double-clicked the open
  window and confirmed fullscreen works. This is not counted as an automated
  pass; the smoke precondition remains unresolved and outside this color change.
- After review fixes, Windows `presentation-target`, `libplacebo-color-policy`,
  and `ffmpeg-first-frame` passed (3/3; decoded GPU test about 30 seconds).
- An isolated Ubuntu 26.04 container built the full native Linux project with
  Qt 6.10.2 and libplacebo 7.360.0. Five focused checks passed: presentation-target,
  rendered-video-surface, libplacebo-color-policy, wayland-color-management,
  and linux-platform-dependencies. P3 cube-corner bounds are included.
- A bounded Weston 14.0.2 headless GL/lavapipe probe opened the HDR fixture using
  the production native Wayland path. Weston advertises only experimental
  `xx_color_manager_v4`, not stable `color-management-v1`, so SunPlayer correctly
  selected unmanaged SDR. This does not validate managed ready/failure events,
  paused target changes, or declaration/buffer pairing on a capable compositor.
- Three independent review lenses covered behavior/HLG/tests, Wayland lifecycle,
  and simplicity. Fixed findings: preserve UI texture rebind before asynchronous
  waiting, cover diagnostic mapping bypass, and bound P3/outside-container colors.
  Re-review found no remaining concrete blocker. No extra strategy framework,
  temporal analysis, dependency patch, or user control was introduced.

Native managed compositor and macOS/display-transition validation is recorded
in `docs/DEFERRED.md`; software readback does not establish physical luminance.

The user reviewed mpv issue 11596 and explicitly retained the BT.2446A SDR
baseline for this pass. Upstream rationale and the controlled comparison are
recorded in `docs/research/2026-09-20-sdr-mapper-comparison.md`.
