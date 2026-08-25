# Subtitle appearance settings

Status: Implemented

## Goal

Add a durable general Settings surface and let viewers customize subtitle
appearance with immediate feedback. The implementation must preserve authored
subtitles by default, use libass and the existing subtitle raster/compositor
boundaries, persist validated global preferences, and avoid a second subtitle
parser, renderer pipeline, or QML-owned settings copy.

Completion means:

* Settings is reachable with or without active media, and the Subtitles page is
  also reachable directly from the existing subtitle-track menu.
* Text subtitle appearance, size, vertical position, and opacity can be changed
  while playback continues; the current cue rerenders immediately.
* Custom foreground, background, and edge colors are unrestricted sRGB colors,
  and every exposed opacity accepts the full `0` through `100%` range.
* Styled ASS remains authored unless the viewer explicitly enables an override.
  Overrides use libass's best-effort regular-dialogue classification so
  explicitly positioned/typeset events normally remain authored; the UI does
  not promise perfect classification for every ASS script or karaoke effect.
* Bitmap subtitles retain authored pixels and geometry. Only whole-layer size,
  vertical position, and overall opacity apply to them.
* Preferences survive ordinary restart, invalid stored values recover to safe
  defaults, and Restore defaults removes the subtitle override keys.

## Grounded constraints

* `SubtitleSource` already keeps decoded timing/content separate from
  presentation. Retained text events can be rerendered without seeking or
  restarting the media operation.
* `SubtitleRenderer` owns libass, the CPU raster, and its GPU texture. Its cache
  currently invalidates for subtitle source/time/viewport changes, so appearance
  needs one additional revision input.
* The supported common denominator is system libass 0.17.4; the current pinned
  Windows/macOS dependency is 0.17.5. Both provide selective dialogue
  overrides, `ass_set_font_scale()`, `ass_set_line_position()`, and border
  styles 1, 3, and 4. The color override is one group covering primary,
  secondary, outline, and background colors; the ASS border model also couples
  background and edge structure.
* Consequently, foreground/background/edge use one authored-or-custom
  appearance mode. Independent authored/custom mixing for those three groups
  would require multipass rendering or output-mask post-processing and is not
  part of this slice. Size and position remain independently overridable.
* The existing subtitle texture is premultiplied sRGB and the compositor maps it
  into the shared linear-light presentation path at the deliberate `0.8`
  subtitle brightness. Appearance settings do not expose or change that HDR
  policy.
* `ApplicationSettings` is persistence and validation only. Observable runtime
  preference state belongs to a dedicated owner restored before the UI and the
  presentation engine are constructed.

## Product behavior

### General Settings surface

Add one window-owned native Qt Widgets Settings dialog, matching the existing
About/support-dialog conventions. Qt Widgets is already a project dependency
and supplies platform-consistent tabs, forms, spin boxes, keyboard navigation,
and an unrestricted RGB color picker without another library. The first
version has two tabs:

* **Playback** binds to the existing canonical volume and capability-gated
  `Blank other displays in fullscreen` values. Existing transport/menu quick
  controls remain available and share those same runtime owners.
* **Subtitles** owns the appearance controls below.

Add `Settings…` to the active and idle overflow menu. Add `Subtitle settings…`
after the track list in the Subtitles submenu; it opens Settings directly on
the Subtitles page. A general opening always opens Playback; no page selection
state is retained or persisted.

The QML menu calls `PresentationWindow::showSettings(page)`; the window owns a
single `QPointer` to the modeless-lifetime, window-modal native dialog and
raises it on repeated requests. The dialog does not pause playback. Native
focus routing prevents player shortcuts while it is active; Escape/Close,
Tab/Shift-Tab, arrow keys, spin boxes, labels, and visible focus behavior come
from the platform Qt Widgets style. Closing or recreating the redirected QML
scene does not discard an open native dialog or the canonical settings state.

### Subtitle controls

When a real text or bitmap cue is active, every relevant edit updates that
actual cue live. No synthetic preview is added: the real renderer is the
authoritative preview, and avoiding a second illustrative subtitle view keeps
the first slice smaller and less misleading when no cue is active.

Controls are:

* **Preset actions:** `As authored`, `High contrast`, and `Large text` update
  the canonical values in one batch. Presets are shortcuts, not a restricted
  palette; individual controls continue to show the resulting values.
* **Appearance:** `Authored` or `Custom`. Custom reveals foreground,
  background, and edge controls while continuing to preserve authored font,
  weight, italics, shaping, event text, and timing.
* **Text:** unrestricted sRGB color plus `0–100%` opacity.
* **Background:** enabled switch, unrestricted sRGB color, and `0–100%`
  opacity. Zero opacity is equivalent to a transparent background but remains
  a valid explicit value.
* **Edge:** `None`, `Outline`, or `Shadow`, with unrestricted sRGB color and
  `0–100%` opacity. Thickness/distance use renderer-owned values proportional
  to subtitle size and are not user controls.
* **Size:** `Authored` or `Custom`; custom scale is `50–200%` in one-percent
  stored precision with keyboard steps of `10%` and a `100%` default.
* **Vertical position:** `Authored` or `Custom`. Custom exposes one `0–100%`
  slider labelled `Bottom` to `Top`, with stored one-percent precision and a
  `10%` keyboard step. Text maps directly to `ass_set_line_position()`. The
  supported 0.17.4/0.17.5 implementations apply it to non-explicit,
  bottom-aligned events while explicitly positioned, top-aligned, and
  center-aligned events stay authored; focused tests lock that behavior.
* **Overall opacity:** `0–100%`, applied after text/bitmap rasterization and in
  addition to component opacity. This is the only color-affecting override
  available for bitmap subtitles.

Color editing uses the platform `QColorDialog`. Its current color previews on
the real subtitle while the picker is open, and cancelling restores the prior
color. Opacity remains an explicit adjacent percentage control rather than
being hidden in the color picker. A non-blocking `Subtitles hidden by opacity`
notice appears when overall opacity is `0%`; `Text is transparent` appears when
Custom appearance has text opacity `0%` and no nontransparent Outline/Shadow
edge. Neither notice rejects a viewer's chosen values. Dynamic contrast
estimation against moving video is deferred.

Preset values are:

| Preset | Appearance | Size | Position | Overall opacity |
| --- | --- | --- | --- | --- |
| As authored | Authored | Authored | Authored | 100% |
| High contrast | white 100%, black background 80%, no edge | Authored | Authored | 100% |
| Large text | Authored | 150% | Authored | 100% |

Changes commit to the runtime owner and persistence adapter immediately. There
is no Apply/Cancel or transactional draft copy; `Close` only closes the dialog.
`Restore defaults` is immediate, confirmation-free, removes all persisted
`subtitles/appearance/*` keys, and returns to the As authored state.

Dormant custom defaults are opaque white text, enabled black background at
`80%`, edge None with dormant opaque black, `100%` scale, `0%` vertical
position, and `100%` overall opacity. Switching a mode from Authored to Custom
reveals the retained custom values. Selecting As authored changes only
effective modes/scale/position/overall opacity and retains dormant colors; High
contrast writes its custom fields; Large text selects authored appearance and
position with `150%` custom size.
Restore defaults, unlike selecting a preset, resets every dormant field to the
defaults above.

## Runtime and persistence design

Introduce one application-owned `SubtitleSettings` QObject and an immutable
`SubtitleAppearanceSnapshot` value used by presentation. The QObject exposes
the enums and values described above, a monotonic `rasterRevision`, batched
preset and reset operations, and `settingsChanged`,
`persistenceChanged(dirtyMask)`, and `persistenceResetRequested` signals.
`rasterRevision` advances only for appearance, component opacity, size, or
position changes which affect text/bitmap rasterization; overall opacity does
not advance it. Every change still emits `settingsChanged` so the engine
requests one frame. Ordinary setters emit the affected key mask; preset
application emits one combined mask; reset changes canonical values once and
emits only the reset persistence intent so removed keys are not immediately
written back. Setters reject non-finite or out-of-range C++ inputs; QML controls
generate only valid values.

`PresentationWindow` constructs `SubtitleSettings`, restores it from
`ApplicationSettings`, connects canonical changes to per-key writes, and then
passes the same owner to `QuickUiLayer` and `RhiPresentationEngine`.
`MediaSession`, decoded subtitle state, and QML do not own or mirror these
preferences. Graphics-device/QML-engine recreation retains the window-owned
settings object.

Persist these stable keys:

```text
subtitles/appearance/mode
subtitles/appearance/textColor
subtitles/appearance/textOpacity
subtitles/appearance/backgroundEnabled
subtitles/appearance/backgroundColor
subtitles/appearance/backgroundOpacity
subtitles/appearance/edgeStyle
subtitles/appearance/edgeColor
subtitles/appearance/edgeOpacity
subtitles/appearance/sizeMode
subtitles/appearance/scale
subtitles/appearance/positionMode
subtitles/appearance/verticalPosition
subtitles/appearance/overallOpacity
```

Enums use stable lowercase strings; colors use opaque `#RRGGBB`; numeric
values use normalized finite doubles. Missing keys retain product defaults.
Invalid or missing individual values fall back independently to that field's
safe default; a valid custom mode does not discard other valid fields because
one field is malformed. Existing bounded settings fault logging applies
without logging stored values. Unknown keys survive. Add
`ApplicationSettings::removeSubtitleAppearance()` for reset and a masked
subtitle-appearance write method so only changed keys are written. Automated
application scenarios continue using the existing temporary INI backend.

## Rendering design

Pass the raster-affecting portion of `SubtitleAppearanceSnapshot` and its
`rasterRevision` into `SubtitleRenderer::prepare()`. Carry an explicit
`appearanceChanged`/force-composite condition through both `prepare()` and
`rasterize()` so the current inner `bitmap unchanged && assChanged == 0` early
return cannot swallow live bitmap transforms or style changes. Record the
rendered raster revision only after successful rasterization. Text-affecting
changes reconfigure the existing libass renderer in place and reraster retained
events; they never restart decode, rebuild/replay the libass track, or alter
event history. Overall opacity bypasses `SubtitleRenderer` entirely.

Use libass selective dialogue overrides only:

* Size uses `ass_set_font_scale()` with selective font scaling.
* Position uses `ass_set_line_position()` only in Custom mode. Authored mode
  restores libass's default line position. The tested 0.17.4/0.17.5 behavior
  described by the UI is the supported implementation contract.
* Custom appearance maps colors and border style into one `ASS_Style` override.
  Use libass border style 1 for outline/shadow without a box, style 3 for a
  background with none/shadow, and style 4 for a background plus outline.
  Populate a non-null fallback `FontName` even though font-name override remains
  disabled. Convert `#RRGGBB` plus opacity to libass RGBA with its inverted
  alpha convention. Style 1 uses `OutlineColour`/`Outline` and
  `BackColour`/`Shadow`; style 3 uses `OutlineColour`/`Outline` for the box and
  `BackColour`/`Shadow` for an optional shadow; style 4 uses `BackColour` for
  the box, `OutlineColour`/`Outline` for the outline, and `Shadow` as box
  padding. Supply fixed values in libass's 288-line reference space:
  `Outline = 1.0` for an outline, `Shadow = 1.0` for shadow distance,
  style-3 `Outline = 2.0` for box padding, and style-4 `Shadow = 2.0` for box
  padding. Do not pre-scale these values; libass applies the selected font scale
  once.
* Authored appearance disables color and border overrides completely. It does
  not synthesize defaults over ASS styling.

Plain text already enters libass through SunPlayer's fallback ASS style, so the
same override path applies. Inline ASS color/alpha tags and unpositioned
karaoke remain best-effort libass behavior; the player neither strips tags nor
claims to preserve every effect under a custom appearance.

For bitmap subtitles, extend only `drawBitmapComposition()`: map authored
regions into the video rectangle, take the union of those destination
rectangles, scale every rectangle about the union center, then translate the
transformed union vertically. Slider `0%` aligns its bottom to the video's 5%
bottom inset and `100%` aligns its top to the 5% top inset, interpolating the
union center between those endpoints. If the transformed union does not fit
inside the inset, center it in the video rectangle and let the existing target
clip it; never silently reduce the requested scale. Authored size/position
continues to use the current mapping unchanged.

Implement overall opacity as a compositor uniform, not a subtitle reraster.
Multiply the effective subtitle alpha by the normalized setting and use that
same effective alpha for premultiplied foreground contribution and background
attenuation. This applies equally to text and bitmap, preserves transparent
pixels, and makes opacity-only edits a uniform update rather than a texture
upload. Add `subtitleOpacity` to `HdrCompositorParameters`; the engine copies
the current canonical value for each requested frame.

Changing a text override calls the existing libass renderer configuration APIs,
which invalidate libass's caches themselves. Opacity-only changes do not touch
decoded, libass, raster, or texture state. Existing raster and upload budgets
remain unchanged, and a style failure preserves the current nonfatal
transparent-layer behavior.

## Implementation slices

1. **Settings model and persistence**
   * Add `SubtitleSettings`/snapshot enums, validation, `rasterRevision`,
     preset/reset batching, and typed QSettings round trips.
   * Restore before UI/engine creation and write through canonical changes.
2. **Renderer integration**
   * Add appearance invalidation and live rerastering.
   * Implement the supported libass selective overrides and bitmap-safe final
     rectangle transforms without changing decoding, timing, or track
     selection; add compositor-level overall opacity.
3. **General Settings UI**
   * Add the native Qt Widgets Settings dialog, Playback/Subtitles tabs,
     unrestricted RGB pickers, direct subtitle-menu route, presets, reset, and
     low-visibility notice.
4. **Validation and documentation**
   * Extend settings, renderer, and QML tests; update subtitle/UI/application/
     testing subsystem truth, `PLAN.md`, and `docs/DEFERRED.md` only after the
     behavior is implemented and validated.

## Validation

Automated coverage must prove:

* Defaults, every preset, arbitrary colors, `0/100%` opacity endpoints,
  `50/100/200%` size, all position modes, independent key writes, reset-key
  removal, malformed-value fallback, unknown-key preservation, and restart
  round trips.
* A paused or playing current text cue visibly updates after each supported
  setting change without a media restart, generation change, seek, or event
  duplication; raster-affecting edits advance the settings `rasterRevision`,
  rerasterize, and upload pixels into the existing texture without advancing
  `SubtitleRenderer::textureRevision()` unless texture identity/size changes.
  Overall-opacity-only edits advance neither raster revision nor raster/upload
  work and alter only composed output. Returning to Authored restores the
  fixture's original style.
* Selective overrides change regular dialogue while a representative explicitly
  positioned/typeset event remains authored. Plain SubRip fallback and
  embedded-font ASS remain valid; karaoke/inline-tag behavior is observed but
  is not generalized beyond libass's documented best effort.
* Each supported libass border mapping renders the requested foreground,
  background, and edge alpha/color; transparent and low-contrast combinations
  remain accepted.
* Bitmap pixels remain authored while size/position transform the region union
  about its documented pivot. Fitting content respects the 5% endpoint insets;
  oversized content is centered and clipped to the video rectangle without
  being silently shrunk. Compositor tests prove overall opacity scales
  premultiplied subtitle contribution and coverage together for text/bitmap.
* QML coverage proves active/idle menu routes and the subtitle shortcut page;
  native dialog coverage proves page selection, playback mirroring and edits,
  representative appearance/geometry edits, live color preview/cancel,
  invisibility warnings, presets, Restore defaults, and window modality. Manual
  acceptance covers native focus and shortcut blocking while the modal dialog
  is active.
* Existing affected track selection, seek, route restoration, graphics
  recovery, compositor ordering, volume, fullscreen blanking, and persistence
  scenarios continue to pass.

Run focused application-settings, subtitle-settings, native settings-dialog,
subtitle-renderer, compositor, and QML shell tests first, followed by QML lint,
the existing media-session regression, the
non-device/non-GPU suite, and the supported cross-platform suites. Renderer
pixels remain covered on the existing D3D11/Metal paths; native Wayland pixel
evidence remains a documented platform gap unless a Linux QRhi capture seam is
added independently. On Windows, initialize the Visual Studio developer
environment for every
build/test command and run all build-related commands outside the sandbox.
Before any manual CMake reconfigure, check for a running CLion instance and
allow its automatic reload to finish.

Where native runners are available, manual acceptance exercises live changes
over bright/dark video, active/no-current-cue/no-media states, ASS dialogue plus
positioned text, bitmap PGS, 200% scale, 0% opacity, keyboard navigation,
fullscreen, restart persistence, and Restore defaults.

## Validation record

On 2026-08-26 the complete Debug tree and both QML lint targets built cleanly.
Focused application-settings, subtitle-settings, settings-dialog, QML-shell,
libass-dependency, subtitle-renderer, and QRhi-compositor tests passed. The full
Windows run passed 37 of 38 registered tests; the unrelated
`application-fullscreen` smoke remained timing-sensitive and timed out at
different existing state-machine stages in repeated runs. Independent UI,
backend, and test/documentation reviews found no remaining concrete issue in
the subtitle-settings change. The broader physical/manual display matrix above
remains release acceptance rather than an automated claim.

## Deliberately deferred

* Font-family selection or overriding embedded fonts.
* Independent authored/custom mixing of foreground, background, and edge.
* User-controlled outline thickness, shadow distance/blur, line spacing,
  letter spacing, horizontal placement, arbitrary drag positioning, or rounded
  background geometry beyond libass border styles.
* A second libass preview renderer, custom glyph-mask/background processing,
  bitmap recoloring, or a subtitle effects engine.
* Operating-system caption-style import, per-file/per-track profiles, subtitle
  delay, forced-only/language policy, external sidecars/downloads, and automatic
  control-overlay collision avoidance.
* Dynamic contrast analysis against video frames or claims that every custom
  color remains readable over every scene.

## Source guidance

* [W3C captions and subtitles](https://www.w3.org/WAI/media/av/captions/)
* [W3C media accessibility user requirements](https://www.w3.org/TR/media-accessibility-reqs/#captioning)
* [47 CFR 79.103 caption customization](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-C/part-79/subpart-B/section-79.103)
* [Apple Media Accessibility caption preferences](https://developer.apple.com/documentation/mediaaccessibility/captions)
* [libass public API](https://github.com/libass/libass/blob/0.17.4/libass/ass.h)
