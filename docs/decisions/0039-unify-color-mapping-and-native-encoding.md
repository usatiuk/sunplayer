# 0039: Unify color mapping and native encoding

* Status: Accepted
* Date: 2026-10-22
* Amended by [ADR 0040](0040-configure-tone-mapping-by-active-output.md) for
  playback algorithm preferences and conditional peak detection.
* Supersedes the stable-PQ-on-SDR decision in ADR 0021, source/SDR-mode operator
  selection in ADRs 0023/0027, and unconditional PQ preference in ADR 0032.
* Amends the normalization and unknown-HDR-black clauses in ADRs
  0025/0026/0029/0030; source preferences and supported platform scope remain.

## Context

Shared rendering modes coupled the video mapper to native transport and
duplicated display-target fields across presentation and rendered surfaces.
Scale-only conversion of libplacebo LINEAR output also retained its modeled
black, unlike the zero-black coordinates used for UI and subtitles. Separately,
the inspected Mutter pipeline adds HDR-to-SDR mapping to PQ content even when
the application has already mapped that content to SDR.

## Decision

Resolve one `VideoColorTarget` and reuse it in the rendered-surface request.
Choose BT.2446A at H=1 and spline at H>1 for all source families and libplacebo
HDR Lab. Preserve the supported source-authored ST 2094-40 override, Lab's
explicit Clip control and existing coherent metadata selection. Native
encoding does not choose the video operator.

Infer the constructed libplacebo LINEAR target once. Unknown minimum is numeric
zero and receives the library's nominal 1000:1 contrast; known zero uses its
effectively-zero sentinel; positive minimum retains the target-relative unit
conversion. The PRE_OUTPUT hook converts mapped light into signed linear
BT.709/D65 working coordinates with `q=(L-B)/(C-B)`. B is target black, C is
ordinary white, and B<C is validated before use. C is the decoded PQ/Dolby
reference preference R, supported Absolute PQ's platform white W, or 203 for
relative SDR/HLG and HDR Lab. Working black is zero and ordinary white is one.
Nominal peak P=C*H becomes `(P-B)/(C-B)`, possibly slightly above H.

Compose video, subtitles and UI in FP16 before native-unit scaling and final
encoding. Generate the sRGB/gamma-2.2/PQ transfer code at build time with
libplacebo and bake it into the existing QRhi compositor. Use libplacebo's
primary-conversion matrix; preserve signed components until the destination
basis, then apply only the selected encoding's required clamp. This adds no
GPU pass, second mapping operation or repeat of physical-black normalization.

Keep `PresentationSurfaceContract` about encoding and output primaries. The
optional native controller keeps Wayland modes, protocol objects, hints and
rejections private; Windows/macOS continue using their current Qt output path.
There is no new Windows/macOS WCG SDR support or placeholder controller.

On Wayland select matching SDR/WCG gamma-2.2 output on SDR displays and PQ on
HDR displays, subject to existing capability and fallback contracts. Use raw
latest display observations to select the output, then resolve its usable
mapping target. Reuse asynchronous description readiness and queued swapchain
transitions. A primaries-only change preserves the swapchain; a format change
preserves the window, native surface and device. Assign each ready declaration
with its matching buffer. Reject PQ and explicit-primary SDR independently per
graphics-device generation, without adding display epochs.

## Alternatives and consequences

Keeping rendering modes or scale-only normalization would preserve conflicting
target meanings and lifted black. A second full libplacebo render could map
the composition again. A generic shader/native-presentation framework is not
needed for the current fixed transfer functions and one optional native writer.
Qt delegation is separate work: its checked Wayland path does not establish
equivalent mutable declarations, explicit luminances or rejection handling.

Source interpretation, hardware import and resource synchronization remain
unchanged. Linux retains WCG SDR and HDR; Windows/macOS retain existing Qt
eligibility and output conventions. Matching SDR avoids the identified Mutter
PQ-to-SDR branch, but does not establish physical HDR target measurements or
universal compositor mapping ownership.

Implementation and validation evidence are recorded in the
[delivery plan](../plans/video-rendering/2026-10-21-color-pipeline-simplification.md).
The native controller's readiness/assignment and pending replacement are
exercised on a hidden host Wayland window with synthetic display/generation
inputs and injected rejection. Actual engine swapchain-buffer
pairing and physical display movement remain acceptance work. This decision
does not claim execution of the changed path on Windows/macOS or physical
display calibration.
