# SDR unknown-black correction checklist

Status: Complete — automated scope; native comparison items remain deferred

## Gates

| ID | Gate | Status | Acceptance and evidence |
| --- | --- | --- | --- |
| RESEARCH-1 | Pinned and platform contracts | Done | Research note distinguishes confirmed libplacebo behavior, retained SunPlayer viewing policies, and unresolved macOS viewing intent. |
| PLAN-1 | Independent revised-plan review | Done | Three read-only reviewers approved the exact revised plan/research/checklist before production edits. |
| RUNTIME-1 | Windows SDR state | Done | Code review plus `windows-sdr-keeps-target-luminance-unknown` prove the current production-policy state is `H=1` with unknown minimum. |
| CODE-1 | Unknown SDR target black | Done | Only unknown no-headroom SDR reaches libplacebo as `0`; unknown HDR and known zero intentionally map to `PL_COLOR_HDR_BLACK`; positive minima retain their conversion. |
| TEST-1 | Pinned numerical regression | Done | Nominal-luminance inference and BT.2446A 0.01–5-nit values distinguish inferred 0.1-nit SDR black from known zero. |
| TEST-2 | Production near-black regression | Done | An in-memory GBRP16 BT.2446A PQ ramp passes through the real D3D11 producer with unknown minimum; existing metadata-less PQ coverage retains its spline assertion. No binary fixture changes. |
| TEST-3 | Windows HDR no-regression | Done | Existing `203 * H` producer and `W / 80` compositor regressions pass unchanged in the complete suite. |
| DOC-1 | Current project truth | Done | ADR 0026 marks 0003/0008/0025 as amended; current video/presentation/testing docs agree with the bounded change. |
| VALIDATE-1 | Focused and complete checks | Done | Out-of-sandbox Debug build passed; focused 3/3 tests and complete 38/38 configured tests passed in the Visual Studio environment. |
| REVIEW-1 | Independent post-change review | Done | Three independent reviewers approved the final code/tests/docs diff after minor documentation findings were resolved. |
| NATIVE-1 | Windows SDR physical comparison | Needs native hardware | Corrected near-black content is compared side by side with Windows Media Player on the reported display. |
| MAC-1 | macOS 100-vs-203 experiment | Deferred investigation | Diagnostic patches are compared with QuickTime/AVFoundation before any normal-playback anchor change. |
| CURVE-1 | BT.2446A-vs-spline A/B | Deferred investigation | Compare only after the target-black repair is isolated and physically validated. |

## Explicitly retained, deferred, or rejected

| Item | Disposition | Reason |
| --- | --- | --- |
| Windows HDR producer `203 * H` | Retained baseline | Current native result looks correct; no evidence supports redesign during this SDR repair. |
| Windows final whole-composition `W / 80` | Retained baseline | This is SunPlayer's chosen reference-white-adaptive viewing contract and must remain exactly once. |
| Windows SDR nominal 100 and `203 / 100` | Retained policy | Isolate the confirmed target-black bug before changing another mapping variable. |
| Managed Wayland PQ203 | No change | Preserve the current declared/encoded presentation contract. |
| BT.2446A/spline selection | No change | Operator changes would confound diagnosis; tests must explicitly assert which operator is selected. |
| macOS final EDR scale `1` | Retained | EDR is display-referred and current headroom remains the rendering ceiling. |
| macOS 100-nit normal-playback anchor | Deferred | Apple documents this video convention, but SunPlayer needs same-display native A/B evidence before adopting it. |
| Shared cross-platform 100-nit anchor | Rejected | Unsupported by native evidence and would materially alter observed-good Windows plus unvalidated Wayland behavior. |
| `absoluteVideoReferenceWhiteNits` surface state | Rejected for this patch | Premature architecture for an unresolved macOS policy. |
| CAEDRMetadata on mixed UI/video layer | Rejected | SunPlayer already tone maps and composes the layer; system tone mapping would duplicate ownership. |
