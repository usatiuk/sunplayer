# Color pipeline simplification: investigation evidence

Date: 2026-10-22
Status: historical research preceding implementation. The delivery plan and
ADR 0039 now describe the implemented behavior; earlier probes below describe
the old code unless explicitly identified as final validation.

The subsequent [native color-contract investigation](2026-10-22-native-color-presentation-contracts.md)
expands the terminology, all SDR/WCG/HDR platform cases, mapping ownership and
standards/API evidence. It qualifies this note's earlier nil-metadata statement
for modern macOS and demonstrates the specific extra Mutter PQ-to-SDR curve;
this earlier audit is not the final native-output design.

The [delivery plan](../plans/video-rendering/2026-10-21-color-pipeline-simplification.md)
owns the design and scope. This note retains the investigation details removed
from the longer draft. Historical decisions, newest notes, and another platform's
appearance are evidence to examine, not proof of correctness. Windows/macOS WCG
SDR enablement and native output repairs are explicitly outside this refactor.

## Linear coordinates and black-level evidence

The intended composition contract uses signed linear BT.709/D65, with reference
white at 1 and video black agreeing with the application background within meaningful numeric
and output precision. A tiny residual such as 0.000001 is acceptable; exact
zero is not a product requirement. Do not add a correction layer to enforce
bit-exact black, white, or peak anchors, or depend on later quantization to hide
a material mismatch. The reproduced 0.0025 lift is a different scale of error.

Signal black is not the same quantity as the declared target minimum B, nor a
measurement of emitted panel light. Write down the mapped-light/composition/
native encoding relationship before changing normalization. Do not assume the
word "linear" establishes that relationship.

Pinned libplacebo's LINEAR output returns mapped light in its nits/203 unit.
Its gamma/sRGB output additionally subtracts the effective minimum and divides
by the effective range before applying the transfer function. The LINEAR
branch returns before that operation. Our producer currently only applies a
scale: 203/R for normal decoded PQ, 203/W for Absolute PQ, and 1 for relative
SDR/HLG. The compositor then applies the selected native encoding.

At an SDR endpoint, the library's inverse-transfer behavior gives an independent
reference for the coordinate conversion:

```text
relative linear signal = (mapped nits - effective black)
                       / (effective white - effective black)
```

Those endpoints must be the library's effective nominal target endpoints after
inference, not an observed frame peak, the tone mapper's source-capped peak,
or guessed physical monitor measurements. Never expand content merely because
its observed maximum is below the target maximum.

For B>0 and P>W, that affine sends peak to `(P-B)/(W-B)`, rather than exactly
`P/W`. This is a coordinate distinction to describe and bound, not a demand for
extra machinery to force all anchors exact. Before normalization changes,
record the chosen conversion, its actual peak coordinate, and the matching
native conversion, including optional Absolute PQ. Verify the resulting black,
white, and peak behavior within appropriate tolerances. The independent generic
selector cleanup need not wait for this investigation.

Validate usable range ordering once at target resolution. A formula dividing
by W-B requires B<W; the current B<=P check is insufficient. Classify unusable
external metadata there, using the existing failure/fallback boundary, rather
than adding repeated shader guards or clamping away invalid arithmetic.

Pinned renderer source confirms that PRE_OUTPUT's `parameters->color` contains
the inferred nominal target color space: inference happens on the pass-local
target, color conversion assigns it to the current image, then the hook runs.
Use that existing hook and the library's nominal-luminance API. No second
`infer_map`, contrast engine, extra pass, or normalization strategy is needed.
See the pinned [renderer implementation](https://github.com/haasn/libplacebo/blob/v7.360.0/src/renderer.c)
for target inference, color conversion and hook invocation order.

Target minima must retain their meaning: unknown, known zero, and known
positive are distinct. Wayland preferred luminances describe the compositor's
declared target model and can be nominal defaults; they are not measurements
of emitted panel light. Do not force a zero target minimum to hide raised
pixels, and do not claim the compositor necessarily "adds the floor twice."
Resolve the contrast assumption once and make the resulting coordinates agree
with the surface declaration.

### New controlled evidence

An offscreen probe on the host GPU used the production decoded-frame capture
helper and actual libplacebo 7.360.0. It held reference white at 80, H=1, decoded
PQ source reference R=100, source pixels, and target metadata constant; only
`VideoRenderingMode` changed. The full-range GBRP16 PQ source contained neutral
patches at 0, .01, .1, 1, 10, 100, 203, and 1000 nits.

| Target minimum | SDR compatibility: source-zero output | Adaptive HDR: source-zero output |
| --- | ---: | ---: |
| Known zero | 0 | 0 |
| Known 0.2 nit | 0.00249672 | 0.00249672 |
| Unknown | 0.000998497 | 0 |

The true zero region of HDR Lab with a known 0.2-nit minimum was approximately
0.00249863 in both modes for the one-times-peak pattern. Thus changing only the
mode does **not** fix the positive-minimum black mismatch, although it changes
the curve and unknown-minimum inference. This is renderer evidence, not a native
Windows comparison or a physical display measurement.

Scratch artifacts: `/tmp/sunplayer-black-gpu-mode-probe.cpp` and
`/tmp/sunplayer-black-gpu-mode.log`. The table records raw FP16 producer-texture
readback before native encoding. The log's additional `PQblack` and
`gamma22black` fields are CPU transfer-function calculations, not captures of
the final encoded surface. This document retains the material results even
when those temporary artifacts are removed.

Reproduction boundary: repository `9978b17bb28f9bc078c614dd30bc3773a7285ecf`,
Qt 6.10.2, libplacebo 7.360.0, libavcodec 62.11.100/libavutil 60.8.100. Allocate
an 8x2 `AV_PIX_FMT_GBRP16LE` frame tagged BT.2020, SMPTE2084, RGB, full range;
write each patch to all three planes as `round(65535 * PQ(nits))`, with no static
or dynamic HDR side data. Clone it through `DecodedVideoFrame`, then call the
existing `captureDecodedFrame` helper with W=80, H=1, composition scale=1,
explicit R=100, and each stated minimum-known/value pair. Only vary the mode
within each pair. Run on the host GPU with `QT_QPA_PLATFORM=wayland`, using an
unshown QWindow and the existing graphics factory. Promote this recipe into
`ffmpeg-first-frame` before using it as acceptance evidence for implementation;
do not create a permanent parallel probe framework.

The decoded SDR fixture also produced black approximately 0.000999451 in both
modes. A subsequent source trace identifies another input to that result:
the old Clip decision's `PL_HDR_METADATA_NONE` ignores the supplied source
luminance bounds and invokes nominal SDR inference. Reassess that result after
uncoupling source metadata mode from operator choice. Earlier temporary affine
experiments produced negative SDR values in one case; clamping those away is
insufficient evidence of correct shadow mapping.

## Native output audit: current limitations, not implementation work

The existing Windows/macOS extended-linear output route has capability gaps.
In the inspected Qt 6.11.1 source, D3D11 permits that FP16 swapchain only for an
active HDR output in both its support query and creation path. Metal reports it
supported only with EDR potential above one; its creation path does not repeat
that headroom gate. Our macOS selection also requires that potential. These are
limitations of the selected Qt/app output route, not evidence that WCG SDR
requires HDR or FP16.

Linux already owns the Wayland color declaration independently of Qt's FP16
swapchain selection. Its managed SDR route converts the composition to declared
display primaries and gamma 2.2 in an ordinary SDR buffer; the PQ route declares
BT.2020/PQ instead. Neither requires a Qt extended-linear swapchain. The internal
FP16 composition is separate from the native buffer format. Record the missing
Windows/macOS native WCG route without claiming gamut support that their active
output cannot carry; its repair is not a prerequisite for this shared refactor.

Native declaration audit: a pixel format specifies storage, while the native
color-space declaration specifies how the stored RGB values are interpreted.
QRhi presets bundle some of these choices. The final shader must match the
effective declaration; neither the monitor profile nor the source video's tag
automatically describes the application's composed buffer.

The macOS SDR path currently emits sRGB-encoded values into BGRA8 but does not
explicitly request an sRGB surface color space. Qt Cocoa's `QNSView::colorSpace`
falls back to `NSWindow.colorSpace`, and `propagateBackingProperties` copies it
to `CAMetalLayer.colorspace`. QRhi explicitly overrides that declaration for
extended-linear output, but its SDR creation branch does not set a declaration.
Thus the current SDR fallback must not be described as proven explicitly tagged
sRGB, nor as a correct WCG SDR path. The actual inherited profile has not been
observed on a native macOS host in this investigation.

Qt Cocoa already accepts `QSurfaceFormat::colorSpace` and propagates its ICC
description to the Metal layer. This is an existing route to an explicitly
declared sRGB or P3 SDR buffer without FP16; matching shader encoding and correct
declarations after display/format changes still need implementation and native
verification. The FP16 support-query gate does not block all WCG SDR routes.
Record this declaration gap and candidate repair with the deferred native work;
do not add a custom swapchain or make it a tone-mapping condition.

## Extended coordinates: gamut and brightness are separate

The shared working basis uses sRGB/BT.709 primaries and D65, with a linear
transfer and signed FP16 storage. It is not ordinary bounded SDR BT.709 video.
Negative coordinates permit physical colors outside the positive BT.709
primaries triangle. Multiplying the complete RGB triplet by a positive scalar
changes luminance while preserving chromaticity; values above one alone do not
expand that triangle.

For example, linear Display-P3 red `(1,0,0)` is approximately
`(1.224940,-0.042057,-0.019638)` in extended linear sRGB, with relative
luminance Y=0.228975 at reference-white Y=1. Both describe the same color.
Linear P3 `(4,2,0.1)` is approximately `(4.44988,1.91589,-0.12600)` in the
working basis: all three P3 display components are positive although one
working coordinate is negative. These are D65 RGB-to-XYZ matrix calculations,
not monitor measurements or renderer captures.

On active Windows/macOS extended-linear paths, Qt declares the native surface
as linear scRGB or extended linear sRGB; the system performs the final display
conversion. Linux's final shader first converts working RGB to BT.2020/PQ, or
to declared display-primary gamma-2.2 SDR. Its compositor receives that matching
declaration. A pixel format specifies storage; the color-space declaration
specifies interpretation. Neither the sign of an individual pixel nor the
source video's metadata substitutes for the declaration.

Linux currently prefers PQ independently of the selected display's headroom.
Managed SDR is its capability/rejection fallback, not a mode selected merely
because the source or current display is SDR. BT.2020 primaries are usable for
SDR and HDR; PQ is an HDR-capable luminance encoding that can also contain an
image limited to SDR brightness. The compositor converts the declared surface
to the active display output. This is a transport policy, not a tone-operator
selection rule or a requirement that the panel be driven in HDR mode.

## Native API and production practice reassessment

This October 22 follow-up examines official guidance and production code,
not just SunPlayer's implementation. App mapping, native encoding declaration,
and optional downstream mapping hints are different responsibilities. A
platform's API recommendation does not automatically apply to another API.

### Why the Wayland machinery was added

* August commit `7b956e0` selected custom declaration ownership for public
  readiness/failure control and same-surface fallback. Qt's version-1 binding
  was also treated as a limitation. Version-2 preferred observation can coexist
  with Qt's writer; a newer feedback version alone does not justify owning it.
* September commit `e7abecb55e4f63ac2383e6add85416169f136cda` added tighter
  composition gamut/peak declarations, containment/matrix bounds, and changing
  pending descriptions. The purpose was to describe the mapped video plus
  overlays more precisely than the full BT.2020/10000-nit container. Its
  validation explicitly left native managed-compositor behavior unverified.
* That change also made optional mastering capability mandatory for HDR.
  October commit `e7822fd` repaired the resulting native GNOME HDR regression.
  Adding a possible mapping hint and requiring that hint for HDR are separate
  decisions; the latter was a demonstrated mistake.

No recorded native A/B establishes the benefit of SunPlayer's particular
composition-volume calculation. This does not prove output hints useless.

### Official native guidance

* [Microsoft SetHDRMetaData](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgiswapchain4-sethdrmetadata)
  recommends mapping to the monitor's gamut/luminance rather than relying on
  optional metadata reaching or being treated consistently by the monitor.
  `SetColorSpace1` independently declares pixel interpretation. Our Qt FP16
  scRGB path need not acquire an extra outgoing metadata payload.
* [Apple's own tone-mapping route](https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping)
  uses an extended-linear floating-point layer and maps to current EDR
  headroom, responding to screen/headroom changes. Leaving
  [edrMetadata](https://developer.apple.com/documentation/quartzcore/cametallayer/edrmetadata)
  unset disables that CAEDRMetadata-driven mechanism; native display color
  conversion remains. The later investigation also covers macOS 15+
  `toneMapMode`, so nil alone is not a blanket modern no-mapping guarantee.
  Apple separately documents
  [system-owned video mapping](https://developer.apple.com/documentation/metal/using-system-tone-mapping-on-video-content).
  Our app-mapped EDR choice is a documented route, not missing mandatory HDR
  metadata.
* [Wayland's protocol](https://github.com/wayland-mirror/wayland-protocols/blob/1.47/staging/color-management/color-management-v1.xml)
  distinguishes the encoding volume from a smaller target/mastering volume.
  Omitting mastering values uses the primary volume. Smaller target volumes
  can reduce subsequent tone/gamut-mapping distances. These fields are not
  required to decode RGB, but are not necessarily inert after app mapping.

### Production practice and actual compositor behavior

* [mpv, revision 488fec0789e5026804ac8dd03fb749570416d48a](https://github.com/mpv-player/mpv/blob/488fec0789e5026804ac8dd03fb749570416d48a/video/out/vo_gpu_next.c#L1485),
  defaults to a target-based output hint when display information is known.
  It adjusts brightness metadata after mapping and renders to the negotiated
  output. Its
  [Wayland writer](https://github.com/mpv-player/mpv/blob/488fec0789e5026804ac8dd03fb749570416d48a/video/out/wayland_common.c#L3733)
  explicitly declares PQ luminances `(0,10000,203)` and supplies output
  mastering values when supported. This is app mapping plus output metadata,
  not necessarily source passthrough and not a requirement to copy its code.
* [libplacebo, revision 13d0b79827ea1671f991ca57700e5356af9731a5](https://github.com/haasn/libplacebo/blob/13d0b79827ea1671f991ca57700e5356af9731a5/src/d3d11/swapchain.c#L205),
  sets optional HDR metadata in its D3D11 swapchain, including its current
  dedicated scRGB path. It also supports Vulkan HDR metadata. This differs
  from Microsoft's recommendation and is evidence against claiming unanimous
  production practice. SunPlayer uses Qt's swapchain instead of this owner.
  Its [swapchain contract](https://github.com/haasn/libplacebo/blob/13d0b79827ea1671f991ca57700e5356af9731a5/src/include/libplacebo/swapchain.h#L67)
  permits asynchronous/ignored hints and requires rendering against the actual
  returned output interpretation.
* [Chromium, revision e882230b0e555b9cb9652d22bbec2dbd18a22970](https://chromium.googlesource.com/chromium/src/+/e882230b0e555b9cb9652d22bbec2dbd18a22970/components/metal_util/hdr_copier_layer.mm),
  explicitly chooses Apple's system mapping for PQ with `CAEDRMetadata` and
  custom mapping with nil metadata for HLG. Its custom path rerenders retained
  content when headroom changes. These are different mapping owners, not a
  reason to forward source mastering metadata after our own mapping.
* [KWin, revision b075cce99e846c48407ca73b54563c1193ead3a6](https://github.com/KDE/kwin/blob/b075cce99e846c48407ca73b54563c1193ead3a6/src/wayland/colormanagement_v1.cpp#L300),
  uses mastering/CLL peak information, otherwise the transfer maximum.
  Its [perceptual pipeline](https://github.com/KDE/kwin/blob/b075cce99e846c48407ca73b54563c1193ead3a6/src/core/colorpipeline.cpp#L41)
  derives an input range from that volume and inserts tone mapping when its
  transformed peak exceeds the output. Therefore omitted metadata can make
  already mapped PQ content undergo mapping based on a 10000-nit envelope;
  correctly describing the output peak can avoid that extra compression.
  This audits real implementation behavior, not SunPlayer runtime on KWin.
* [Mutter 50.1](https://github.com/GNOME/mutter/blob/50.1/clutter/clutter/clutter-color-state-params.c#L564)
  disables this tone-mapping stage for HDR output but compares declared peak
  to reference ratios for SDR output. The recorded baseline does not support
  mastering requests, so SunPlayer already omits them there. Do not claim all
  compositors apply the same mapping or accept the same hints.

### Qt replacement and black-level limits

Checked Qt 6.10.2, 6.11.1 and the current development source. Their Wayland
declaration builder supplies primaries and transfer, without explicit
luminances/mastering. It waits for readiness internally; creation failure logs
an unimplemented-recovery TODO rather than exposing a public application result.
[Qt 6.10.2 source](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandcolormanagement.cpp).
The current development check is an observation, not a stable dependency pin.

The [window implementation](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandwindow.cpp#L254)
initializes the declaration from the requested format when the native surface
is created; its runtime `setFormat()` preserves the established color space.
Thus Qt can own a fixed PQ declaration, but this does not yet replace our
same-surface PQ/SDR fallback or changing display-primary SDR declaration.

The protocol defaults are PQ `(0.005,10000,203)` and ordinary SDR `(0.2,80,80)`.
Its [normative transfer appendix](https://github.com/wayland-mirror/wayland-protocols/blob/1.47/staging/color-management/appendix.md#st2084_pq)
uses `L = 10000 * O + B` for PQ. Explicit zero changes that interpretation;
it is not a monitor-black measurement. It also does not establish visible
raised black by itself: downstream black-point adaptation matters. mpv's
explicit zero is useful production precedent, not proof the baseline requires
SunPlayer's override. No Qt-owned versus custom-owned native A/B was run.

Using a description before it is ready is a protocol violation. That lifecycle
rule does not justify custom ownership when Qt can own it. Display capability
observation can remain eventually consistent. Actual encoding changes must
still submit pixels with the matching declaration; do not turn this into a
new display epoch, generation system, or transaction framework.

### Minimal-code conclusion

Keep libplacebo mapping shared and native encoding at presentation. Do not add
optional metadata to Windows/macOS merely for symmetry. Do not delete Wayland
output hints on the unsupported inference that app mapping makes them useless.
Prefer Qt ownership when it can express the required interpretation and native
changes; the current writer's layout/state is not a design requirement.
Decide any writer replacement or hint deletion against the concrete API gaps
and compositor behavior above, separately from the shared mapper refactor.

This is source/API research and plan review only. It provides no new native
runtime, physical-display, or black-fix validation. Three independent reviewers
examined native API correctness, actual Wayland/production behavior, and
simplicity/history; their substantive findings informed this reassessment.

## Prior review record

Before consolidation, three independent reviewers completed two rounds covering
minimal-code architecture, color/metadata correctness, and tests/evidence/scope.
Their accepted corrections included the common SDR/HLG/Lab operator selector,
source-derived metadata authority, public target inference, render-local HLG
convention, fixed Lab reference, inferred PRE_OUTPUT endpoints, replacement of
obsolete tests, and the durable probe recipe above. Those reviews do not prove
the remaining normalization equation or native output correctness.

## Research read in chronological order


Every line of the following related notes was read. Dates order the history;
they do not rank its correctness. Some files retain later amendments internally.

* July 29: [SDR fixture](2026-07-29-compressed-sdr-fixture.md),
  [FFmpeg/import](2026-07-29-ffmpeg-windows-dependency-and-frame-import.md),
  [libplacebo build](2026-07-29-libplacebo-windows-dependency-build.md).
* July 30: [reference-white mapping](2026-07-30-reference-white-adaptive-hdr-mapping.md).
* August 1: [color review](2026-08-01-color.md),
  [display migration reconciliation](2026-08-01-display-audio-migration-reconciliation.md),
  [subtitle composition](2026-08-01-embedded-subtitle-pipeline.md),
  [HDR fixtures/HLG](2026-08-01-hdr-input-fixtures-and-hlg.md),
  [pinned-source verification](2026-08-01-pinned-color-source-verification.md),
  [Linux baseline](2026-08-01-ubuntu-26-04-linux-platform-baseline.md),
  [production display migration](2026-08-01-video-audio-switch.md).
* August 2: [Metal EDR transitions](2026-08-02-qt-metal-edr-screen-transition.md),
  [dialog/native-window ownership](2026-08-02-qt-quick-native-dialog-parenting.md),
  [Wayland window/decorations](2026-08-02-qt-wayland-vulkan-window-decorations.md),
  [redirected composition/depth](2026-08-02-redirected-qt-quick-depth-and-window-outline.md).
* August 3: [Wayland HDR10](2026-08-03-wayland-hdr10-presentation.md).
* August 9: [Windows startup black](2026-08-09-windows-first-presentation-background.md).
* August 19: [gamut and platform color semantics](2026-08-19-colors.md).
* August 21: [metadata-first mapping](2026-08-21-metadata-first-hdr-to-sdr-mapping.md).
* August 22: [reference-white regression](2026-08-22-reference-white-hdr-regression.md).
* August 23: [D3D11VA edge color corruption](2026-08-23-windows-d3d11va-edge-corruption.md).
* August 26: [SDR black and reference policy](2026-08-26-sdr-black-and-hdr-reference-policy.md).
* September 20: [HDR10+ comparison](2026-09-20-hdr10plus-external-playback-comparison.md),
  [mapper comparison](2026-09-20-sdr-mapper-comparison.md).
* October 17: [VAAPI/import](2026-10-17-linux-vaapi-drm-prime.md),
  [control contrast](2026-10-17-qt-control-contrast.md),
  [Wayland CSD/HDR transport](2026-10-17-wayland-csd-shadows-and-rounding.md),
  [optional mastering](2026-10-17-wayland-optional-hdr-mastering.md),
  [WCG SDR](2026-10-17-wayland-wcg-sdr.md).

Additional implementation/source checks:

* Current `PresentationTarget`, `PresentationSurfaceContract`,
  `RhiPresentationEngine`, `LibplaceboColorPolicy`, `LibplaceboRenderContext`,
  decoded producer, surface description, and final compositor shader.
* ADRs 0026, 0027, and 0029 for subsequent changes not fully reflected in the
  older research narratives.
* [Pinned libplacebo colorspace/inference](https://github.com/haasn/libplacebo/blob/v7.360.0/src/colorspace.c)
  and [shader conversion](https://github.com/haasn/libplacebo/blob/v7.360.0/src/shaders/colorspace.c).
* Inspected Qt 6.11.1 [Windows HDR detection](https://github.com/qt/qtbase/blob/v6.11.1/src/gui/rhi/qdxgihdrinfo.cpp),
  [D3D11 swapchain gate](https://github.com/qt/qtbase/blob/v6.11.1/src/gui/rhi/qrhid3d11.cpp),
  and [Metal swapchain gate](https://github.com/qt/qtbase/blob/v6.11.1/src/gui/rhi/qrhimetal.mm),
  alongside the current native providers and swapchain selector.
* Qt 6.11.1 [Cocoa color-space initialization, inheritance, and Metal-layer propagation](https://github.com/qt/qtbase/blob/v6.11.1/src/plugins/platforms/cocoa/qnsview_drawing.mm),
  and Apple's [Metal-layer color-space contract](https://developer.apple.com/documentation/quartzcore/cametallayer/colorspace).
* [Windows Advanced Color and signed scRGB](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range).
* [Wayland color-management image descriptions and extended coordinates](https://wayland.app/protocols/color-management-v1).
* [ICC sRGB primaries and transfer function](https://registry.color.org/rgb-registry/srgb).


## Implementation outcome (2026-10-22)

The accepted implementation uses one black-relative working convention:
`q=(203*x-B)/(C-B)`. The PRE_OUTPUT hook obtains nominal target B from
libplacebo; C is R, W or 203 according to the existing source-reference rule.
Unknown LINEAR target minimum is inferred consistently before rendering. Known
positive minimum remains mapping metadata, then its modeled light floor is
removed when converting into composition coordinates. No test-only black fix,
forced-zero target, second tone mapper or conditional platform normalization
was introduced.

The original scale-only producer could turn source black into about 0.0025.
The real-GPU regression now sends a zero PQ patch plus near-black values through
the production decoded path with unknown, zero and 0.2-nit targets, H=1,
nextafter(1) and 4, and R=100/203. Source black remains within 1e-5 of zero.
An independent libplacebo renderer emitting complete sRGB (without our hook)
confirms the production working result after inverse sRGB transfer. A separate
LINEAR library oracle checks positive-minimum Absolute PQ. Absolute uses the
same common coordinates; its native nominal light is W*(L-B)/(W-B), not exact
unadjusted L when B>0.

Resource-free libplacebo transfer functions are generated at build time and
baked into the existing QRhi pass. An extra pl_dispatch pass was rejected on
actual integration cost: QRhi owns the composed texture, and Metal additionally
crosses Vulkan. The selected route adds no runtime compiler, GPU handoff or
second renderer. Independent analytic captures cover all four output encodings,
P3 D65/D50, signed and bright wide colors, native white scaling and UI/subtitles.

Three independent Astra/xhigh implementation reviewers covered math,
native lifecycle and simplicity/encoding. Final test review fixed a native-test
capability assumption. Linux Debug 41/41 CTests, Release build and QML lint pass;
the final GPU executable rerun passes 40 cases with six expected skips. The
host's managed native controller is exercised separately on a hidden surface:
real async ready/assignment, synthetic display/generation/rejection inputs,
three passes including setup/teardown. No swapchain buffers or physical movement
are tested by that controller scenario. Windows/macOS execution and physical
appearance remain unverified in this delivery. Exact commands, scope and limits
are recorded in the [delivery plan](../plans/video-rendering/2026-10-21-color-pipeline-simplification.md)
and [testing guide](../TESTING.md).
