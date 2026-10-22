# Color, HDR and native presentation: contracts and processing

Date: 2026-10-22. Status: research preceding the implementation in ADR 0039.
Historical descriptions of SunPlayer below refer to the pre-refactor code; the
delivery plan and subsystem README own current behavior.

This investigation answers the terminology, ownership, metadata, SDR/WCG/HDR,
platform and simplification questions raised during the color-pipeline review.
It starts from standards and native APIs; existing SunPlayer behavior and old
research are evidence to assess, not the definition of correctness.

**Product goal: libplacebo chooses the displayed image through tone and gamut
mapping; presentation preserves that choice while converting its coordinates
and units into the native display path.** Avoid another perceptual mapping pass
in the compositor wherever the native API permits it. Display characterization,
calibration, encoding and user-controlled display adjustments are separate
responsibilities and must not be confused with tone mapping.

The [shared simplification plan](../plans/video-rendering/2026-10-21-color-pipeline-simplification.md)
records the subsequent implementation. Windows/macOS WCG SDR enablement remains
outside its scope, although this research examines native alternatives. No
source changes, installations, visible player runs or physical measurements accompanied
this note.

## 1. Begin with three different meanings of a number

A pixel contains numbers. Their meaning comes from an agreed interpretation.
The number `1` by itself says neither a particular color nor a number of nits.
A surface here is the image our application submits for desktop composition,
including video, UI and subtitles. It is not the physical screen. Naming an
internal texture “BT.709” does not tell the compositor anything; the native
declaration communicates that choice.

| Location | Meaning of a neutral RGB value of 1 |
| --- | --- |
| Proposed shared linear working image | Our ordinary white, by application convention. |
| Ordinary normalized SDR image | The image's white endpoint; physical brightness depends on the display path and settings. |
| Windows HDR scRGB surface | Nominal 80 cd/m². |
| Windows Advanced Color SDR/WCG scRGB surface | The display's reference white. |
| macOS extended-linear EDR surface | Current SDR/UI white. |
| Zero-black PQ surface | Nominal 10,000 cd/m², by the PQ transfer function. |

“Neutral” means R=G=B; individual components of a colored pixel require the
declared RGB primaries as well. The Windows distinction is deliberate and
mode-dependent, even though the native format/color-space tuple can be the same.
See [Microsoft's white-level rules][win-white], [Apple's EDR introduction][apple-edr]
and the [Wayland transfer appendix][wl-appendix].

We choose a useful internal convention, such as working white=1. The final
adapter converts it into the convention required by the native surface. There
is no requirement that the stored numbers remain the same through every step.

## 2. A glossary with one meaning for each term

| Term | Meaning and purpose here |
| --- | --- |
| SDR | Standard dynamic range. In our target model, normal white is the neutral peak, H=1. This does not fix every SDR monitor to one physical brightness. |
| HDR | High dynamic range. A usable HDR target provides highlight range above normal white; source HDR and display HDR capability are separate facts. |
| WCG | Wide color gamut, here wider than ordinary bounded sRGB. It can exist with SDR or HDR brightness. |
| EDR | Apple's extended dynamic range representation/presentation model, expressed relative to current SDR white. |
| Surface / buffer / swapchain | The submitted image / storage holding its samples / the native sequence of presentation buffers. These do not inherently identify a color space. |
| Luminance, Y | A photometric measure of light from a surface in a direction. Units are cd/m², also called nits. “Luminosity” is too ambiguous for a pipeline field. |
| Brightness | Perceived intensity. It also depends on surroundings, adaptation and the image; it is not interchangeable with a measured luminance. |
| RGB coordinates | Three weights in a specified color basis. A different basis can represent the same color with different numbers. |
| Primaries | The red, green and blue chromaticities defining that RGB basis. |
| White point | The chromaticity of neutral white, such as D65. D65 describes color, not 65 nits, 203 nits or a brightness setting. |
| Gamut | Colors a representation or device can express under stated bounds. Distinguish an RGB basis from its ordinary bounded gamut. |
| Color volume | Color capability including the luminance dimension and bounds. A gamut triangle alone does not describe highlight capacity. |
| Transfer function | The rule between stored nonlinear signal values and linear-light values. It determines the meaning of code values, not which RGB primaries they use. |
| Linear light | Values proportional to the light quantity under the declared normalization. Doubling all components doubles that quantity. “Linear” alone does not define its unit or black offset. |
| Luma, Y′ | A weighted signal derived from nonlinear components in video coding. It is not the same quantity as linear-light luminance Y. |
| OETF / EOTF / OOTF | Respectively: scene light to encoded signal; encoded signal to display light; the overall scene-to-display rendering relationship. An inverse PQ EOTF is the encoding equation used below. |
| Gamma | Often used loosely for nonlinear encoding. A power law, piecewise sRGB and PQ are different functions; name the actual function. |
| Scene-referred / display-referred | Whether values describe scene light or an intended rendered display image. Some Windows API documentation uses these labels differently for native coordinate conventions; read its explicit numeric contract. |
| sRGB | A defined set of primaries, D65 and a nonlinear transfer function. “Linear sRGB” retains the primaries/white point and replaces the transfer with linear coordinates. |
| scRGB | An extended sRGB/BT.709-based representation. Windows' FP16 convention accepts signed linear values; its luminance interpretation depends on desktop mode as shown above. |
| BT.709 | A television recommendation. Its primaries coincide with sRGB; its complete signal conventions are not identical to sRGB. We must say “BT.709 primaries” when that is what we mean. |
| BT.2020 | Includes a wider set of RGB primaries. Those primaries can carry SDR or HDR. They do not by themselves mean PQ or HDR. |
| PQ / ST 2084 | A nonlinear encoding with fixed nominal luminance meaning. Normalized code 1 corresponds to 10,000 nits at the standard zero-black endpoint. |
| HLG | Another HDR system, with a relative scene signal and a display rendering relationship. It is not PQ with a different exponent or a fixed absolute-nit code table. |
| Reference white | An anchor within a particular image or display convention. Always qualify which image/display, its unit, and who chose it. It is not automatically the maximum. |
| Peak | A maximum of something specified: encoding capacity, prepared target, display capability, mastering display, actual content, or full-frame brightness. These are different quantities. |
| Headroom H | Usable peak/reference-white ratio for the selected output path. H=1 means no extra highlight range; extra stops are log₂(H), so zero extra stops means H=1. |
| Signal black | The image code intended to represent black. Often zero after range normalization. |
| Display black | Light actually emitted for black under given conditions. A nonzero physical floor does not imply the application should send a positive gray code. |
| Pixel format | Storage: channel widths, integer/float, normalized interpretation and alpha. It does not fully describe color. |
| Color-space declaration | The contract telling the receiver how to interpret a surface's pixels. APIs may bundle several conventions into a named preset. |
| Profile / characterization | A description of device response, potentially from measurements, manufacturer data or an approximation. A profile's presence does not prove it was measured. |
| ICC profile | A standardized color-profile representation used to describe color response and conversion. An embedded source profile and the display's destination profile have different roles. |
| Calibration | Adjusting device response toward a known condition. Calibration tables and profile-based color conversion are related, but distinct. |
| Compositor | The system component combining application surfaces into desktop output, generally also participating in color management. |
| Output | Must be qualified: application's submitted surface, compositor's display signal, or light emitted by the monitor. |
| Metadata | Any information about interpretation or content. Required encoding declarations, source mastering data and optional prepared-output hints are different categories. |
| Rendering intent | The selected rule for relating source and destination color volumes, such as perceptual or relative colorimetric. It can affect more than the RGB basis conversion. |
| Black-point compensation, BPC | Relates source and destination black/reference ranges during conversion. It is distinct from assigning an arbitrary positive value to video black. |
| Clipping | Truncating values that exceed a representable/usable boundary. It can lose detail and differs from a tone mapper deliberately redistributing that detail. |
| Quantization / dithering | Rounding to available code values / adding controlled variation to reduce visible rounding structure. Neither defines a color space or corrects a wrong transfer function. |
| Alpha / premultiplication | Coverage or opacity, and whether RGB has already been multiplied by it. Alpha is not luminance and must not receive the RGB white scale or PQ transfer. |

These definitions align with the [ICC sRGB characterization][icc-srgb],
[W3C color terminology and conversions][w3c-color] and [ITU HDR systems][bt2100].
The platform-specific meanings of reference white are specified in later
sections rather than hidden inside the word “HDR.”

HDR10 combines a defined HDR signal with static metadata conventions;
HDR10+ adds dynamic mapping guidance, and supported Dolby Vision paths add
their own representation/metadata processing. These source formats do not
require the application's already composed desktop surface to retain their
original metadata or compressed representation. HDR as a capability, PQ as
an encoding, and HDR10 as a content/delivery convention are different terms.

## 3. Why 203 nits becomes approximately 0.581 in PQ

PQ allocates its finite code values nonlinearly across a very large luminance
range. A linear allocation to 10,000 nits would spend too few integer levels
on dark values. Its equation is standardized in SMPTE ST 2084 and reproduced
in ITU-R BT.2100, Table 4. We do not choose the equation to make 203 become 1.
[Standard and equation][bt2100].

For nominal luminance L and zero black:

```text
y = (L / 10000)^(2610 / 16384)
PQ(L) = ((3424/4096 + (2413/128) * y) / (1 + (2392/128) * y))^(2523/32)
```

For RGB this encoding is applied to the linear components. The examples below
are neutral grays/whites, so equal RGB components also describe the stated
luminance; one component of a saturated color is not its total luminance.

Calculated directly from that equation:

| Nominal luminance | Normalized PQ code | Approximate full-range 10-bit RGB code |
| --- | ---: | ---: |
| 0 nits | approximately 0 | 0 |
| 80 nits | 0.485857 | 497 |
| 100 nits | 0.508078 | 520 |
| 203 nits | 0.580689 | 594 |
| 1,000 nits | 0.751827 | 769 |
| 10,000 nits | 1.000000 | 1023 |

The last column is a graphics-buffer example. A limited-range video signal uses
different integer endpoints; decoding its range is a separate earlier step.
The equation's tiny positive encoded value at mathematical black quantizes to
zero here. That does not justify relying on quantization to hide another
stage's material black error.

The 203-nit HDR reference-white recommendation comes from production guidance,
including ordinary diffuse/graphics white under its stated conditions. It is
not a requirement that every movie places white objects at 203, or that every
consumer monitor emits 203 nits for desktop white. [ITU-R BT.2408 §2][bt2408].

On a contract such as ordinary Wayland PQ, declaring reference white 203 tells
the compositor which point in that image is its normal-white anchor. Windows
DXGI PQ has no such separate reference-white field. The Wayland anchor does not rewrite the
PQ equation. The compositor can decode 0.580689 to nominal 203, then relate
that anchor to the desktop's chosen white. Additional tone mapping, where
enabled, is another operation. Native behavior is examined below.

The 10,000-nit endpoint is an encoding capacity. It is not the panel's peak,
the image's measured peak, or FP16's largest number. A finite half-float can
store numbers up to 65,504; attaching “nits” to one requires a separate unit
contract. RGB10A2 stores normalized 10-bit RGB and 2-bit alpha; it cannot store
negative RGB. A declaration of a smaller prepared peak does not change PQ(1),
nor does it necessarily establish a hardware clipping threshold.

## 4. Wide color in BT.709 coordinates, including bright wide color

An ordinary sRGB image usually restricts all three components to [0,1]. Those
bounds plus its primaries restrict the colors it can express. Signed floating
point removes that box constraint. A wider-gamut color can then have negative
BT.709 components without requiring negative emitted light from the monitor.

For example, converting linear Display P3 red to the linear BT.709 basis gives:

```text
P3 (1, 0, 0) = BT.709 (1.224940, -0.042057, -0.019638)
```

Both triplets describe the same color. The OS can convert the second back to
positive P3 values when the surface is correctly declared. Clamping the
negative values prematurely changes that color. This is ordinary linear
algebra, not a special negative-number flag recognized by the compositor.
The declaration identifies the basis. [Conversion definitions and sample
matrices][w3c-color].

Brightness and chromaticity remain separately controllable. Multiplying the
entire linear triplet by a positive factor preserves chromaticity and changes
luminance. Adding equal components generally moves the color toward white.
For a color containing all three P3 components:

```text
P3 (1, 0.2, 0.03) = BT.709 (1.179952, 0.166354, -0.002417)
three times brighter:
P3 (3, 0.6, 0.09) = BT.709 (3.539856, 0.499063, -0.007250)
```

The latter has luminance about 1.109 times the reference-white luminance in
this normalization and remains colorful. Whether the display can reproduce it
depends on its color volume. Merely checking Y against a peak is not a complete
color-volume test.

For Linux's bounded BT.2020/PQ transport, the final pass changes the RGB basis
from BT.709 to BT.2020 before PQ encoding. For Windows/macOS extended-linear
surfaces, the receiver accepts the signed BT.709 coordinates directly. A
matrix conversion preserves a representable color; gamut mapping changes an
unrepresentable color to a chosen reproducible alternative. The matrix belongs
at the final encoding boundary, not throughout the shared mapper.

SDR and WCG can therefore be carried inside a capable HDR representation. That
does not force the picture to use highlights or the entire transport gamut.
It also does not establish that every compositor will preserve such a surface
unchanged on an SDR output: the Wayland counterexample is concrete below.

## 5. The processes, their inputs and their purposes

| Process | Input → output | Purpose and intended owner |
| --- | --- | --- |
| Decode video | Compressed media → decoded sample planes and retained metadata | FFmpeg reconstructs samples and preserves source meaning. GPU decoding changes storage/import, not the color contract. |
| Interpret range and representation | YCbCr/RGB samples → correctly normalized source color | Respect limited/full range, matrix coefficients, chroma location, primaries and transfer. FFmpeg/libplacebo integration owns this once. |
| Source rendering interpretation | PQ/HLG/Dolby/HDR10+ source → coherent renderable source model | Honor the applicable source metadata and transformations. Avoid mixing incompatible metadata families. |
| Transfer decode | Nonlinear code → linear-light coordinates | Recover the light-related quantity encoded by the signal. This is not highlight compression. |
| Primary conversion / chromatic adaptation | One linear color basis/white point → another | Preserve the intended color under the selected white-adaptation convention. A D65-to-D65 basis change needs no white-point adaptation. |
| Tone mapping | Source luminance distribution → usable target range | Choose how shadows, midtones and highlights fit. Libplacebo owns this for our product. |
| Gamut mapping | Source colors → usable target color volume | Choose reproducible colors when exact colors do not fit. Libplacebo owns this for our product. |
| Unit/black-coordinate conversion | Library output convention → shared working convention | Make video, UI and subtitles mean the same thing numerically. It must follow an explicit equation; it is not another creative mapping pass. |
| Composition | Linear video, UI, subtitles and alpha → one image | Blend layers in the agreed working space. Keep signed color values intact. |
| Reference-white scaling | Relative working values → native brightness units | For example W/80 on Windows HDR. A uniform scale differs from nonlinear highlight compression. |
| Native encoding | Working basis/transfer → declared surface basis/transfer | For example BT.709 linear → BT.2020 → PQ. Perform once at the final edge. |
| Declaration | Native pixels + color description → interpretable surface | Tell the receiver what the buffer means. Correct storage alone is insufficient. |
| System color conversion | Declared surface color → display color coordinates | Translate the chosen result into the display's characterized color response. Managed native paths own it. |
| System tone/gamut mapping | Declared image volume → output volume | An additional image adjustment, depending on native API, intent and compositor. Avoid it for already mapped output where possible. |
| Calibration/output encoding | Display coordinates → calibrated display signal | Apply the installed device adjustment and encode/quantize the actual output connection. This is not necessarily the application's surface format. |
| Physical display response | Display signal → emitted light | Panel behavior, brightness setting and hardware processing determine the final light. |

These are logical responsibilities, not mandatory shader passes or classes.
Libraries and compositors can fuse or reorder compatible operations. Tone and
gamut mapping may be coupled inside a library; the table does not require us
to reimplement them separately.

```text
Source samples + source description
  → libplacebo interpretation and target mapping
  → signed linear working image + UI/subtitles
  → one final native encoding + matching declaration
  → system display conversion / any enabled system mapping
  → display signal → monitor light

OS/display observations → one usable target → libplacebo
```

“Display conversion and calibration” means translating our already chosen
color into the controls this monitor needs. For example, a chosen P3 red in
signed BT.709 coordinates must become the monitor's own channel values. A
profile can also describe channel nonlinearity or tint; calibration can correct
the response. Applying a monitor profile in the app and again in the system
can transform the image twice. Ordinary Windows SDR without Advanced Color
does not automatically provide this managed conversion.
Windows' distinction between automatic profile conversion and other display
calibration facilities is documented in its [ICC behavior][win-icc] and
[calibration pipeline][win-calibration].

Both transfer encoding and calibration can use nonlinear curves. That alone
does not make them content tone mapping: encoding changes how a selected light
value is stored, and calibration changes device controls to achieve its intended
response. Content tone mapping chooses a different representable brightness
relationship because the original image does not fit, or because the rendering
policy deliberately adapts its appearance.

An OS can additionally implement Night Light, accessibility filters or ambient
adaptation; hardware can impose local-dimming/power limits. These are not the
app's source tone mapper. “We own tone mapping” is a specific rendering
responsibility, not a promise to override every user or device adjustment.

## 6. Four descriptions that must stay distinct

| Description | Who supplies it? | What it describes / what it is used for |
| --- | --- | --- |
| Source content | File/decoder, sometimes a documented assumption | Meaning of source samples, mastering conditions, static/dynamic guidance. Used to interpret and map the video. |
| Usable target | OS observations constrained by the active presentation path | The volume we can use now. Used to choose the mapped image. It can be a compositor's preferred target rather than physical panel measurements. |
| Submitted surface | Application, Qt or WSI acting as its single declaration owner | Meaning of the composed buffer: basis, transfer and native luminance convention. Used by the compositor to read pixels. |
| Prepared-output volume hint | Application where the API supports/requires a useful hint | The volume for which this already processed composition is prepared. Can influence further system mapping; not necessarily the video's original mastering volume. |

An original 4,000-nit source mapped to a 600-nit target is no longer an
unmodified 4,000-nit presentation. Forwarding the source peak unchanged can
invite inappropriate additional mapping. Conversely, merely tagging the result
BT.2020/PQ leaves a 10,000-nit encoding envelope; it does not announce that the
result was already prepared for 600 nits.

Scanning the pixels does not solve that ambiguity. A dark frame's maximum
does not reveal the intended white anchor, the target for the next frame or
whether an artistic mapping was already applied. Metadata can describe intended
capacity rather than measured image statistics. APIs differ in how that
information affects their compositor; it is not universally mandatory or
universally useless.

The source reference-white preference, working white=1, Wayland surface
reference=203, Windows HDR scale=80 and monitor white are also separate
quantities. Source PQ gives absolute nominal luminances; it does not guarantee
every production's diffuse white equals one universal value. A player's
source-reference preference is a mapping policy, not a new PQ transfer curve.

The different maxima deserve explicit names:

| Maximum | Example / purpose |
| --- | --- |
| Encoding maximum | PQ's nominal 10,000-nit endpoint. |
| Mastering-display maximum | Capability of the display used to prepare the source; not proof a pixel reaches it. |
| MaxCLL | Reported maximum content light level. It is not the target monitor's peak. |
| MaxFALL | Reported maximum frame-average content light level; distinct from a display's full-frame capability. |
| Display peak / full-frame maximum | Device capability reports for different operating conditions; small highlights and full-screen white can have different limits. |
| Prepared-output maximum | Bound of the target volume for our processed composition, in that description's units. |
| Observed frame maximum | A statistic of one frame, not permission to expand it to fill the target. |

Microsoft separately documents [content metadata][win-hdr-fields] and
[display capability fields][win-output-desc]; Wayland distinguishes the
primary encoding volume from the target volume in its [protocol][wl-protocol].

### Direction of the information, on every platform

| Direction | Windows | macOS | Wayland |
| --- | --- | --- | --- |
| System → app: capability and current target | HWND-bound Advanced Color information, change events, DXGI fallback capabilities | Window's NSScreen, current/potential/reference EDR ratios, screen color space and notifications | Advertised protocol capabilities/intents plus surface preferred-description feedback |
| App → system: storage | DXGI swapchain format | Metal drawable pixel format | Vulkan/native buffer format; Wayland buffer attachment |
| App → system: pixel interpretation | DXGI color-space convention, explicitly selected as needed | CAMetalLayer color space and EDR request | Ready image description and required supported rendering intent attached with matching content; Qt/WSI/app has one writer role |
| App → system: processing controls/hints | Optional HDR metadata; unnecessary for the selected app-mapped path | CAEDRMetadata for system mapping; toneMapMode controls ownership | Optional prepared target/mastering volume |
| User/system → output: normal-white brightness | Desktop SDR brightness/HDR white settings and monitor controls | System/display brightness establishes relative EDR white and available H | Compositor's white/brightness policy and output settings |
| System → display | Desktop conversion, calibration and connection encoding | Core Animation/ColorSync and display output | Compositor conversion, output encoding and display configuration |

The app asks for capabilities and declares its own image. Those are two
directions, not one interchangeable metadata payload. None of these platforms
requires us to send the same universal black/white/peak tuple: named native
conventions can already fix some quantities. On Windows HDR, the 80-nit unit
is fixed and we encode white into pixel values. Apple's app-owned EDR path
uses relative white. Ordinary Wayland PQ descriptions define a reference anchor,
explicitly or by default, alongside PQ's fixed transfer meaning.

## 7. One numerical example through the native paths

Assume a hypothetical output with current SDR white W=200 nits, usable peak
P=800 nits and H=4. Use zero black to isolate the units. These are illustrative
values, not measurements of the user's displays, and not a claim that all
platforms expose W and P in physical units.

Let x be a neutral working RGB value after our mapping. x=1 is ordinary white;
x=4 is the chosen neutral peak.

| Working x | Windows HDR scRGB: x·W/80 | macOS relative EDR: x | Wayland PQ nominal luminance: x·203 | Wayland PQ stored code |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 0 | 0 | approximately 0 |
| 1 | 2.5 | 1 | 203 | 0.580689 |
| 2 | 5 | 2 | 406 | 0.654176 |
| 4 | 10 | 4 | 812 | 0.729145 |

The Windows adapter supplies the white scale. Apple's relative EDR contract
anchors 1 to current UI white. A Wayland reference-white adaptation can relate
nominal 203 to output white 200; in that simple adaptation, nominal 812 becomes
800. A prepared-volume description must use the surface description's units,
so 812/203 preserves H=4. Do not copy 800 from a different unit system blindly.

That Wayland arithmetic describes the reference adaptation, not a guarantee
against a compositor's additional tone mapping. The exact Mutter SDR branch
below is a counterexample to treating it as the complete pipeline. Its feedback
also does not supply the hypothetical physical 800-nit peak.

For SDR H=1, ordinary white is the target's neutral peak. An sRGB encoding of
working 1 becomes code 255 in an 8-bit full-range buffer; working 0.5 becomes
approximately 188, because sRGB is nonlinear. Neither number fixes a consumer
monitor's actual white brightness. A WCG SDR image has the same lack of extra
highlight headroom but can use a wider gamut when its presentation path allows.

## 8. The nine platform/output cases

“Native route” below describes available platform behavior. “Current route”
describes the audited SunPlayer/Qt path, without treating it as the desired
design. Display capability and actual usable output are separate.

| Platform and target | Native app-mapped route | Current Qt/SunPlayer consequence |
| --- | --- | --- |
| Windows sRGB SDR | Ordinary SDR sRGB buffer, H=1. With Advanced Color inactive, generic DirectX output is not automatically display-profile managed. | Existing 8-bit SDR path; no claim of complete display ICC correction. |
| Windows WCG SDR | Active Advanced Color can use signed FP16 scRGB with 1=display white, H=1; system converts to display gamut. | Qt 6.11.1's D3D11 FP16 eligibility requires active HDR, blocking the intended WCG-only route. App remains sRGB-limited. |
| Windows HDR | FP16 scRGB, 1=80 nominal nits; app maps target and scales white. | Existing supported extended-linear route, with target observation. |
| macOS sRGB SDR | Explicitly tagged sRGB SDR surface, H=1; ColorSync interprets its declared space. | Current SDR shader emits sRGB but does not explicitly request that native declaration; Qt can inherit the window profile. Matching is not established merely by calling it SDR. |
| macOS WCG SDR | Explicit P3 SDR is native color management; extended floating-point routes also exist independently of an 8-bit storage requirement. | Current selection and Qt FP16 support query require EDR potential. A pure WCG SDR display falls to the existing SDR path; no completed P3 SDR route. |
| macOS HDR/EDR | FP16 extended linear sRGB, 1=current SDR white; map to current H. Explicit system tone-mapping control is available on newer macOS. | Existing EDR route; potential capability selects it while current headroom drives mapping. Modern tone-map ownership needs a native follow-up. |
| Wayland sRGB SDR | Managed SDR declaration with matching primaries/transfer and H=1. Unmanaged assumed-sRGB remains a distinct weaker fallback. | Current preferred managed PQ transport can remain active on SDR. On checked Mutter it triggers another HDR-to-SDR mapping. |
| Wayland WCG SDR | Managed SDR declaration of usable wider primaries, matching final encoding, H=1. | Existing gamma-2.2/display-primary SDR path can carry WCG, but is currently selected as PQ capability/rejection fallback rather than by SDR capacity alone. |
| Wayland HDR | Supported PQ/BT.2020 or a genuinely supported extended-linear contract, with accurate declarations and target observations. | Baseline uses RGB10A2/PQ. Actual compositor capabilities and target semantics constrain how completely we can map to the physical display ourselves. |

An HDR-capable macOS screen with current H=1 is not equivalent to a screen
with no EDR potential: the former can retain its capable EDR surface. Likewise,
a narrow-gamut monitor can be under an active managed desktop mode. The table
classifies usable presentation, not just marketing labels on the monitor.

WCG does not inherently require FP16. FP16 is useful for preserving signed
working coordinates and precision; a correctly encoded/tagged bounded P3 SDR
surface also works. Eight-bit output may introduce visible banding. Internal
FP16 and final native buffer precision are independent decisions.

### Source type is a separate axis from the output

| Source → usable target | sRGB SDR target | WCG SDR target | HDR target |
| --- | --- | --- | --- |
| sRGB SDR content | Preserve its intended SDR rendering | Same SDR brightness; the surface need not invent wider colors | Place normal white at current SDR white; extra display headroom does not require invented highlights |
| WCG SDR content | Map colors that exceed sRGB | Preserve usable wider colors, H=1 | Preserve usable colors and SDR brightness; extra highlight range can remain unused |
| HDR content | Tone map into H=1 and fit sRGB | Tone map into H=1 while retaining usable wide colors | Fit the available brightness and color volume; source may still exceed this HDR display |

These responsibilities are shared across platforms. A particular native path
can reduce the usable target, as current Windows/macOS WCG-only gaps do. The
source file should not independently switch the native surface every time its
metadata says SDR or HDR; the surface must serve the composed output contract.

## 9. Windows: declarations, observations and alternatives

The application chooses DXGI storage and color space separately. The normal
advanced route uses `R16G16B16A16_FLOAT` with
`DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`. Its signed BT.709 coordinates carry
wide color; HDR mode gives the 80-nit unit, while Advanced Color SDR gives the
display-white unit. Microsoft's mode-specific white-level table is the
important authority here. [White-level adjustment][win-white].

The app observes `DisplayInformation`/`AdvancedColorInfo` for active mode,
primaries, white and range information, and listens for changes. HWND/HMONITOR
interop is documented from Windows 11 build 22621; the generic guide's statement
that desktop apps cannot access this information is stale. [Interop API][win-interop].
The SDK's active mode distinguishes SDR, WCG and HDR; physical wide-gamut
capability alone does not prove Advanced Color is active.

`AdvancedColorInfo.SdrWhiteLevelInNits` supplies the system's HDR-desktop SDR
white W used for our final W/80 conversion. The newer display-config SDK also
defines separate SDR/WCG/HDR mode values; its compile-time availability alone
does not establish every runtime version supporting that alternative query.
Neither finding requires a duplicate observer. [White query][win-white-query],
[SDK mode definitions][win-sdk-mode].

For the app-owned route, Microsoft recommends mapping to display capabilities
instead of relying on `SetHDRMetaData`. `SetColorSpace1` is still required to
establish interpretation when changing from the default. Optional HDR10
mastering metadata does not itself define the pixel format or replace the
color-space declaration. [Microsoft's metadata guidance][win-metadata].

The DXGI enum's `G22` spelling is also not permission to replace the documented
piecewise sRGB function with arbitrary gamma 2.2; use the actual definition of
the chosen preset. [DXGI color-space definitions][win-color-enum].

Windows' documented Advanced Color output includes color conversion and
clipping when submitted values exceed the display. It should not be treated
as an implicit artistic HDR-to-SDR mapper for arbitrary application surfaces.
Applications can instead explicitly use Microsoft's Direct2D HDR tone-map
effect or the supported media-processing pipeline; then that component owns
the conversion. HDR10/PQ swapchains are another native transport, with different
restrictions and metadata/monitor behavior. These alternatives do not improve
our goal merely by moving code out of libplacebo. [Advanced Color guide][win-hdr],
[Direct2D HDR tone-map effect][win-tonemap].

Windows PQ can also carry our app-mapped result: in the numerical example,
encode physical-model W·x rather than Wayland's 203·x. Native white 200 and peak
800 become approximately PQ 0.579133 and 0.727525. Choosing this transport
does not inherently request system tone mapping or require HDR metadata.

Window movement or mode/white changes require two distinct responses: refresh
the usable mapping target and rerender retained video when it changes; change
native resources/declarations when their contract requires it. The OS handles
desktop routing and display conversion. It does not automatically regenerate
our already mapped video texture. Qt creates the swapchain we request within
its supported presets, rather than selecting the complete application policy.

Qt 6.11.1 checks active HDR for FP16 support and creation. Its D3D11 code can
also fall back from a requested HDR format, and warns on `SetColorSpace1`
failure. The requested QRhi format enum alone is consequently not independent
proof of the established native contract. These are native boundary limits,
not a reason to add a shared color-policy fork. [Qt D3D11][qt-d3d],
[Qt HDR detection][qt-dxgi].

## 10. macOS: relative EDR and explicit mapping ownership

Apple documents app-owned rendering using `CAMetalLayer` with
`RGBA16Float`, `extendedLinearSRGB` and
`wantsExtendedDynamicRangeContent = true`. Ordinary white is 1. Current
`NSScreen.maximumExtendedDynamicRangeColorComponentValue` supplies H; potential
headroom describes capability, not what is usable at this instant. Brightness,
display changes and system state can change H. [Apple's own-mapping route][apple-own].

Current headroom can initially be one when no onscreen content requests EDR;
potential headroom avoids confusing that state with lack of support. A third
AppKit value, maximum reference EDR headroom, concerns supported reference
rendering and can be zero when unavailable. It is not another current physical
peak measurement. [Potential headroom][apple-potential],
[reference rendering range][apple-reference].

The app requests the layer's interpretation; ColorSync converts it to the
screen's color space. `NSScreen.colorSpace` describes that screen. AppKit's
headroom ratio does not by itself reveal physical white in nits, and we do not
need to invent a physical white to use the relative EDR route. An H of 4 means
four times current SDR white, not necessarily 800 nits. [Layer color space][apple-space],
[current headroom][apple-headroom].

Apple's `CAEDRMetadata` contract says nil disables that optional system
tone-mapping mechanism; values beyond current H may be clipped. macOS 15+
also exposes `CALayer.toneMapMode`, inherited by `CAMetalLayer`, with
`automatic`, `never` and `ifSupported`. For an explicit app-owned contract,
`never` is the relevant public setting where available; nil metadata alone is
too broad a statement about all modern mechanisms. WebKit explicitly chooses
`never` for already painted content to avoid a second mapping. This is an API
finding, not evidence that our current Qt layer has visibly remapped a frame.
[EDR metadata][apple-metadata], [tone-map mode][apple-tonemode],
[WebKit implementation][webkit-tonemode].

This setting controls tone mapping. ColorSync still performs necessary display
conversion; the public Metal-layer contract exposes no separate guarantee
against gamut handling. Our gamut mapper should therefore target a supported
displayable volume rather than rely on arbitrary out-of-gamut values surviving.

The system-owned alternative supplies `CAEDRMetadata` describing the source
and an optical output scale in nits per unit. Core Animation can then map that
content to available headroom. Source interpretation, declared color space and
this metadata must agree; post-mapping video must not be described as the
original unmodified HDR source. [Apple's system-mapping route][apple-system].

For example, `opticalOutputScale = 100` says submitted linear 1 represents
100 reference-display nits before system mapping. It is not a measurement or
setting of the user's current screen white. [Parameter contract][apple-optical].

macOS 26 also exposes layer headroom/dynamic-range controls. These are distinct
from a Metal drawable's pixel format and color space. A generic CALayer API's
deprecation must not be assumed to deprecate CAMetalLayer's own EDR property.
Do not add new properties merely for symmetry or because they exist; choose
one coherent documented ownership contract.

Native P3 SDR is an ordinary available color-managed path. Qt Cocoa propagates
a requested color space to its Metal layer, while QRhi's FP16 support query
requires EDR potential. Thus the WCG-only gap is in our selected Qt route and
wiring, not a fundamental inability of macOS to display WCG without HDR.
The current fallback also needs an explicitly matching declaration; inheriting
a P3 window space while writing sRGB values would change colors. No such native
repair is implemented in this research. [Qt Metal][qt-metal],
[Qt Cocoa color-space propagation][qt-cocoa].

## 11. Wayland: explicit descriptions and actual compositor behavior

### What is exchanged

The compositor advertises supported features, transfer functions, primaries
and rendering intents. Surface feedback supplies a preferred image description;
the app observes it and changes its target when the semantic values change.
The app then creates a supported description of its own pixels, waits for the
protocol's ready result, and attaches that description to matching buffer
content. A declaration is not a request to set monitor brightness. Preferred
feedback is not guaranteed to be a raw EDID measurement.
[Protocol][wl-protocol].

The encoding primaries and transfer define how to interpret the image.
Both are required in the parametric description. Explicit luminances and
mastering fields are optional, capability-gated additions with specified
defaults; they are not universal protocol prerequisites for HDR.
Primary luminances include black and reference white; for PQ, the transfer
itself fixes the 10,000-nit span. Target/mastering fields can describe a smaller
prepared volume. Omitted target fields fall back to the primary volume. For
PQ, the default black/reference values are 0.005/203; an explicit zero black
changes that description, not the measured panel floor. The appendix defines
how these parameters enter transfer decoding. [Transfer appendix][wl-appendix].

For our existing explicit zero-black PQ convention, normal white is encoded at
PQ(203). The compositor can anchor it to its output reference white. Different
descriptions' references establish the white relationship; a rendering intent
can additionally alter the image to fit the target volume.

### Mutter 50.1: a definite extra mapping on SDR

The checked baseline is Mutter 50.1, commit
`8e9bb806da4a6a4fc87aa4a481b228dba6735cbd`. Its color-management implementation
accepts perceptual intent for these surfaces and does not expose the client
mastering setters needed to describe a smaller PQ target volume. Thus ordinary
PQ is treated as having nominal peak 10,000 and reference 203.
[Wayland implementation][mutter-wl].

Its shader decision is explicit: this tone-mapping stage is disabled for an
HDR destination whose mastering maximum exceeds its reference. For an SDR
destination, a higher source peak/reference ratio invokes tone mapping. It is
not a guess based on whether the current pixels happen to contain highlights.
[Decision and shader][mutter-color].

For neutral input luminance L, source reference S=203 and source peak Q=10000,
the SDR branch uses destination peak P and anchor T=P/1.5:

```text
L < S:   result = L * T/S
L >= S:  t = (L-S)/(Q-S)
         result = T + (P-T) * 5*t/(4*t+1)
```

At L=S, the result is T=2P/3. The ordinary reference-white scaling stage is
skipped when this tone-mapping stage applies. Therefore “203 always becomes
the desktop's SDR white through one uniform scale” is false for this path.
Already mapping video to SDR in the app does not remove the second transform
while the surface still declares that large PQ volume.

A matching SDR description with source maximum equal to reference avoids this
HDR-to-SDR branch. Relative/BPC intent cannot simply be selected on this
baseline, because that surface implementation supports only perceptual. This
is concrete evidence for reconsidering stable PQ across SDR/HDR movement when
app-owned mapping is the goal. It does not by itself prove the gray-black fix.

Another limit is feedback: the checked monitor color-state construction uses
the transfer's nominal maximum, and Wayland information reports that maximum
as target maximum too. PQ 10,000 and BT.2020 can therefore describe the
compositor's preferred envelope, not actual panel peak and gamut. We cannot
claim to map exactly to the physical monitor by treating those fields as
measurements. [Monitor color-state construction][mutter-device],
[feedback serialization][mutter-wl].

### KWin: prepared-output metadata has an actual purpose

The checked KWin revision
`b075cce99e846c48407ca73b54563c1193ead3a6` uses content/mastering peak information
when provided, otherwise transfer maximum. Its perceptual pipeline compares
the transformed declared range with the output and can add tone mapping.
Accurate prepared-output metadata can therefore avoid compression triggered
by an unnecessarily large 10,000-nit envelope. This is why app mapping does
not universally make outgoing Wayland metadata redundant.
[Description construction][kwin-wl], [mapping decision][kwin-pipeline].

The request remains a description, not a universal “disable compositor mapping”
switch. Rendering intents and compositor behavior still matter. A colorimetric
intent/BPC path, where supported, is a different contract from perceptual
adaptation. Krita documents that distinction for calibrated editing; its
recommended editor workflow is useful evidence, not an automatic video-player
policy. [Krita color management][krita-color].

### Qt, WSI and alternatives

Qt 6.10.2 and 6.11.1 can create basic Wayland PQ descriptions. They send
primaries and transfer, choose perceptual intent and handle description
readiness internally. They do not supply our explicit luminances/mastering
values. `QWaylandWindow::setFormat()` retains the established color space;
public runtime retagging and rejection recovery are not a proven drop-in
replacement for our existing changing SDR/PQ declarations.
[Qt declaration builder][qt-wayland], [Qt window lifecycle][qt-window].

A separate version-2 read-only feedback observer can coexist with Qt's
version-1 declaration writer. The restriction is one surface declaration
owner, not one observer. Qt's Vulkan `PASS_THROUGH` selection prevents the WSI
from independently declaring the image; it does not disable the compositor's
color processing. [Qt Vulkan swapchain][qt-vulkan].

Extended linear BT.709 is expressible through Wayland mechanisms, but named
`ext_linear`, extended target-volume capability and Windows-scRGB creation are
different contracts. They do not all imply 1=80 nits or permit the same
extended values. Native support must match the actual Vulkan/QRhi format and
color-space pair. The baseline's supported PQ pair does not prove its required
extended-linear pair is available. Newer protocol version 3's Windows BT.2100
creator likewise does not retroactively change ordinary PQ semantics.

Specifically, ordinary `ext_linear` follows its declared min/max range and
reference, while the separately advertised Windows-scRGB creator fixes a
nominal 80-nit unit and permits signed extended values. Mesa requires extended
target-volume support for its `EXTENDED_SRGB_LINEAR_EXT` route. Merely seeing
`ext_linear` in an advertised transfer list is insufficient. Internal FP16
composition has none of these Wayland transport restrictions.
[Version-3 protocol snapshot][wl-current], [Mesa WSI][mesa-wayland].

The relevant alternatives are: match the preferred SDR/HDR encoding; retain
PQ with an accurate prepared volume and suitable supported intent; use an
available extended-linear native contract; or deliberately let the compositor
map source-described content. They have different ownership and capability
requirements. A fixed PQ transport is convenient, but convenience alone does
not prove it meets our app-owned-mapping requirement.

## 12. Black, 0.2 nits, and what the prior experiment proves

The nominal sRGB reference conditions include white 80 and black 0.2 nits.
These are standard reference-model values, not measurements of every SDR
monitor. A compositor can legitimately report such a model. The error would
be assigning a different meaning to it later in the pipeline.
[ICC reference medium][icc-srgb].

The previous production-boundary probe held source/target metadata constant
and changed only the old SDR/adaptive rendering mode. A known 0.2-nit target
minimum produced approximately 0.00249672 for source zero in both modes. That
isolates an app-side coordinate issue from the additional native behavior
discovered above. It does not prove which one-line normalization is correct
for every native contract. The raw evidence and recipe are retained in the
[earlier investigation](2026-10-22-color-pipeline-simplification-evidence.md#new-controlled-evidence).

Libplacebo's linear result can represent mapped luminance including target
black, while our composition expects its own signal coordinates. A correct
conversion must account for this difference. For a relative SDR model with
black B and white W, the inverse relationship is `(L-B)/(W-B)`. At HDR peak P,
that yields `(P-B)/(W-B)`, which is not exactly P/W. Use the intended native
contract to choose and test the conversion rather than demanding mathematically
incompatible anchors or forcing metadata to zero.

The observed lift is not comparable to a harmless 0.000001 residual. In a
simple sRGB encoding, linear 0.0025 becomes 0.0323, about eight 8-bit levels;
linear 0.000001 becomes only about 0.003 of one 8-bit level. These are calculated
scale comparisons, not a prediction of every HDR screenshot or panel response.

“Windows looked better in a VM” is useful observation, but does not isolate
the cause: its target minimum, active output, mapper inputs and compositor path
can differ. Neither a rendering-policy switch nor a screenshot alone proves
the source-to-surface black equation. The final fix remains unimplemented.

## 13. Own mapping and system mapping: the alternatives aligned

| Path | App's responsibility | System's responsibility | Fit for our goal |
| --- | --- | --- | --- |
| Windows app-mapped FP16 | Interpret source, fit available range/gamut, apply native white units, declare scRGB | Convert/calibrate through Advanced Color; clip out-of-range values | Main HDR route. Native WCG SDR route exists, Qt eligibility blocks current use. |
| Windows explicit system processing | Supply suitable source to D2D/media processing, then present in its documented output convention | Selected component performs HDR/SDR conversion | Alternative ownership, not our selected libplacebo policy. |
| Windows HDR10/PQ | Encode/tag PQ and satisfy route restrictions; metadata behavior is separately specified | Desktop/display handling depends on that route | Available alternative, not an automatic simplification or reliable replacement for own mapping. |
| macOS app-mapped EDR | Fit current H/gamut, declare extended-linear space, explicitly avoid further tone mapping where supported | ColorSync/display adaptation; native relative-white behavior; clipping at limits | Main native contract for our goal. |
| macOS system-mapped video | Supply coherent source color, optical scale and CAEDRMetadata | Core Animation maps to current headroom and display | Useful alternative; do not use original-source metadata after our own mapping. |
| Wayland matching managed SDR | Fit H=1/gamut, encode and declare matching SDR | Declared color conversion and output adaptation | Avoids the demonstrated Mutter PQ→SDR mapping stage. |
| Wayland app-mapped HDR | Fit the usable known target, encode/tag supported HDR, provide prepared volume where useful and supported | Behavior follows intent and implementation | Must assess concrete compositor limits; no cross-compositor no-mapping guarantee follows from PQ alone. |
| Wayland compositor-mapped | Declare a coherent source/combined image volume rather than claiming it was already display-mapped | Compositor chooses mapping under its supported contract | A general alternative, not a verified physical-target solution on baseline Mutter, and it relinquishes our chosen tone-mapping policy. |
| Unmanaged SDR | Emit assumed-sRGB and constrain usable range/gamut | No promised complete profile conversion | Existing fallback with explicitly weaker color guarantees. |

Production projects use more than one route. mpv can map to a negotiated target
and send target-based output hints; its Wayland implementation explicitly sets
PQ luminances and optional mastering values. Chromium uses different Apple
mapping owners for different source types. Upstream libplacebo also sends
optional native metadata in paths where Microsoft recommends not relying on
it. These examples disprove simple universal rules such as “metadata means
passthrough” or “own mapping makes metadata pointless.” They do not require
SunPlayer to inherit every feature. [mpv target hints][mpv-target],
[mpv Wayland declaration][mpv-wayland], [Chromium Metal handling][chromium-metal],
[libplacebo D3D11][lp-d3d].

Those source versions matter: SunPlayer's pinned libplacebo 7.360.1 D3D11
swapchain still documents incomplete scRGB support, while the inspected newer
upstream code has dedicated scRGB handling. Replacing Qt with the already
pinned libplacebo swapchain is therefore not an established easy solution to
the Qt WCG gate. [Pinned swapchain source][lp-pinned-d3d].

## 14. Consequences for the smallest correct design

The shared policy remains simple:

**Interpret once, map once to the usable target, compose in signed linear
coordinates, then encode and declare a native path that preserves that result.**

1. Resolve one target from available evidence and the actual usable path.
   Preserve provenance/unknowns where they change interpretation. Do not
   invent physical peaks from encoding maxima.
2. Use one generic tone-operator parameter: BT.2446A at H=1, spline above one,
   as requested. Supported source-authored guidance remains a separate source
   responsibility. A choice of operator does not repair a units mismatch.
3. Give the working image one explicit basis, white and black convention.
   Convert library output into it once. Preserve negatives and highlights.
4. Keep native encoding and declaration together at the last boundary. A
   native mode change can be necessary even when the shared mapper is unified.
5. Prefer platform/Qt ownership where its API expresses the required contract.
   Add no new metadata schema, display epoch or general processing graph just
   to describe these responsibilities.
6. Reconcile the latest semantic target at a safe render boundary. Rerender
   retained video when its mapping inputs change; recreate native resources
   only when their actual contract or lifetime changes.

The newly checked native behavior prevents preserving all current transport
choices as an unquestioned design requirement. Stable PQ on baseline Mutter
SDR conflicts with avoiding a second tone map. Modern Apple mapping ownership
needs explicit consideration. These are research findings and proposed native
follow-up, not implemented changes or permission to add Windows/macOS WCG
workarounds.

There is also a real limit to “map everything ourselves”: if a compositor
supplies only an encoding envelope, the app cannot reconstruct unknown physical
panel capability from it. Choose between a supported usable-target contract,
an honest conservative target, or delegating the missing adaptation. Label that
decision; do not hide it behind guessed nits or another copy of display state.

Delegation is not a demonstrated solution to the baseline Mutter HDR limitation:
its inspected HDR branch disables the tone-mapping stage. Raw EDID/vendor gamut
and peak values can inform approximate characterization, but do not establish
current white adaptation, calibration, hardware processing or emitted light.
Do not make a new EDID reader or physical-target subsystem a prerequisite to
the shared cleanup.

### Standards compliance versus the behavior we want

Compliance means truthful pixel interpretation, supported API/protocol use,
and correct ownership/lifetimes. It does not select one universal artistic tone
curve or require every desktop to use the same reference-white brightness.
A valid PQ description can legitimately cause perceptual adaptation that our
already mapped result does not want. Conversely, disabling tone mapping does
not make out-of-range pixels displayable.

Our best-practice choice is therefore conditional on concrete native contracts:
own the source mapping, submit a correctly described prepared image, explicitly
avoid a second tone mapper where possible, and keep system display conversion.
Use native abstractions rather than adding equivalent app machinery. If the
contract cannot meet that goal, record the specific limitation and choose the
smallest honest alternative. The library operator choice at H=1 versus H>1 is
our product policy; the standards do not mandate that particular switch.

### Follow-up: final encoding and separate libplacebo use

The final encoder consumes a composition whose layers already agree on black,
reference white and signed BT.709 coordinates. It must not repair video black
by subtracting a video-specific offset from the complete UI composition. The
library-output coordinate conversion remains a preceding responsibility.

For the required paths, final encoding is bounded: native white-unit scaling,
an RGB-primary matrix where needed, then the declared transfer function.
Extended-linear scRGB/EDR preserves signed values; nonlinear SDR or PQ encoding
occurs after the destination-primary conversion. SDR WCG changes the declared
primaries rather than introducing another source-dependent mapper. This does
not enable the deferred Windows/macOS WCG paths.

Libplacebo 7.360.1 provides `pl_get_color_mapping_matrix` and
`pl_shader_delinearize` as separate building blocks. The latter generates
transfer-encoding GLSL, including black/range normalization for applicable SDR
transfers. It expects the library's linear units; LINEAR returns unchanged,
while nonlinear encoders clamp negative components. Supply matching units and
endpoints rather than applying physical target metadata a second time to an
already normalized composition. See the [matrix API](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/colorspace.h)
and [transfer implementation](https://github.com/haasn/libplacebo/blob/v7.360.1/src/shaders/colorspace.c).

The generic `pl_shader_color_map_ex` is not an unconditional encoding-only
shortcut: its header says a null state disables tone/gamut mapping, but the
pinned implementation substitutes simpler clip/linear and clip/saturation
methods and can still change luminance ranges or colors. Use the narrow
operations when the intent is only representation conversion.

Two integration choices are available. The [direct shader API](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/shaders.h)
returns generated GLSL with variable/resource descriptions, which could be
integrated into the existing QRhi pass and baked with Qt's shader tools. That
requires shader-generation/binding integration, but no additional GPU pass.
[pl_dispatch](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/dispatch.h)
can instead execute a dedicated shader without invoking the full renderer; it
requires libplacebo-visible input/output textures and correct GPU handoff.
Our existing interop wraps video output, not the final swapchain, so neither
choice is an already implemented drop-in. An additional `pl_render_image`
call would also bring renderer mapping policy into a step that does not need
it. The agreed simplification preference is libplacebo ownership of conversion,
choosing by total maintained code. A separate GPU pass is acceptable; do not
build elaborate shader-generation plumbing merely to avoid it. Retain custom
conversion only for a concrete correctness/integration reason, rather than
treating the current handwritten encoder or pass count as a requirement.

This follow-up was inspected by the primary agent and the Astra Windows/API
researcher. It is source-based feasibility analysis, not a GPU prototype or a
new accepted architecture decision.

## 15. Evidence, limits and review

The research uses standards, native API documentation, pinned source and
production examples. Calculated PQ values, RGB basis conversions and the
Mutter neutral-reference result are reproducible arithmetic, not display
measurements. Qt 6.10.2/6.11.1 and Mutter 50.1 are specifically identified;
current upstream examples are not claims about all older releases.

The previous agents were stopped at the user's request. Three new independent
Astra/xhigh researchers covered Windows, macOS and Wayland; the primary agent
covered common terminology, standards, arithmetic and synthesis. All three
then reviewed the consolidated research and plan with distinct evidence,
correctness and simplicity lenses. A second targeted round confirmed the
substantive findings resolved, with no remaining research-document findings.

Review corrections qualified native mapping ownership and gamut guarantees,
separated Wayland reference declarations from Windows PQ, repaired source
links, removed the requirement to preserve stable PQ on SDR, and prevented
missing physical facts from becoming a new subsystem mandate. Documentation
checks covered local links/anchors, reference definitions, code fences and
whitespace; `git diff --check` passed. This reviews the research and proposed
design, not a delivered renderer implementation.

No new gray-black fix, physical color certification or platform runtime result
is claimed. The unresolved implementation work is narrow but real: library-to-
working black/white normalization; a Wayland policy that meets app-owned
mapping on the baseline; accurate interpretation of preferred feedback; and
explicit native declaration/mapping ownership where current Qt behavior is
insufficient. WCG SDR enablement on Windows/macOS stays excluded.

[bt2100]: https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-3-202502-I!!PDF-E.pdf
[bt2408]: https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-8-2024-PDF-E.pdf
[icc-srgb]: https://registry.color.org/rgb-registry/srgb
[w3c-color]: https://www.w3.org/TR/2026/CRD-css-color-4-20261020/
[win-white]: https://learn.microsoft.com/en-us/windows/win32/direct2d/white-level-adjustment-effect
[win-hdr]: https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
[win-icc]: https://learn.microsoft.com/en-us/windows/win32/wcs/advanced-color-icc-profiles
[win-calibration]: https://learn.microsoft.com/en-us/windows/win32/wcs/display-calibration-mhc
[win-color-enum]: https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type
[win-interop]: https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.display.interop/nn-windows-graphics-display-interop-idisplayinformationstaticsinterop
[win-metadata]: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgiswapchain4-sethdrmetadata
[win-tonemap]: https://learn.microsoft.com/en-us/windows/win32/direct2d/hdr-tone-map-effect
[win-hdr-fields]: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/ns-dxgi1_5-dxgi_hdr_metadata_hdr10
[win-output-desc]: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/ns-dxgi1_6-dxgi_output_desc1
[win-white-query]: https://learn.microsoft.com/en-us/uwp/api/windows.graphics.display.advancedcolorinfo.sdrwhitelevelinnits
[win-sdk-mode]: https://github.com/microsoft/win32metadata/blob/2f767671802301cf043c717d66bf004ed7863487/generation/WinSDK/RecompiledIdlHeaders/um/wingdi.h#L3203
[apple-edr]: https://developer.apple.com/videos/play/wwdc2022/10114/
[apple-own]: https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping
[apple-space]: https://developer.apple.com/documentation/quartzcore/cametallayer/colorspace
[apple-headroom]: https://developer.apple.com/documentation/appkit/nsscreen/maximumextendeddynamicrangecolorcomponentvalue
[apple-potential]: https://developer.apple.com/documentation/appkit/nsscreen/maximumpotentialextendeddynamicrangecolorcomponentvalue
[apple-reference]: https://developer.apple.com/documentation/appkit/nsscreen/maximumreferenceextendeddynamicrangecolorcomponentvalue
[apple-metadata]: https://developer.apple.com/documentation/quartzcore/cametallayer/edrmetadata
[apple-tonemode]: https://developer.apple.com/documentation/quartzcore/calayer/tonemapmode-swift.property
[apple-system]: https://developer.apple.com/documentation/metal/using-system-tone-mapping-on-video-content
[apple-optical]: https://developer.apple.com/documentation/quartzcore/caedrmetadata/hdr10(displayinfo:contentinfo:opticaloutputscale:)
[webkit-tonemode]: https://github.com/WebKit/WebKit/blob/e29aed83c50ffbe64b7d09c6bd5ffd5ab4b1ddc8/Source/WebKit/Shared/RemoteLayerTree/RemoteLayerBackingStore.mm#L581
[wl-protocol]: https://github.com/wayland-mirror/wayland-protocols/blob/1.47/staging/color-management/color-management-v1.xml
[wl-appendix]: https://github.com/wayland-mirror/wayland-protocols/blob/1.47/staging/color-management/appendix.md
[wl-current]: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/819004adb3ab7e46f3fa3caef05b96e20434b244/staging/color-management/color-management-v1.xml
[mesa-wayland]: https://gitlab.freedesktop.org/mesa/mesa/-/blob/566ed9cd9dff0a0825c7ea71fc3c31a4af03d5a7/src/vulkan/wsi/wsi_common_wayland.c
[mutter-wl]: https://github.com/GNOME/mutter/blob/8e9bb806da4a6a4fc87aa4a481b228dba6735cbd/src/wayland/meta-wayland-color-management.c
[mutter-color]: https://github.com/GNOME/mutter/blob/8e9bb806da4a6a4fc87aa4a481b228dba6735cbd/clutter/clutter/clutter-color-state-params.c
[mutter-device]: https://github.com/GNOME/mutter/blob/8e9bb806da4a6a4fc87aa4a481b228dba6735cbd/src/backends/meta-color-device.c
[kwin-wl]: https://github.com/KDE/kwin/blob/b075cce99e846c48407ca73b54563c1193ead3a6/src/wayland/colormanagement_v1.cpp
[kwin-pipeline]: https://github.com/KDE/kwin/blob/b075cce99e846c48407ca73b54563c1193ead3a6/src/core/colorpipeline.cpp
[krita-color]: https://docs.krita.org/en/reference_manual/preferences/color_management_settings.html
[qt-d3d]: https://github.com/qt/qtbase/blob/v6.11.1/src/gui/rhi/qrhid3d11.cpp
[qt-dxgi]: https://github.com/qt/qtbase/blob/v6.11.1/src/gui/rhi/qdxgihdrinfo.cpp
[qt-metal]: https://github.com/qt/qtbase/blob/v6.11.1/src/gui/rhi/qrhimetal.mm
[qt-cocoa]: https://github.com/qt/qtbase/blob/v6.11.1/src/plugins/platforms/cocoa/qnsview_drawing.mm
[qt-wayland]: https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandcolormanagement.cpp
[qt-window]: https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandwindow.cpp
[qt-vulkan]: https://github.com/qt/qtbase/blob/v6.10.2/src/gui/rhi/qrhivulkan.cpp
[mpv-target]: https://github.com/mpv-player/mpv/blob/488fec0789e5026804ac8dd03fb749570416d48a/video/out/vo_gpu_next.c#L1485
[mpv-wayland]: https://github.com/mpv-player/mpv/blob/488fec0789e5026804ac8dd03fb749570416d48a/video/out/wayland_common.c#L3733
[chromium-metal]: https://chromium.googlesource.com/chromium/src/+/e882230b0e555b9cb9652d22bbec2dbd18a22970/components/metal_util/hdr_copier_layer.mm
[lp-d3d]: https://github.com/haasn/libplacebo/blob/13d0b79827ea1671f991ca57700e5356af9731a5/src/d3d11/swapchain.c#L205
[lp-pinned-d3d]: https://github.com/haasn/libplacebo/blob/v7.360.1/src/d3d11/swapchain.c
