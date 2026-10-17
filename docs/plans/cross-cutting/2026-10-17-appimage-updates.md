# AppImage updates

Status: Implemented and validated locally. User authorized commit/push and a
minor release (v1.4.0). Hosted release execution is pending.

## Accepted behavior

- Official signed AppImage releases check the published stable channel once per
  normal launch, asynchronously. No daemon, persistent schedule or polling loop.
- Show **Update available** only inside PlayerPage's existing `emptyState`
  (`MediaSession.Empty`), beside Open video. No playback notification or popup.
- An explicit **Update and restart** action starts the update. Use an
  indeterminate Updating indicator; upstream CLI output is not a progress API.
- Opening media remains possible. Leaving Empty cancels an ongoing update;
  recheck the latest media state before activation. Playback never triggers an
  update restart. No persisted ready-to-install state or session restoration.
- Cancellation/download/authentication/storage failure leaves the installed
  AppImage unchanged. Report an explicit update failure on the empty screen;
  background check failures stay in diagnostics and retry next launch.

## Implementation

Use a pinned, bundled **appimageupdatetool** through `QProcess`, rather than
linking libappimageupdate into the player. Keep all helper/network/install
behavior in one Linux/AppImage owner. Expose the few UI facts/actions through
the existing WindowCommands boundary; do not thread an updater through QRhi,
QuickUiLayer, video or media internals. No cross-platform updater interface or
placeholder backend. Other installation types do not expose the action.

The check uses the helper's documented check mode and exit codes. On an
explicit update:

1. Acquire one update lock for the installed AppImage path, preventing two
   player instances from updating the same file concurrently.
2. Create a private temporary directory beside the installed AppImage. Copy
   the current image into it as a deterministic seed/candidate path. Use a copy,
   not a hardlink optimization or symlink.
3. Run the helper with `--overwrite` against that staged copy. Its downloads,
   backup and signature validation stay inside staging. A failed or terminated
   helper never gets to modify the installed image.
4. Require successful process exit, an actually changed candidate and preserved
   executable permissions. The changed-file check matters because upstream
   zsync2 can report success after some replacement failures. An unchanged
   candidate does not cause restart or a false updated-success message.
5. Recheck Empty and atomically rename the candidate over the original path.
   Keep the installed filename/path stable. Never truncate the running image.
6. Relaunch the outer `APPIMAGE`, using normal player shutdown and a clean
   launcher environment, rather than the executable inside the old mount.
   Restore/remove the old bundle's library/plugin environment deliberately.

The worker must have finished before its temporary directory is removed.
On cancellation/exit, retire the `QProcess` with bounded shutdown. No parsing
terminal percentage messages, custom delta reconstruction or upstream fork.

## Release signing and packaging

Signing is a prerequisite, not a second updater subsystem:

- Sign the first updater-enabled AppImage and subsequent releases with the
  release signing key. AppImageUpdate validates both images and requires a
  shared signer fingerprint. Existing unsigned AppImages need a one-time manual
  download of a signed, updater-enabled release.
- Use appimagetool's existing `--sign --sign-key <fingerprint>` support. Keep
  the dedicated private key/passphrase in protected release secrets and an
  isolated temporary signing home on the runner. Supply the passphrase using
  the tool's existing noninteractive mechanism. Include only the public
  key/signature in the image. Never put secrets in Docker layers, persistent
  build volumes, logs or artifacts; do not import into a developer keyring.
- Signing precedes `.zsync` generation, so checksums describe the finished
  signed image. The `.zsync` itself is not signed. Verify the expected signer
  fingerprint as well as output validity before release upload. The existing
  pinned runtime has the signature sections structurally; actual signing with
  the pinned tools still needs the execution proof below.
- Retain the same signing fingerprint for v1. Automatic key/subkey rotation is
  not supported by this proposal; document manual reinstall as recovery.
- Embed architecture/baseline-specific stable GitHub update information and
  produce/publish the corresponding `.AppImage.zsync` alongside the image and
  existing checksum. Keep the release draft until all matching assets exist.
- Bundle the pinned helper and required runtime dependencies only in AppImage
  packaging. Do not add a source-fetch dependency to every ordinary Linux build.
- Verify signed output before publication. Signing failure must fail the release.
  Development/CI images do not advertise the stable self-update action.

No additional signed JSON manifest, custom crypto, installer service, background
agent, update history database or automatic rollback system is proposed.

## Findings and explicit limits

Inspected AppImageUpdate revision:
`a211784dfc746fdb6d8d32d6bb39add451c1ddeb` (2025-10-18).
Its zsync2 revision is `fc62ae7851ef28dc390a57fbdbe90b0afc5e3102`.

- Core `Updater::stop()` throws "not implemented". Its worker/message lifetime
  makes direct integration more work than its API suggests. Process isolation
  avoids owning those internals. The optional upstream GUI targets Qt5 Widgets;
  SunPlayer keeps its own Qt6/QML UI.
- CLI signature policy allows both-unsigned updates and warning states, including
  expired-key/missing-key warnings; do not assume every warning proves valid
  authentication. Signed release seeds are
  mandatory. The proposed policy uses upstream signer continuity; it does not
  claim stricter pinned-key/expiry enforcement. Changing this policy needs a
  deliberate decision, not stderr parsing or another signature stack.
- Upstream checks whether the stable artifact differs, not whether its semantic
  version is greater. The proposal follows **current published stable** and
  disables updating development builds. It does not promise monotonic-version
  enforcement or anti-rollback across republished/older stable releases.
- Upstream overwrites its input before validating signatures, hence staging.
  It also uses hardlinks internally for backups; unsupported storage must fail
  safely with an update error. No separate download fallback is provided.
- The signed package and one real signed helper update pass locally.
  Dependency cost includes libappimage, zsync2, CPR/curl and GPGME/GnuPG. This is
  the accepted packaging footprint; normal Linux builds do not link it.

## Delivery checks

- [x] Prove the pinned helper and signing tools build and run inside the
  package, then exercise one signed old-to-new update through the packaged
  helper. Controller staging/relaunch is checked separately through a controlled
  subprocess edge. Trust upstream signature verification; do not duplicate
  its wrong-key, unsigned-image or tampering test suite. If packaging requires
  an upstream fork or substantial custom machinery, reassess before UI work.
- [x] Add the single Linux owner and empty-screen UI, controller process tests
  and a real-helper smoke; extend existing Basic/Fusion shell tests for Empty,
  Opening, Ready, Error and non-player visibility and for cancel-on-media-open.
- [x] Add signing/update metadata and matching release assets. Prove helper/GPG
  dependencies work without host development tools or the user's keyring.
- [x] Exercise one old-to-new signed update through the packaged upstream helper.
  Controller tests separately cover clean outer-launcher environment and media
  cancellation. These are complementary checks, not one physical-desktop
  end-to-end update/restart run. Hardware HDR is unrelated to this contract.
- [x] Update packaging/UI/build/testing documentation, independently review the
  actual implementation and its failure cases, then report evidence. Commit,
  push and release require separate authorization.

## Plan review

Three read-only reviewers independently checked upstream/library correctness,
SunPlayer integration and tests, and simplicity. All pass the proposed scope.
Review removed claims stronger than the observed warning policy. Following
user direction, signature verification and its adversarial tests remain
upstream-owned. Local validation covers our packaging, UI, staging and relaunch
integration. The delivery evidence below records the completed checks.
The user approved implementation and requested a dedicated signing key stored
locally and in release-only CI secrets. The key is encrypted and kept in the
Git-ignored `.local-secrets/appimage-release/` directory (0700; files 0600).
The combined `sunplayer-appimage-signing-backup.txt` contains the encrypted
private export, passphrase, public export and fingerprint. The user confirmed
backing it up. Do not include these files in commits or artifacts.
GitHub environment `appimage-release` holds the key/passphrase secrets and public
fingerprint variable, with deployments restricted to branch `main`. Only the
release AppImage job selects this environment; ordinary builds use an empty
`appimage-build` environment. Trusted main workflow code remains the boundary
for environment access; GitHub does not restrict environment secrets by workflow
filename. Signing files are mounted separately read-only into the container;
local secrets are masked from the source mount, and GPG state stays on tmpfs.

## Sources

- [AppImageUpdate core and signing policy](https://github.com/AppImageCommunity/AppImageUpdate/blob/a211784dfc746fdb6d8d32d6bb39add451c1ddeb/src/updater/updater.cpp)
- [CLI result and signature policy](https://github.com/AppImageCommunity/AppImageUpdate/blob/a211784dfc746fdb6d8d32d6bb39add451c1ddeb/src/cli/main.cpp)
- [zsync2 replacement and check behavior](https://github.com/AppImageCommunity/zsync2/blob/fc62ae7851ef28dc390a57fbdbe90b0afc5e3102/src/zsclient.cpp)
- [appimagetool signing](https://github.com/AppImage/appimagetool/blob/854e19e951900cf141245b384d45735942521abe/src/appimagetool_sign.c)
- [Signing before zsync generation](https://github.com/AppImage/appimagetool/blob/854e19e951900cf141245b384d45735942521abe/src/appimagetool.c#L1127-L1168)
- [Official update integration and metadata](https://docs.appimage.org/packaging-guide/optional/updates.html)
- [AppImage environment contract](https://docs.appimage.org/packaging-guide/environment-variables.html)

Harness uses Tauri's updater, not AppImageUpdate. Its clean relaunch is relevant;
its separate CLI/server/Node distribution machinery is not needed here.

## Delivery evidence

- Full Linux Debug build and both QML lint targets pass.
- Basic/Fusion shell checks cover optional capability, available/updating/error
  presentation, action dispatch and suppression outside the initial screen.
- `appimage-updater` integration runs the real controller with a controlled
  subprocess edge: staged replacement, preserved permissions/path, clean outer
  launcher environment, media cancellation, and removal of lock/staging pass.
- A signed Docker package passes upstream output validation and exact expected
  signer fingerprint, bundled updater/GnuPG checks, QML verification and actual
  packaged audio/video playback smoke. It emits AppImage, `.sha256`, `.zsync`.
- A temporary loopback HTTP server and two small signed AppImages exercise the
  packaged upstream helper's real check/update path. New bytes match exactly;
  host command lookup is disabled. Log: `out/appimage-updater-live-smoke.log`.
  This validates packaging integration, not a new upstream crypto test matrix.
- GitHub Actions syntax passes actionlint 1.7.12 (shellcheck unavailable).
  Three independent reviewers pass Linux boundary, lifetimes, packaging,
  release-secret handling and simplicity. Review fixes disable lock age expiry
  and wait for completed restart output in the process test.
- Initial-screen screenshot uses the actual QML with update availability
  simulated; it does not claim an already published updater-enabled release.
- GitHub environment secret names and main-only deployment policy are verified.
  Hosted minor-release dispatch is authorized; its outcome will be recorded
  after the workflow runs.

The first signed updater-enabled release must be downloaded manually by users
of existing unsigned packages. No user data/configuration is migrated.
