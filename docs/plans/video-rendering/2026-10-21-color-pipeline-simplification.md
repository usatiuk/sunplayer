# Simplify the shared color pipeline

Created: 2026-10-21; consolidated and discussion digested: 2026-10-22.
Status: implemented and locally validated (2026-10-22); native coverage limits recorded below.

## Design in one sentence

**Let libplacebo map video to one usable target, combine it with UI in common
signed linear coordinates, then use libplacebo to convert the composition into
the selected native surface encoding.**

Prefer the integration with the least code to own and maintain. A separate
final GPU pass is acceptable; one pass is not an architectural requirement.
Platform code observes display facts and declares/presents the resulting image.
The product goal is to avoid a second perceptual tone/gamut mapping while the
OS handles the required display color conversion and calibration. Current
native contracts do not guarantee that goal everywhere; keep those limits
explicit rather than compensating for them with another video policy.

The [native-contract research](../../research/2026-10-22-native-color-presentation-contracts.md)
contains definitions, platform APIs, compositor behavior and alternatives. The
[investigation record](../../research/2026-10-22-color-pipeline-simplification-evidence.md)
preserves the chronological reading, source audit, probes and historical reviews.

## Scope and independent follow-up

Simplify shared mapping, metadata/default resolution, coordinate conversion,
final encoding and overlapping target/render-description structures. Preserve
source features and preferences, libplacebo-backed HDR Lab, hardware import,
synchronization, resource recovery, paused-frame updates and existing native
output functionality. Existing layouts, names and pass boundaries are not sacred.

**Windows/macOS WCG SDR enablement remains out of scope.** No Qt gate workaround,
dependency patch, new P3 SDR output, custom swapchain, new ICC processing or
precision upgrade. Existing Linux WCG SDR remains supported. The procedural
QRhi diagnostic imposes no production design requirement; adapt or remove it
when simpler, without recreating libplacebo's operators in a diagnostic shader.

Linux now follows the usable output: managed SDR (including its existing WCG
support) on SDR outputs and PQ on HDR outputs. Reuse existing buffer/declaration
reconciliation, keeping protocol details private behind the optional native
controller. Windows/macOS retain their Qt presentation paths. This boundary
must serve the current Linux behavior and allow its removal without changing
the video mapper; no placeholder future platform implementation is needed.

Replacing the Wayland writer with Qt and adding other native mapping controls
remain separate follow-up. Preserve honest native limitations; shared math
alone cannot guarantee complete app-owned mapping. Implementation and validation
are authorized; commit, push and installation are not part of this request.

## The complete flow and what each step means

```text
retained video + coherent source metadata
    → libplacebo interpretation and mapping to the resolved usable target
    → library light coordinates converted to our working coordinates
    → linear composition with UI and subtitles
    → final native encoding, preferably using libplacebo's conversion APIs
    → matching surface declaration and native presentation
```

| Responsibility | Inputs and result |
| --- | --- |
| Interpret source | Retained final AVFrame/pl_frame and existing source preferences give one coherent representation, including applicable Dolby/HDR10+/HLG information. |
| Resolve target | Display observations and usable output capabilities give one gamut/white point, black/contrast and brightness range. Preserve unknowns and distinguish nominal descriptions from physical capability. |
| Map video | One shared libplacebo configuration fits video to that target. Source interpretation and artistic mapping end here. |
| Establish working coordinates | Convert the library result into the same black, white and RGB basis used by UI/subtitles before blending. |
| Compose | Combine layers in signed linear FP16. Do not apply a second tone mapper or video-only exposure correction. |
| Encode and present | Convert the complete composition's units/basis/transfer for the selected surface, then submit matching pixels and declaration. |

These are responsibilities, not six new classes or a processing graph. The
final encoder has no SDR/PQ/HLG/Dolby source branches. Source metadata describes
the incoming video; display information describes what is usable; the surface
declaration describes the pixels actually submitted. Passing more metadata is
useful only when its meaning and units are preserved across those boundaries.

## One target and one working representation

| Usable target | Gamut | H = peak / reference white |
| --- | --- | --- |
| sRGB SDR | sRGB/BT.709 | 1 |
| WCG SDR | Usable wider gamut | 1 |
| HDR | Usable gamut | Above 1 when extra brightness is available |

Zero extra stops means H=1, not H=0. Potential HDR capability does not create
current headroom. An extended-linear or PQ surface may have an H=1 target;
transport encoding alone must not select the mapper. Conversely, a physical
WCG display behind an sRGB-only path does not give us a usable WCG target.

Use **signed extended linear sRGB** for composition: BT.709/sRGB primaries,
D65 white point, linear transfer, FP16 storage and ordinary white at RGB 1.
sRGB and BT.709 share primaries; their usual nonlinear transfer conventions
differ. Negative coordinates represent colors outside the BT.709 triangle.
Multiplying a whole RGB triplet changes brightness while preserving its
chromaticity, so bright wide colors can have both negative and above-one
components. Preserve these through composition and destination-primary
conversion; clamp only where the selected final encoding requires it.

The target gamut restricts usable colors; it does not restrict the working
coordinate basis. The native declaration, not the sign or magnitude of a
pixel, tells the OS how to interpret that pixel.

## Reference white, source preference and black

Keep the following meanings distinct without adding a new metadata hierarchy:

| Quantity | Meaning |
| --- | --- |
| Working white = 1 | Our common ordinary-white anchor for video, UI and subtitles. |
| Native/display white W | The platform's current intended ordinary-white luminance when available. It is not a live optical measurement; relative SDR/EDR can work without absolute W. |
| Source-reference preference R = 100..203 | Existing decoded PQ/Dolby mapping preference. It does not measure or set the monitor's brightness. |
| Libplacebo linear unit = nits/203 | The library's light-coordinate convention, requiring translation into our working units. |
| Surface encoding units | For example Windows HDR scRGB 1 = 80 nominal nits, macOS EDR 1 = current SDR white, and our current Wayland PQ reference anchor = 203 nominal nits. |

Normal decoded PQ/Dolby uses nominal target peak R*H and coordinate white C=R. At H=1, the mapped ordinary-white endpoint at R becomes
working 1; an original source pixel at R is not guaranteed to map to 1 because
the curve can change it. Optional Absolute PQ instead uses the supported native
W coordinate; SDR/HLG and HDR Lab retain their relative convention. Keep these
preferences independent of the generic operator. The coordinate fix below
must preserve their intended meaning, not blindly preserve a scale-only bug.

PQ's equation maps nominal 10,000 nits to code 1. Nominal 203 nits encodes to
about 0.581; reference white is an anchor within that encoding, not its maximum.
Neither number is the maximum representable FP16 value or automatically the
monitor's brightness. The research explains reference adaptation and native
mapping separately.

A described SDR black of 0.2 nits means the model produces that light at its
black endpoint. It does not mean the application should send pixel code 0.2.
Libplacebo maps in light coordinates; its LINEAR output retains mapped black.
Its gamma/sRGB encoder also converts the target light range into pixel codes,
so modeled black becomes code zero. The previous LINEAR producer only scaled;
the probe recorded about 0.0025 at source black for a known 0.2-nit minimum.
Changing BT.2446A versus spline alone did not remove it. The implementation now
uses the same black-relative convention before composition.

The implemented relation is **q=(203*x-B)/(C-B)**, where x is libplacebo's
LINEAR output, B its inferred nominal target black, and C the ordinary-white
endpoint in the same units. C=R for decoded PQ/Dolby, C=W for supported Absolute
PQ, and C=203 for relative SDR/HLG and HDR Lab. The existing PRE_OUTPUT hook
obtains B from the public nominal-luminance API. No content-measured peak or
source-capped tone-map endpoint enters this conversion.

The same equation applies above SDR: P maps to `(P-B)/(C-B)`, slightly above P/C
when B>0. The native optional composition bound uses that working peak (capped
by transport capacity). For Absolute PQ, native scaling consequently presents
nominal `W*(L-B)/(W-B)`: positive-black normalization makes a small adjustment
to absolute mapped light, rather than preserving exact physical nits. This is
one convention across all sources, not another mode. B<C is validated before
constructing the working target. Tiny residuals are acceptable; no exact-zero
patch or second contrast engine is added.

Windows SDR looking better in a VM is not proof that Windows repairs this
conversion. Its unknown minimum and sRGB output differ from the recorded Linux
known-minimum/PQ path. Preserve that observation without claiming an unmeasured
cross-platform explanation.

## One generic tone operator and one metadata flow

Reuse the existing enum and libplacebo call:

```cpp
toneMapping = H == 1.0f ? Bt2446a : Spline;
```

Apply to SDR, PQ/Dolby, HLG and libplacebo-backed HDR Lab; let libplacebo decide
when mapping is a no-op. Retain supported source-authored ST 2094-40 guidance
and the explicit Lab mapping-disabled Clip control. Delete VideoRenderingMode,
usesAdaptiveHdrMapping and Clip-to-PL_HDR_METADATA_NONE coupling. Add no
platform exception, potential-HDR exception, new strategy type or setting.

* Validate external facts once. Preserve unknown, known zero and known positive
  minima. Delete the managed-SDR forced-known-zero mapping-target rewrite;
  retain genuine SDR capacity limits and native unit constraints.
* Preserve source ownership and coherent Dolby/HDR10+ representation and
  metadata selection. No parallel source parser or rewritten retained frame.
* Infer the constructed target once with public pl_color_space_infer. Unknown
  minimum enters as zero, known zero uses the existing effectively-zero
  sentinel, and positive minima use the established unit conversion. LINEAR
  inference gives nominal 1000:1 contrast independently of source/operator/H;
  this replaces the old unknown-HDR infinite-contrast assumption and is a
  library fallback, not a monitor measurement.
* Keep the render-local HLG source-peak convention 203*H at every H, independent
  of operator. HDR Lab's source reference stays 203. Preserve decoded R and
  optional Absolute PQ capability/preference; delete redundant mode gates.
* Normalize library coordinates once and native output units once. Do not
  cancel native white scaling in the producer or repeat black normalization
  after already normalized video has been mixed with UI.

## Libplacebo owns final conversion by default

Final conversion is bounded: native white-unit scaling, a primary-conversion
matrix when needed, and the declared transfer function. It must preserve the
prepared image rather than infer another artistic tone/gamut mapping policy.

Use libplacebo's narrow matrix and transfer APIs where they fit. In pinned
7.360.1, pl_shader_delinearize expects library light units, includes black/range
normalization for applicable SDR transfers, and preserves signed values only
for LINEAR. Supply a description of its actual input/output coordinates;
copying physical target metadata into this step can normalize black twice.
The generic pl_shader_color_map_ex with null state is not a reliable
encoding-only shortcut: its implementation can substitute simpler mapping
methods despite the public header's broader no-mapping claim. See the
[API analysis](../../research/2026-10-22-native-color-presentation-contracts.md#follow-up-final-encoding-and-separate-libplacebo-use).

The implementation generates the resource-free `pl_shader_delinearize` code
for sRGB, gamma 2.2 and PQ at build time and inserts it into the existing QRhi
compositor template. Qt's qsb bakes the result for the native backends. The
existing CPU libplacebo matrix API supplies primary conversion, including
709→2020. Linear output remains signed; nonlinear outputs clamp after the matrix.

This is one small generator, no runtime compiler, shader binding framework,
extra GPU pass, texture handoff or device ownership. A separate `pl_dispatch`
pass was considered: QRhi owns the composed image, so that option would require
a new QRhi-to-libplacebo handoff, especially across Metal/Vulkan. There is no
resource benefit here. UI sRGB decoding remains a separate input operation.
The synthetic final-encoder description is normalized black/white, not copied
physical display metadata, so it cannot repeat black normalization.

## A narrow native boundary, reusable across platforms

The manual Wayland communication can remain private. Refactor the existing
PresentationSurfaceContract/PresentationSurfaceController rather than adding a
native-presentation framework. Separate the resolved mapping target from the
selected pixel encoding. The shared presentation engine still owns rendering
resources; the native adapter selects a supported contract, prepares its
declaration and exposes whether that contract can be used.

Keep the custom controller optional. Linux implements it for the native work
needed today; Windows/macOS continue through their current Qt-owned path with
no custom or placeholder controller. Shared encoding semantics make later
native work reusable without implementing that future work now. This boundary
clarification does not change any platform's current HDR capability.

| Shared semantics needed by real consumers | Kept inside the native adapter |
| --- | --- |
| Selected primaries/white point, transfer and native white/unit scale, coupled to buffer representation and usable limits | Wayland protocol versions, feature bits, intent negotiation and request payloads |
| Current usable display/target observations, with unknown/nominal/physical meanings respected | Platform queries and their interpretation; do not expose raw protocol descriptions as physical display facts |
| Small ready/pending/unavailable result where native activation requires it | Description objects, ready identities, rejection/retry bookkeeping and native surface lifetime |
| Existing resolved composition facts when useful for supported output hints | Optional Wayland mastering/volume setters and their derived payloads |

These semantic values are a necessary contract, not Wayland leakage. Reuse the
existing primaries, output-encoding and scaling fields; merge/remove overlap
rather than introduce another source/target/surface schema. The old
PresentationSurfaceMode and usesAdaptiveHdrMapping are deleted. Shared output
encoding defines fixed primaries except for Gamma22, which carries its actual
SDR primaries. Wayland mode selection and rejection stay native-private.

Keep one declaration writer per native surface. Wayland descriptions must be
ready before use and their double-buffered assignment must accompany matching
pixels; these protocol requirements stay local. A shared pending result may
defer presentation without exporting protocol identities or dispatching native
events inside rendering. Display observations remain eventually consistent;
reconcile the latest semantic values at a safe boundary. Retain real device/
media identities, not new display epochs or a transition framework.

Optional hints describe the prepared composition, not original video mastering
metadata. They do not belong in the mapping target or a mandatory cross-platform
mastering tuple. The Wayland adapter can derive supported hints from existing
composition facts. Their usefulness is compositor-specific: checked KWin uses
them in mapping decisions, while the baseline Mutter lacks those setters.

### Existing paths and future reuse

| Path | Native interpretation after final conversion |
| --- | --- |
| Windows HDR | FP16 signed BT.709 scRGB; working white uses W/80 scaling. |
| macOS EDR | FP16 signed extended linear sRGB; 1 = current SDR white. |
| Linux managed HDR | RGB10A2 BT.2020/PQ with reference anchor 203, selected for a usable HDR output. |
| Linux managed SDR/WCG | Declared output primaries and gamma 2.2 on SDR outputs and as HDR capability/rejection fallback. |
| Ordinary SDR fallback | Matching SDR encoding/declaration where established; unmanaged fallback is assumed sRGB. Current macOS inherited declaration remains a known gap. |

Future Windows WCG scRGB or macOS tagged P3 SDR would reuse the same target
resolution with H=1, mapper, signed composition and final scale/matrix/transfer.
They would add native capability/declaration support, not another video policy
or Wayland-style metadata interface. No placeholder backend or future format
matrix is needed now. The native output can change on display movement when
required; shared mapping remains the same calculation with updated parameters.

### What relying more on Qt could remove

Qt already owns native swapchain presentation on our supported platforms. Its
checked Wayland implementation can also own a supported fixed color-space
declaration and its asynchronous readiness. A richer read-only feedback
observer can coexist with that writer. A successful Qt delegation could remove
our corresponding description creation, readiness, assignment and writer
lifetime code; it would not remove target observation, libplacebo mapping,
working-unit conversion or app encoding by itself.

The checked Qt 6.10.2/6.11.1 path is not a demonstrated full replacement for
current dynamic declarations: explicit luminances/output hints, public rejection
handling and runtime SDR-primary/PQ changes have gaps. QColorSpace::SRgb maps
to gamma 2.2 in that Wayland builder, so match the actual native interpretation.
Qt's requested QRhi format alone also does not prove the established format/tag.
Do not promise deleting the whole Linux window context: it also owns feedback,
Vulkan setup and window integration. Quantify deletable code only after choosing
an equivalent supported path, rather than inventing a line-count saving.

Evaluate Qt ownership separately and delete the custom writer where equivalent
behavior is demonstrated. Keep only the missing native operations. This does
not justify Qt patches, loss of existing Linux WCG behavior or new Windows/macOS
WCG work in this refactor. Detailed source/version evidence is in the
[native research](../../research/2026-10-22-native-color-presentation-contracts.md#11-wayland-explicit-descriptions-and-actual-compositor-behavior).

Checked Mutter adds a second HDR-to-SDR map to PQ-on-SDR input; the new SDR
selection avoids that path. HDR feedback may still describe an encoding
envelope rather than physical display limits.
Modern macOS has explicit tone-map ownership controls not currently set by Qt.
Those remaining native limits are follow-up before claiming complete app-owned
mapping. Changing the generic operator or final encoder cannot resolve them.

### Scope resolved: retain Linux WCG SDR

The user selected keeping Linux WCG SDR with a narrow removable adapter. Its current managed WCG SDR
buffer is already XRGB8888 on the baseline host: 8-bit precision and wide-gamut
color declaration are independent choices. Ordinary 8-bit sRGB SDR would cap
the usable SDR target to sRGB and remove changing display-primary SDR
declarations and their dedicated output-conversion branch. It would not remove
the shared signed working space, HDR gamut mapping, display feedback or the PQ
declaration/lifetime work needed by the retained HDR path.

Replace the current preference for PQ even on SDR with display-driven selection.
SDR targets retain matching SDR primaries/declarations; HDR targets use PQ when
available. Reuse existing transitions and failure handling. Keep HDR on all
platforms; Windows/macOS WCG SDR remains out of scope.

## Refactor by deleting overlap

Resolve one canonical mapping-target value and reuse it in rendering and
semantic reuse checks. Simplify PresentationTarget and
RenderedVideoSurfaceDescription together; remove repeated fields and manual
reconstruction. Native encoding/scaling belongs at presentation; source
preferences belong with source interpretation; diagnostics stay with their
owner. Fixed texture properties can be a contract instead of one-case enums.

Preserve retained-frame versus imported-view ownership, true graphics/media
lifetimes and actual rendering inputs. Remove superseded flags, mode branches
and handwritten conversion when the selected library integration replaces it.
Do not require new classes, mirrored snapshots or a generalized processing graph
merely to make the responsibilities look symmetrical.

## Delivery and acceptance

Implemented:

- Reuse one `VideoColorTarget` in presentation and the render description;
  delete mode gates, repeated field construction and one-case texture enums.
- Select BT.2446A at H=1 and spline above it; preserve authored HDR10+ mapping,
  source metadata ownership, source white preference and Absolute PQ selection.
- Infer target black consistently, then apply the common affine before UI
  composition. Preserve genuine known positive minimum instead of forcing zero.
- Replace custom output transfer formulas and the fixed PQ matrix with
  libplacebo-generated functions and its matrix API in the existing GPU pass.
- Keep the optional native controller with shared pixel semantics. Linux selects
  SDR/WCG or PQ from raw display observation; same-format gamut changes preserve
  the swapchain, encoding changes reuse queued recreation. Windows/macOS use Qt.

Validation covers source-zero and near-black at H=1, nextafter(1), H=4;
unknown/zero/positive minimum; R=100/203; retained SDR/PQ/HLG/HDR10+/Dolby input;
positive-minimum Absolute PQ; signed WCG and bright wide colors; all output
transfers; UI/subtitle composition; and retained-frame target updates.
The complete libplacebo SDR encoder is an independent boundary oracle. Native
declarations are tested through a hidden Wayland surface with real asynchronous
compositor responses. This is not a claim of physical display measurement or
real window movement across unlike monitors.

## Review and delivery evidence

Three independent Astra/xhigh implementation reviews covered math/source
metadata, native lifecycle/capabilities, and simplicity/final encoding. No
blocking implementation defect remained. Fixed findings: Windows PQ capture
needed the now-explicit BT.2020 matrix; HDR Lab's PQ diagnostic provenance was
incorrectly labeled relative SDR; duplicate unused capability fields and an
unused scaling constant were removed. Final review also caught a test assuming
PQ support implied arbitrary SDR primaries; the native scenario now checks both
actual contracts before running. A second test/math review found no further
actionable defects. Review requested stronger runtime
coverage; source-zero, full-library SDR oracle, Absolute PQ and native
asynchronous declaration scenarios were added to the existing integration test.

Delivery evidence, 2026-10-22:

- Complete Linux Debug build and Release player build pass; QML lint passes.
- Complete rebuilt Linux Debug CTest suite: **41/41 pass** (20.33 s), including
  resolved targets, surfaces, mapping, Wayland, real decoding, QML and application
  playback. Configuration/cache/data were isolated under `/tmp`.
- Real Intel GPU (`/dev/dri/renderD128`): final FFmpeg/GPU executable rerun
  after the test capability correction: **40 pass, 0 fail, 6 skip** (8.57 s)
  in private GL-rendered Weston. Five skips are foreign-platform paths; the
  managed native test skips on this compositor and is run separately below.
  Expanded black/encoding tests and complete SDR/Absolute PQ library oracles pass.
- Baseline color-management-v1 v2 compositor: hidden native declaration test
  **3/3 pass** including setup/teardown (0.28 s), covering PQ→WCG SDR→PQ,
  SDR gamut changes, pending-request replacement,
  same-contract fallback, independent rejection and new-device retry. Protocol
  trace confirms ready descriptions precede assignment. Display/generation
  inputs and rejection are synthetic; compositor readiness/assignment are real.
  No swapchain buffer is presented, no device is recreated, and no window is shown.
- Native Windows/macOS execution and physical unlike-display movement remain
  untested on this host. Shader baking and source review do not substitute for
  those measurements. Known Qt/native limitations remain in `docs/DEFERRED.md`.

Reproduction uses the configured native build trees:

```sh
cmake --build build/linux-debug -j6
cmake --build build/linux-release --target sunplayer -j6
cmake --build build/linux-debug --target sunplayer_qmllint
# With a private Weston GL socket and isolated XDG directories:
SUNPLAYER_REQUIRE_VAAPI=1 ctest --test-dir build/linux-debug --output-on-failure -j4
# On the baseline host compositor; this scenario never shows its window:
SUNPLAYER_REQUIRE_MANAGED_WAYLAND=1 WAYLAND_DEBUG=client \
  build/linux-debug/tests/sunplayer_ffmpeg_first_frame_tests nativeColorDeclarationTransitions
```

The unsigned Linux AppImage was rebuilt from this working tree using the cached
builder and smoke images, with no dependency installation. Packaged QML,
audio/video playback smoke and checksum verification pass. Artifact:
`SunPlayer-1.4.1-ubuntu26.04-x86_64.AppImage`, SHA-256
`9a3830a116abef843be9435a7365d5f46258767a4cae9ef934033b896b635cb0`.

The user subsequently reported that black levels appear fixed on their Linux
setup. This is a visual observation of the rebuilt app, not a calibrated
measurement or confirmation of every display/transport combination.

The user authorized a final blocker/correctness/simplicity review followed by
commit and push to main. Three fresh independent Astra/xhigh reviewers found
no introduced runtime, packaging, correctness or overengineering blockers. The
blocker review found stale graphics subsystem documentation describing permanent
PQ and the former normalization rules; those sections were corrected and the
reviewer confirmed the finding resolved. Final whitespace and formatting checks
pass. Only documentation changed during this final review, so the already-passing
build, GPU and packaging runs were not repeated. No dependencies were installed.
