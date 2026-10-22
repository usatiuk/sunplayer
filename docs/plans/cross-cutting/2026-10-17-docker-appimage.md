# Docker-built AppImage

Status: Complete (local implementation and validation; no push/publication)

Build an x86-64 AppImage entirely in Ubuntu 26.04 Docker containers, using
the existing CMake install and pinned linuxdeploy/Qt/appimagetool binaries.
Mount source read-only; keep build state in a Docker volume and export only
the AppImage and SHA-256 file. No host build-package installation.

Bundle Qt/QML and media libraries. Keep Vulkan, VA-API, DRM, EGL/GL and
Wayland system libraries and vendor drivers host-owned. Require the Ubuntu
26.04 runtime baseline rather than add loader probes or compatibility shims.
Explicitly deploy Qt 6.10's native Wayland integration. Preserve existing
hardware decode, import, fallback and color policy without player changes.

Use the same Docker entry point locally and in CI. Release builds consume the
existing release Git bundle so Windows and AppImage assets identify the same
version-bump commit; publication waits for both artifacts.

Validate shell/YAML and Docker builds, inspect the AppDir's library boundary,
and run the packaged QML/playback scenarios with no development Qt available.
Attempt real native-Wayland/Vulkan/VAAPI evidence where accessible; distinguish
software-hosted checks from hardware coverage. Update build/testing docs and
record unavailable GPU/vendor/HDR coverage. No new decoder backend, updater,
older-distro build graph, signing, Snap work or actual release publication.

## Evidence, 2026-10-17

* `packaging/appimage/build.sh` completes using rootless Docker with source
  mounted read-only. The output is a roughly 102 MiB x86-64 AppImage plus a
  passing SHA-256 file. No build packages were installed on the host.
* The separate smoke container resolves Qt Core/Quick, all four FFmpeg
  libraries, libplacebo, cubeb and libass from the extracted AppImage.
  `--verify-qml` passes; `--playback-smoke` passes with advancing Pulse output
  and multiple presented video revisions beyond 1.5 seconds. These are the
  real production application boundaries, not new mocked tests.
* Final AppDir inspection finds none of the excluded Vulkan, VAAPI, DRM,
  GBM, EGL/GL or Wayland system libraries, and only `libqwayland.so` remains
  in the platform-plugin directory. The Qt plugin initially copied
  `libwayland-cursor` despite exclusions; the explicit post-plugin removal
  closes that observed upstream behavior.
* Native playback also passes through the actual AppImage runtime's
  `--appimage-extract-and-run` option. A disposable Docker runtime uses the
  real `/dev/dri`, Wayland and Pulse sockets, and the host iHD media driver
  plus its `libigdgmm` dependency. Its ordinary Mesa/Vulkan runtime comes
  from the smoke image. A generated fixture remuxes the repository's H.264
  inter-frame seek video with the audio-late fixture's FLAC stream without
  re-encoding. Logs show `vaapi.device renderNode=/dev/dri/renderD128`,
  `decode.first_frame hardware=true`, and successful playback completion
  at 1519 ms with 72930 presented audio frames. The compositor accepts
  version-2 managed HDR10/PQ declarations; this is protocol/presentation
  evidence, not physical HDR accuracy or universal driver compatibility.
* A second native run opens the ASS/SubRip/embedded-font fixture and discovers
  both tracks. Its fullscreen scenario times out in stage 0 waiting for
  cursor idle while video keeps presenting. It does not prove fullscreen,
  seeking, selected-subtitle rendering or portal inhibition. These remain
  manual/controlled-session checks; no unrelated player changes were made.
* Shell syntax and ShellCheck pass for all four packaging scripts. Actionlint
  passes with the pre-existing Linux test script's unused `attempt` warning
  excluded; expression/YAML validation also passes with ShellCheck disabled.
* The exact release publication shell was exercised with a substituted
  GitHub CLI edge: new publication, identical retry and partial retry pass;
  a conflicting retained image or checksum fails before uploading any
  missing asset. All existing assets are preflighted before mutation.
  No authenticated GitHub mutation or workflow dispatch was performed.
* Three independent read-only reviews covered packaging/runtime correctness,
  release identity/recovery, and simplicity/evidence/scope. Fixed the missing
  FFmpeg `libva-x11` host dependency and both directions of partial-upload
  checksum mismatch. Follow-up packaging review passes. No compatibility
  shim, updater, decoder backend or release-orchestration refactor was added.

Hosted CI and actual release publication remain unexecuted at the user's
request. AMD, NVIDIA, older distributions, FUSE-mounted startup, selected
subtitle rendering, controlled fullscreen/seek and physical HDR are not
validated by this delivery. See `docs/DEFERRED.md` and the packaging README.

## Sources and decisions

### Desktop integration follow-up

The initial bundle lacked `iconengines/libqsvgicon.so` and all platform themes.
Host symbolic SVG window icons therefore fell back to the player's Lucide
assets, and Qt Quick's FileDialog lacked its native desktop integration.
Bundle the matching distro Qt SVG engine, GTK and portal themes; preserve host
icon assets and settings. No player UI redesign is needed.

The launcher respects explicit `QT_QPA_PLATFORMTHEME`; otherwise one bundled
`dbus-send` FileChooser Properties.Get request, bounded to one second, selects
the portal only when its interface is available. This avoids desktop-name
allowlists and unconditional portal selection on systems without a backend.
Qt's portal theme delegates desktop settings/icons to its normal base theme.
Its dialog can fail to open when FileChooser is absent and there is no native
helper, so a semicolon-separated theme list does not provide that recovery.

Primary references:

* [Qt icons](https://doc.qt.io/qt-6.10/qtquickcontrols-icons.html): theme names
  precede source fallbacks.
* [Qt plugin deployment](https://doc.qt.io/qt-6.10/deployment-plugins.html):
  bundle compatible Qt plugins rather than loading the host's Qt SDK plugins.
* [Qt portal theme](https://raw.githubusercontent.com/qt/qtbase/6.10/src/plugins/platformthemes/xdgdesktopportal/qxdgdesktopportaltheme.cpp)
  and [dialog implementation](https://raw.githubusercontent.com/qt/qtbase/6.10/src/plugins/platformthemes/xdgdesktopportal/qxdgdesktopportalfiledialog.cpp)
  establish base-theme delegation and the unavailable-portal failure behavior.
* [FileChooser interface](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html)
  defines the desktop-owned picker boundary.
* [DuckStation launcher](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/qthost.cpp)
  demonstrates respecting explicit theme overrides; [OpenShot's reported
  failure](https://github.com/OpenShot/openshot-qt/issues/6084) illustrates why
  forcing a portal without an available backend is insufficient.

Validation uses a test-only public Qt icon/FileDialog probe outside the
shipped AppDir. Native `OpenFile` bus traffic is required to claim portal
dispatch; successful probe exit alone is insufficient.

Follow-up evidence: the Docker rebuild and separate smoke pass, including all
four Adwaita symbolic icons, packaged QML, and A/V completion at 1508 ms with
72399 presented audio frames. The native Wayland probe renders the actual
host `Yaru-olive` window icons with the rebuilt bundle. Opening its public
Qt Quick FileDialog after startup produces an observed
`org.freedesktop.portal.FileChooser.OpenFile` request titled
`SunPlayer portal check` on the real session bus. The immediate-startup probe
initially used GTK before Qt's asynchronous portal capability query completed;
the interactive probe models opening after startup. Its short timer bounds
the manual diagnostic, rather than asserting that exit proves dispatch.
The native probe uses the tool sandbox, whose read-only session directory
produces GTK/dconf warnings; picker visual parity is still a user desktop
check. Launcher checks using the real bundled D-Bus helper select
`xdgdesktopportal` on the live host, preserve `gtk3` when explicitly supplied,
and leave normal Qt selection intact with an unavailable session bus.
SHA-256 validation, ShellCheck and `git diff --check` pass. Three independent
read-only follow-up reviews found no blocking issue and required the native
dispatch evidence recorded here. No commit, push or release was performed.
An additional native hardware run still selects Intel VAAPI and decodes
hardware frames, but its playback smoke times out at 5000 ms with all 240000
audio frames presented and no distinct presented video revisions observed.
This does not supersede the earlier successful native run or prove current
visible playback; controlled native presentation remains a coverage gap.

* [AppImage native packaging](https://docs.appimage.org/packaging-guide/from-source/native-binaries.html)
  supports reusing CMake's install tree with linuxdeploy and its Qt plugin.
* [Qt plugin configuration](https://github.com/linuxdeploy/linuxdeploy-plugin-qt)
  provides QML source scanning; inspect actual Qt 6.10 plugin names rather
  than copying historical Wayland filenames from examples.
* [AppImage exclusions](https://github.com/AppImageCommunity/pkg2appimage/blob/master/excludelist)
  and [Moonlight's packaging](https://github.com/moonlight-stream/moonlight-qt/blob/master/scripts/build-appimage.sh)
  document in-process host-driver/library compatibility problems. Choose an
  explicit recent host baseline rather than reproduce their loader probes.
* [Qt 6.10 deployment](https://doc.qt.io/qt-6.10/qt-deploy-runtime-dependencies.html)
  excludes system-installed libraries by default, so it does not eliminate
  the explicit AppImage dependency policy for this distro-based build.

## Shared control contrast follow-up

The light portal palette exposed a white ellipsis SVG on a light ToolButton
background when keyboard-focused, hovered or pressed. The shared idle action
now uses a square standard Button with explicit buttonText icon tint. The
four HDR Lab switches retain windowText for native Windows/macOS/Basic and
also set text for Fusion. Custom transport/titlebar backgrounds include
visualFocus in their existing hover highlights. No platform conditional,
global style override, Qt patch or portal-selection change is introduced.
See the [source research](../../research/2026-10-17-qt-control-contrast.md).

The real QML shell passes with both Basic and Fusion, including light idle
button palette/focus, all four switch label colors and all three titlebar
focus highlights. A fresh-shell scenario verifies transport focus separately
from the large scenario's native dialog/page-routing history. QML lint passes.
Three read-only reviewers found no remaining blocker; the initial test's
recursive discovery of internal IconLabels was corrected with explicit control
names. Native Windows/macOS style sources were inspected; their runtime
appearance and complete keyboard traversal are not claimed by these tests.

The Docker AppImage rebuild with these shared QML changes completes. Packaged
QML verification, all four system window icons, dependency-resolution checks
and A/V smoke pass (1524 ms, 73196 presented audio frames). SHA-256 validation
passes and the artifact digest differs from the preceding package. Build to a
separate output directory and atomically replace the normal artifact, keeping
already-running AppImage mounts intact. Final formatting and diff checks pass.
No commit, push, CI dispatch or release publication was performed.

## Rootful CI tool-permission repair

CI reached package-sunplayer's linuxdeploy call after successful Release compile
and installation, then reported command not found. Read-only inspection and
reproduction established root-owned 0700 extraction directories under
`/opt/tools/linuxdeploy/squashfs-root` and appimagetool's equivalent. Rootless
local builds run as container root and masked the failure; running the existing
image as `--user 1001:1001` reproduced exit 127 through the otherwise-correct
/usr/local/bin symlink. This was not a PATH or missing-download problem.

The Dockerfile now applies `chmod -R a+rX` to each extracted tool tree, retaining
read-only/nonexecutable data semantics while allowing directory traversal and
execution of existing executables. `set -eu` prevents extraction failures being
masked by a later successful symlink command. Image construction executes
linuxdeploy --version, linuxdeploy-plugin-qt --help and appimagetool --version
as UID 65534, then restores root for image setup. This catches the real runtime
contract without adding a test framework, alternate user policy or new tools.
All three checks also pass as the previously failing UID 1001 after the repair.

Three independent read-only reviews covered tool installation, pipeline/cache
behavior, and simplicity/failure handling. A focused recheck found the applied
fix resolves the findings. No change to release orchestration was warranted:
the visible Publish release job requires the reusable workflow's release input
plus the authorized actor/main conditions; normal push CI cannot publish.
The builder's tool layer was freshly executed, including all pinned downloads
and checksums, rather than relying on the old cached successful root build.
Full packaging and separate-container dependency/icon/QML/playback smoke pass.
Playback reports Pulse, position 1519 ms and 72947 presented audio frames.
The new AppImage and checksum verify; the normal local artifacts are refreshed
by atomic file replacement. This proves software-hosted packaging and execution,
not new physical GPU/HDR evidence. Release conditions were inspected and left
unchanged. The conversation's CI heartbeat was disabled at the user's request.

## Runtime checksum refresh (2026-10-22)

[CI run 39830257741](https://github.com/usatiuk/sunplayer/actions/runs/39830257741)
failed before source compilation because upstream replaced the continuous
`runtime-x86_64` asset on October 19. Update only its `tools.sha256` entry to
`617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46`.
The fresh download matches GitHub's published release-asset digest. The release
points to [fedfad51](https://github.com/AppImage/type2-runtime/commit/fedfad51acc2860b2916a1eef32ba4a500ff31ef),
which removes temporary build paths from dependency debug information to make
runtime builds reproducible. Existing checksum enforcement and mutable-URL
update policy remain unchanged.

All six fresh tool downloads pass the updated manifest. Rebuilding with the
verified runtime mounted into the cached builder passes; no dependency install
or Docker image rebuild is needed for this local validation. The embedded
runtime matches the download except its expected populated `.digest_md5`
section. The separate runtime container passes dependency/icon/QML and A/V
playback smoke (1520 ms, Pulse, 73004 presented audio frames), and the finished
AppImage checksum verifies. This validates the new unsigned runtime/package;
signed release verification remains the existing release job's responsibility.
