# Redirected Qt Quick target completion and window outline

Status: Complete

## Goal

Restore one crisp, media-independent inner perimeter around the complete
application-drawn window while correcting the underlying redirected Qt Quick
render-target contract shared by Windows and Linux.

Completion means:

* the custom Qt Quick target owns the depth/stencil attachment expected by
  Qt's default 2D renderer;
* application chrome draws one cohesive full-window outline above the titlebar
  and page scene;
* the outline remains present with no media and disappears in fullscreen;
* its logical width follows the actual redirected-render DPR so it represents
  one physical pixel; and
* tests and documentation make no claim beyond the evidence actually obtained.

## Grounded baseline

Commit `379faf6` has no intentional application-chrome perimeter. Its
`QuickUiLayer` gives Qt Quick a color-only `QRhiTextureRenderTarget` through
`QQuickRenderTarget::fromRhiRenderTarget()`. The current QML chrome correctly
owns titlebar and resize interaction but no frame visual.

The exact Qt 6.10.2 source analysis and the clean-slate WSLg experiment are
recorded in
[the research note](../../research/2026-08-02-redirected-qt-quick-depth-and-window-outline.md).
The experiment proved that a matching depth/stencil attachment plus one
full-root border restores the missing perimeter in the production Vulkan
window. Its code was reverted before this plan began.

## Fixed invariants

* `QuickUiLayer` owns every attachment in the `QRhiRenderTarget` it supplies to
  `fromRhiRenderTarget()`; Qt does not complete an already-created target.
* Color texture, depth/stencil buffer, render target, and compatible render-
  pass descriptor are one size/sample/lifetime group.
* The full QML scene keeps ordinary Qt Quick depth-assisted ordering enabled.
* Window-chrome visuals remain in `ClientSideWindowChrome`; presentation and
  final-compositor code do not know about borders.
* The outline is one inward rectangle, not independently reconciled edge state.
* Visibility follows existing application-chrome availability and is
  independent of video, HDR Lab, content inset, and titlebar opacity.
* The DPR passed to QML is the same validated native render DPR consumed by
  `QuickUiLayer`, not the offscreen Quick window's attached `Screen` value.

## Implementation

1. Add one single-sample `QRhiRenderBuffer::DepthStencil` beside the existing
   RGBA16F texture. Fail through the existing device-loss or packaged-program
   paths if it cannot be created, attach it before creating the render target,
   and release it in dependency order.
2. Add a required `renderDevicePixelRatio` value to the redirected QML root.
   Initialize it from the native render window and update it only when
   `ensureRenderTarget()` observes a changed validated DPR.
3. Pass that value to `ClientSideWindowChrome` and add one full-root transparent
   `Rectangle` above visual content. Use an opaque `#4a4f5a` border,
   antialiasing off, `pixelAligned: false`, and width
   `1 / renderDevicePixelRatio`.
4. Extend the existing QML shell scenario to check full-root geometry,
   enabled/fullscreen visibility, media independence, and one-physical-pixel
   width at representative DPR values through the public QML boundary.
5. Synchronize ADR 0001, ADR 0020, the graphics/UI subsystem descriptions,
   project status, testing strategy, Linux checklist/evidence, research index,
   and deferred-work list.

## Validation

Run from clean build directories where practical:

* Configure and build the Linux Debug tree with tests enabled.
* Run `sunplayer_qmllint`, the focused `qml-shell` test, packaged-QML
  verification, and then the complete Linux CTest set.
* Configure/build/install Release and verify the installed QML module.
* Run the bounded native-Wayland production scenario under Vulkan standard and
  synchronization validation.
* Preserve the already completed interactive WSLg experiment as the visual
  regression evidence. Ask for a final interactive confirmation if the final
  implementation differs materially from the proven prototype.
* Run `git diff --check` and inspect the final change against the baseline.

The QML scenario proves policy and geometry but not GPU pixels. The real
interactive run is the current highest practical oracle for the original
visual defect. A dedicated redirected-Quick capture harness is not introduced
solely for this change.

## Review

After implementation and validation, request at least three independent
read-only reviews:

* Qt Quick/QRhi correctness, attachment lifetime, and cross-backend risk.
* Regression coverage, documentation accuracy, and unsupported claims.
* Simplicity and scope: actively remove redundant state, fallback treatments,
  speculative abstractions, and work unrelated to the proven cause.

Resolve substantive findings and rerun affected validation. Re-review any
material redesign.

## Explicit non-goals

* Exterior shadows or transparent native margins.
* A compositor-shader border or second native surface.
* Qt-private decoration negotiation or runtime decoration-mode switching.
* A generic theme/token framework.
* A permanent GPU readback/debug overlay.
* Changes to color management, Vulkan/libplacebo interop, video viewport
  geometry, titlebar fade policy, or resize interaction.

## Delivery boundary

Ship the render-target correction, one QML outline, focused regression
coverage, and synchronized documentation as one coherent change. Do not
commit or push without separate authorization.

## Implementation checkpoint

`QuickUiLayer` now owns a single-sample depth/stencil buffer beside its RGBA16F
color texture, attaches both to the target supplied to Qt Quick, and releases
the complete target group before QRhi teardown. Its existing validated native-
window DPR crosses one required root property into QML. The application chrome
contains one full-root inward `Rectangle`; no alternate edge treatment,
compositor path, platform branch, or decoration state was added.

The QML shell scenario reads the rectangle's actual grouped `border.width`
property at DPR 1 and 2, verifies that the outline covers the full chrome root,
sits above the titlebar, appears with no media and with an active video
viewport, and hides in fullscreen.

## Validation performed

The final working tree passed:

```sh
cmake -S . -B /tmp/sunplayer-outline-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/sunplayer-outline-debug --parallel 2
ctest --test-dir /tmp/sunplayer-outline-debug \
  --output-on-failure --parallel 2
cmake --build /tmp/sunplayer-outline-debug \
  --parallel 2 --target all_qmllint
```

All 24 CTests and both QML lint targets passed.

The installed Release path also passed:

```sh
cmake -S . -B /tmp/sunplayer-outline-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/sunplayer-outline-install
cmake --build /tmp/sunplayer-outline-release --parallel 2
cmake --install /tmp/sunplayer-outline-release
/tmp/sunplayer-outline-install/bin/sunplayer \
  --verify-qml --no-log-file
```

The installed binary completed the bounded native-Wayland fullscreen scenario
under the Khronos standard and synchronization validation layers:

```sh
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
VK_LAYER_VALIDATE_SYNC=1 \
  /tmp/sunplayer-outline-install/bin/sunplayer \
  --fullscreen-smoke --no-log-file \
  tests/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv
```

It returned status zero with no Vulkan validation error. WSLg emitted its
already-recorded zero-size fullscreen configure, post-completion xdg-surface,
and pipe-teardown diagnostics.

Before the clean-slate production pass, the user ran the narrow prototype in
the real WSLg Vulkan window and confirmed that the complete perimeter rendered
where the earlier QML treatments had failed. The final implementation retains
the same attachment and outline behavior; its only refinement is sourcing DPR
from `QuickUiLayer` instead of the offscreen QML `Screen` attachment.

The supported Windows build and fractional/mixed-DPR native monitor matrix
were not available in this environment. Those remain platform validation
gaps, not alternate implementation paths.

## Review outcome

Three independent read-only reviews covered Qt Quick/QRhi correctness,
tests/evidence/documentation, and simplicity/anti-overengineering.

Review found no attachment lifetime, device-loss, cross-backend, DPR
propagation, or production-design defect. Findings were resolved by:

* testing the actual grouped border width and stacking rather than only a
  helper expression;
* removing that now-unnecessary helper property;
* preserving the earlier Linux presentation evidence as a historical
  checkpoint and recording current build paths here;
* removing unsupported speculation about another producer for the observed
  state-dependent outline;
* restoring one architecture-tree indentation; and
* recording final commands and evidence in this plan.

The simplicity review found the production change already minimal: it uses
Qt's normal complete render-target contract and one QML primitive, with no
fallback stack, observer, retry state, native ownership, or speculative
abstraction. Targeted correctness and evidence rechecks confirmed that the
review findings were resolved without introducing another issue.

`git diff --check` passes. No commit or push was performed.
