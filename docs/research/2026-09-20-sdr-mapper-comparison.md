# SDR mapper comparison and upstream guidance

The controlled libplacebo 7.360.1 scalar comparison holds source maximum at
1000 nits, target at 100 nits, target black at 0.1 nits, and default mapper
constants. BT.2446A/spline output pairs include:

| Source nits | BT.2446A | Spline |
| --- | --- | --- |
| 0.1 | 0.3384 | 0.3094 |
| 1 | 1.0396 | 1.1288 |
| 50 | 15.1083 | 25.5889 |
| 100 | 25.3709 | 38.2757 |
| 203 | 43.1382 | 53.9764 |
| 1000 | 100 | 99.9997 |

This characterizes substantial midtone differences. Formula regression is not
proof of preferred appearance, grading fidelity, or superiority on all content.

The user supplied mpv issue 11596 during implementation. Reading its comments
with `gh api` confirms haasn recommends spline plus peak detection (April 2023),
then explains spline uses scene-average brightness whereas BT.2446A does not
(May 2023). This is a recommendation of a curve plus measurement, not evidence
that selecting spline alone reproduces mpv's entire pipeline.

The current mpv master manual (checked 2026-09-20) defaults gpu-next auto mapping
to spline, while its BT.2446A entry still recommends it for well-mastered content.
Peak computation defaults to auto and uses temporal smoothing/scene handling.
Therefore upstream guidance supports spline as a general playback baseline,
but does not make BT.2446A intrinsically invalid. Peak measurement remains an
independent change with source-history and paused-rerender requirements.

Sources:
- https://github.com/mpv-player/mpv/issues/11596#issuecomment-1512879974
- https://github.com/mpv-player/mpv/issues/11596#issuecomment-1566105577
- https://mpv.io/manual/master/#options-tone-mapping
- https://mpv.io/manual/master/#options-hdr-compute-peak
- https://libplacebo.org/options/#tone_mappingfunction

The user chose to retain the current BT.2446A SDR baseline for this change
rather than switch to spline now. Equivalent metadata/fallback policy remains
implemented; general playback curve choice and measured peak detection remain
follow-up questions. This is an explicit scope decision, not a fidelity claim.
