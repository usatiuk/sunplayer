# Playback media details

## Goal

Turn Player's optional playback-statistics overlay into a compact playback-
details view that explains the selected video, audio, and subtitle tracks,
classifies the current decoded video signal without guessing, distinguishes
source dynamic range from presentation mode, and retains the existing live
performance diagnostics.

## Grounded constraints

* The final retained decoded `AVFrame` remains the authoritative source-color
  evidence under ADR 0012. Diagnostics may derive a small current value from
  it, but must not create a parallel HDR metadata model or duplicate
  FFmpeg/libplacebo interpretation.
* Stream probing remains part of the single `decodeMediaFrames()` operation
  under ADR 0011. The details UI must not open a second demuxer or run a
  separate probe.
* Track selection is generation-scoped. Details may be eventually consistent
  while a replacement generation opens, but stale operation results must not
  replace the current media identity.
* The output description comes from the existing `PresentationOutputState`;
  Player must not infer display or swapchain state from source metadata.
* Unsupported or incomplete source metadata is reported as unknown or generic
  PQ HDR. It is never silently promoted to a branded HDR format.

## Scope

1. Preserve the currently discarded structured subtitle descriptor and add
   only the missing stream facts needed by the UI: source audio sample rate,
   channel layout, and text/bitmap subtitle kind.
2. Let the existing track models return the selected descriptor to their
   owning `MediaSession`; do not add a second selected-track store.
3. Derive a small dynamic-range classification from each current retained
   frame, in precedence order: Dolby Vision, HDR10+, HLG, static-metadata
   HDR10, generic PQ HDR, known SDR, unknown.
4. Publish selected track summaries, current video signal/dynamic range,
   nominal frame rate, and an HDR-source boolean from `MediaSession`.
5. Pass `PresentationOutputState` to `PlayerPage` and replace the flat
   statistics overlay with Media, Video, Audio, Subtitles, Output, and
   Performance groups. Keep error/fallback diagnostics visible.
6. Rename user-visible and QML-local statistics concepts to playback details.

## Deliberately deferred

* Bitrate and variable-bitrate estimates.
* Codec profile/level and friendly codec-brand normalization.
* Mastering-display luminance, MaxCLL, and MaxFALL readouts.
* Dolby Vision profile display and claims about enhancement-layer support.
* Atmos, DTS:X, or other object-audio inference.
* A general-purpose ffprobe-style inspector for every unselected stream.

## Verification

* Focused decoded-frame tests cover every classification branch and the
  conservative unknown/PQ behavior using real FFmpeg frame side data.
* Existing real-media decoder tests assert the newly retained audio and
  subtitle facts.
* The production `MediaSession` scenario verifies selected summaries and
  source signal properties through its public API.
* The QML shell scenario opens the renamed panel and asserts representative
  Video, Audio, Subtitles, Output, and Performance text plus track-selection
  updates.
* Run clang-format on changed C++ files, build the affected Debug targets,
  execute focused media/playback/UI tests, then run the full registered
  non-device test set if focused validation passes.

## Delivery evidence

Completed on Windows against the existing `cmake-build-debug` tree, with the
Visual Studio developer environment initialized for every build and test
command:

* `clang-format` was applied to every changed C++ source and header, and
  `git diff --check` passed.
* The affected targets `sunroom_decoded_video_frame_tests`,
  `sunroom_ffmpeg_media_decoder_tests`, `sunroom_media_session_tests`, and
  `sunroom_qml_shell_tests` built successfully.
* Focused `ctest -R
  "^(decoded-video-frame|ffmpeg-media-decoder|media-session|qml-shell)$"`
  validation passed 4/4 after correcting a synthetic test fixture to
  initialize FFmpeg side-data storage explicitly.
* The complete Debug tree built successfully (128/128 steps), and
  `all_qmllint` passed for both the application and shell-test QML modules.
* `ctest -LE "device|gpu" --output-on-failure` passed all 22 registered
  non-device/non-GPU tests.

The first pre-review focused run had one 1 ms timing mismatch in an existing
media-session clock assertion; its immediate isolated rerun and both later
focused/full runs passed without changing that test. Independent review then
found and drove conservative validation for typed HDR side data, complete
per-frame diagnostics change detection, unavailable-output wording, and
source-versus-presentation branch coverage. Small-window/keyboard scrolling
expansion remains intentionally out of scope for this focused details-panel
change.
