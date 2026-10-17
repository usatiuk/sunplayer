# Restore Wayland HDR without optional mastering metadata

Status: Delivered; native HDR restored

## Regression and scope

September 20 commit `e7abecb` added mastering-display-primaries to the HDR
capability gate. This incorrectly demotes the native GNOME compositor to SDR:
it advertises PQ/BT.2020 but not
the optional mastering requests. The October WCG change preserved that gate.
The user reports HDR no longer presenting; current native feedback reports
HDR reference white 383 nits and target headroom 26.1097 while app selection
is managed display-primary SDR.

## Approach

Restore PQ/BT.2020 HDR eligibility independently of optional mastering metadata.
When mastering support exists, preserve the existing tight composed volume.
Otherwise omit those requests and use the BT.2020/PQ full transport volume.
Require explicit luminances and declare `(0,10000,203)` for all PQ surfaces: the
protocol default minimum is 0.005, which excludes our zero-black backgrounds.
Represent omitted volume in the existing immutable description key; target changes still rerender video but do
not recreate an identical full-volume declaration. Preserve generation-bounded
presentation rejection and managed display-primary SDR fallback. No custom
swapchain, compositor detection, forced HDR or new compatibility backend.

## Evidence and validation plan

Primary protocol XML explicitly defaults absent mastering primaries/luminances
to the primary volume; PQ defaults to 0.005–10000 nits with 203-nit reference white.
The two mastering requests are feature-gated, not PQ itself.

Review policy, protocol/lifetime behavior and regression coverage independently.
Test both mastering-present and mastering-absent HDR selection, missing PQ/2020,
rejection and SDR fallback. Build Debug/Release, run native production HDR
fixture playback outside the sandbox, trace accepted PQ declaration and 10-bit
buffers with no unsupported mastering requests, and run affected policy/render
CTests. Keep physical display accuracy distinct from protocol/pixel evidence.

## Results

* Debug and Release builds pass; `git diff --check` and clang-format pass.
* Affected native CTests (`presentation-target`, `wayland-color-management`,
  `ffmpeg-first-frame`) pass 3/3 with `SUNPLAYER_REQUIRE_VAAPI=1`, in 8.47 s.
* Native trace changes from `managed-display-gamma22-sdr` to
  `managed-hdr10-pq`. It has a ready BT.2020/PQ description with explicit
  `(0,10000,203)`, no mastering requests, and XR30 (fourcc 808665688) buffers.
  Ready description #74 is applied before buffer #62 attaches and commits.
  One enhanced description is reused while target feedback changes.
* The PQ fixture decodes four frames; the user sees the native window in HDR.
  Process exit is zero, but no playback-smoke completion marker is present.
  This establishes native HDR transport, not completed automated smoke or
  measured display luminance. Remaining GPU tests are offscreen.
* Three independent reviewers cover correctness, simplicity/lifetime and
  evidence. All find no blocking implementation defect. Their initial plan
  review corrected PQ's nonzero default minimum; the implementation explicitly
  declares zero black and requires that supported luminance request.
* Checked adjacent format-pair, output encoding, reference-white scaling and
  rejection paths; no additional concrete HDR eligibility bug found. Other OS
  code has no equivalent mastering capability gate; no Windows/macOS runtime
  tests were performed for this Linux-only fix.

Logs for this investigation are `/tmp/sunplayer-hdr-regression-before.log`,
`after.log`, `tests.log`, `debug-build-final.log`, and `release-build.log` with
that same prefix. Nothing was installed.

See [ADR 0033](../../decisions/0033-keep-wayland-hdr-mastering-optional.md) and
[research](../../research/2026-10-17-wayland-optional-hdr-mastering.md).
