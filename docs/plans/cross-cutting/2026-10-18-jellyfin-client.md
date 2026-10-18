# Jellyfin V1: connect, browse, play

Status: **Implemented; manual acceptance pending** — 2026-10-18.
This is the first Jellyfin increment, not a change to SunPlayer's existing V1
commitment. Scope clarified on 2026-10-18: a useful, simple browser and native
playback, with no speculative infrastructure. This checklist records the accepted
scope and replaces earlier scope sketches.

Evidence: [Jellyfin research](../../research/2026-10-18-jellyfin-client.md).

## V1 includes

* Manually enter one server URL; one saved account, with no account switcher
  or multi-account storage/coordination machinery.
* Username/password login and logout, isolated from browsing and playback.
* Remember the access token using QtKeychain. Never save the password.
  If secure storage is unavailable, offer session-only login.
* A nice, simple browser: select a library, movie or show, season, and episode.
  Show artwork, movie/show descriptions, and episode titles/descriptions.
  Use a small set of reusable views/cards/rows and ordinary back navigation.
* Show watched/unwatched state and available playback progress. Offer Play,
  Mark watched, and Mark unwatched through Jellyfin's existing APIs.
  Paginate results and load appropriately sized artwork lazily.
* Play the original file through authenticated HTTP(S) and existing FFmpeg.
  If Direct Play is unavailable, show a clear unsupported-playback error.
* Reuse existing player controls and embedded audio/video/subtitle selection.
  Resume from the server's saved position, with a simple Play from start action.
  Allow seeking only when the opened resource supports it; if a saved resume
  point cannot be reached, explain that and offer playback from the start.
* Synchronize final playback position when playback closes, ends, is replaced,
  or logs out, including orderly app exit. Send start/stop reports; no periodic
  progress timer, background synchronization, or offline outbox in V1.
* Responsive loading, cancellation, bounded network timeouts, buffering feedback,
  and clear errors. Local-file playback remains supported.

## Explicitly outside V1

* Quick Connect, server discovery, multiple saved accounts, account switching.
* Search, custom sorting, animated backdrops, and custom artwork caches.
  Library tiles/tabs, bounded home shelves and illustrated detail headers were
  subsequently authorized in the presentation expansion below.
* Automatic next episode, queues, playlists, recommendations.
* External/server subtitles, remuxing, audio conversion, video transcoding, HLS.
* Live TV, casting, remote control, offline downloads, local library databases.
* Automatic reconnect/retry systems, custom AVIO, disk caching, optimized
  in-place remote seeking, and unrelated buffering/performance redesigns.
* A server plugin or a generic media-service/provider framework.

Excluded features get no placeholder UI, implementations, or tests. Keep the
authentication and media-resolution boundaries below clean so Quick Connect
or transcoding can be added locally later, without a framework for either.
Add those features in separate plans only when requested. Network/authentication
errors remain explicit user-retry paths; never silently fall back to another
format.

## Small boundaries, clean interfaces

These are responsibilities, not mandatory separate classes or library targets.
Use concrete owners, small typed values, and existing Qt signals.

* **Jellyfin authentication:** one session owner handles password login, logout,
  and the current token. A successful login establishes the same session
  regardless of login method; future Quick Connect can use that boundary.
  No auth-provider hierarchy or account collection.
* **Jellyfin API/browser:** typed library/item pages, descriptions, artwork
  references, user state, and mark-watched/unwatched operations. Share Qt HTTP
  construction/authentication/parsing; Jellyfin remains the library authority.
* **Jellyfin playback adapter:** resolves an item into a playable media input,
  keeps item/play-session identity, and reports final position. Direct Play is
  the only resolver behavior now; a later transcode choice belongs here rather
  than in the UI or decoder. No strategy registry or unused resolver classes.
* **UI:** reusable browser views and typed navigation models/user commands.
  No raw JSON, authorization, or playback policy in QML. Use existing Qt image
  loading/cache facilities; expose safe artwork references, not tokens.
* **Credentials:** a small asynchronous QtKeychain adapter reads/writes/deletes
  tokens. Ordinary settings hold non-secret server/user/device metadata.
* **Shared playback:** one small media-input value carries locator
  and safe display label through every open/restart path. Native open policy
  centralizes protocol/time/TLS options; authenticated URLs remain internal.
  The player accepts this source-neutral reference plus an optional starting
  position; local files and Jellyfin use the same playback engine. FFmpeg owns
  media I/O; playback knows nothing about Jellyfin IDs or endpoints.
* **Application:** constructs owners and connects play/close actions. No global
  service locator or additional orchestration framework.

Jellyfin -> shared media input -> existing MediaSession/FFmpeg/render/audio.
Do not add a second reader, decoder, clock, playback state machine, or library.
Prefer Qt Network/JSON, QtKeychain, native FFmpeg protocols, and existing
player signals/models. Do not create a separate abstraction for each endpoint
or reporting operation; do not implement a service plugin system.

## Essential contracts

* Preserve asynchronous open/cancel and generation-scoped restart ownership
  from [ADR 0006](../../decisions/0006-asynchronous-media-session-and-stable-active-video-source.md)
  and [ADR 0009](../../decisions/0009-generation-scoped-seek-restart.md).
  One generation owns one A/V demux operation. Reuse reopen-based seeking.
* Cancel superseded browse/play requests. A late response cannot replace a
  newer item or local file. Logout closes Jellyfin playback and clears its data.
* Keep Jellyfin item/play-session identity outside the decoder. Seek/recovery
  is not a new Jellyfin playback session. Use tracks discovered by FFmpeg;
  do not apply server track indexes as local decoder indexes.
* Capture final position from the existing playback clock before clearing or
  replacing media. Send it in the Jellyfin stop report while the account
  context exists. Normal app exit allows a bounded asynchronous completion
  window; do not just enqueue a request immediately before destroying networking.
  Never block indefinitely or retry in the background. Crash/network failure
  may lose progress since the previous close; disclose failed synchronization.
* Read resume position and watched state from Jellyfin, convert time units once
  at the boundary, and refresh affected browser user data after a successful
  stop or watched-state mutation. Let Jellyfin determine automatic completion;
  do not duplicate its watched-percentage policy or maintain a second database.
  Explicit mark watched/unwatched uses its API and reports failure honestly.
* Keep close/report ordering simple: one final update per logical playback,
  scoped to its item/session; do not let a stale completion overwrite another
  item's UI. Apply final stop before subsequent explicit watched-state changes
  for that item so an older report cannot undo the user's newer action.
* Keep secrets out of QML models, public media URLs, logs, native media metadata,
  and persisted media state, including FFmpeg diagnostics. Validate schemes and
  credential handling across redirects. Never ignore certificate errors.
* No plaintext credential fallback. Report failed token saves/deletions honestly;
  failed removal must not silently restore a logged-out account next launch.
* Keep the existing bounded packet/frame queues. Use existing interruption
  semantics for network starvation, including video-only content. No new cache.

## Delivery checklist

* [ ] Choose and record one supported Jellyfin server version for the initial
  release and its HTTP(S) policy. Preserve reverse-proxy base paths.
* [ ] Add media input through open, seek, track changes, replay, and recovery;
  verify packaged FFmpeg HTTP/HTTPS/TLS on Windows, macOS, and Wayland Linux.
* [ ] Add login/logout and token storage, then prove one item plays end to end.
* [x] Add the reusable browser with lazy artwork, descriptions, watched/progress
  state, and mark watched/unwatched. Add resume and final-position sync.
  Stop adding features when the V1 includes list is satisfied.
* [ ] Verify real FFmpeg HTTP playback: seek/pause, nonseekable input, stall,
  timeout, disconnect, cancellation, rapid replacement, and shutdown.
* [ ] Verify a real Jellyfin server: browse/play SDR and HDR originals, embedded
  track selection, local-file replacement, logout, and seek/recovery session
  identity. Confirm Direct Play and retained original color metadata.
* [ ] Verify show/season/episode navigation, large-library pagination, missing
  artwork/descriptions, failed images, and back navigation. Verify resume,
  Play from start, partial playback then close/reopen, end-of-media completion,
  explicit watched/unwatched changes, and truthful failed-sync feedback.
* [ ] Prove final-position reporting on replacement, logout, and orderly app
  exit completes or reaches its bounded failure outcome before teardown.
* [ ] Verify remembered login, rejected token, failed save/delete, and unavailable
  keyring. Use isolated test settings/secret entries; test native stores on the
  supported platforms. Check secrets do not escape diagnostics or metadata.
* [ ] Preserve local playback tests; check packaged dependencies; update relevant
  subsystem READMEs and testing docs with actual behavior and coverage gaps.

Use real dependencies and public boundaries; control HTTP/credential edges only
where needed for deterministic failures. No personal keyring/config changes in
unattended tests. Do not build a fake Jellyfin implementation for every endpoint.

## Activation and evidence

The feature list above reflects the requested V1. Before implementation, choose
the initial server version and HTTP(S) policy. Re-estimate after the packaged
HTTP playback spike; earlier brainstorm estimates are not delivery commitments.

Platform/dependency constraints: Linux uses system packages only (no vcpkg);
QtKeychain Qt 6 development package was installed by the user. Windows/macOS
use their existing Qt installations; no second Qt distribution. Consult the
user before adding any further dependencies or changing dependency sourcing.
Build and test only Linux on this host; do not attempt Windows/macOS builds or
verification here. Native-host acceptance remains explicitly unverified.

Implementation is present with accepted ownership in the
[Jellyfin subsystem](../../subsystems/jellyfin/README.md) and
[ADR 0036](../../decisions/0036-jellyfin-direct-play.md). Linux uses Qt 6.10.2,
FFmpeg 8.0.1 ABI libraries and system QtKeychain 0.15.0. Linux targeted HTTP,
TLS, resume, report ordering, auth-loss and local playback/QML regressions pass.
Full-suite and final AppImage results are recorded below after final validation.

The user will perform real Jellyfin acceptance against their own server;
server version is not yet known, so no tested-version compatibility claim is
made. HTTP and HTTPS are permitted for explicitly entered servers, with base
paths preserved and HTTPS certificate validation required. No disposable
Jellyfin container was run. Tests use controlled HTTP/TLS edges and real media
dependencies, not a server-policy emulator.

Windows/macOS recipes were edited only, with no builds/tests on this host.
Native keychain save/restore/delete/failure and real-server hierarchy/artwork,
completion policy and orderly window-close report acceptance remain pending.
The delivery checklist above intentionally retains those acceptance gates.

Review fixes: retain logical playback identity across retry; reject stale play
resolution after local replacement; serialize old final report before new item
start/explicit watched; scope callbacks to adapter lifetime; close playback on
401 before a new login; sample final clock before decode/audio/presentation
error resets; remove obsolete runtime property lookup in favor of typed
constructor injection. Independent correctness, simplicity and evidence reviews
informed these fixes. No speculative service/provider machinery was added.


## Final Linux delivery evidence (2026-10-18)

* `cmake --build build/linux-debug -j 6`: passed with the installed system
  Qt 6.10.2, FFmpeg 8.0.1 ABI libraries and QtKeychain 0.15.0-2.
* `cmake --build build/linux-debug --target all_qmllint -j 6`: both production
  and test QML lint targets passed.
* `ctest --test-dir build/linux-debug --output-on-failure -j 1`: 40/41 passed
  in 93.96 seconds. The sole failure was `linux-native-media-controls` private
  bus initialization: AppArmor policy lookup was blocked by this sandbox's
  read-only system view, before its behavioral tests ran.
* The same unmodified media-controls executable passed all four Qt Test
  entries with `dbus-run-session --config-file=/tmp/sunplayer-jellyfin-test-bus.conf
  -- env QT_QPA_PLATFORM=offscreen
  build/linux-debug/tests/sunplayer_linux_native_media_controls_tests`.
  The temporary config included `/usr/share/dbus-1/session.conf` and disabled
  AppArmor only for that disposable private test daemon. No host configuration
  or project test policy changed.
* `jellyfin` passed all nine behavior scenarios plus initialization/cleanup
  (11 Qt Test entries, no skips). Real local decoding, session regressions,
  QML shells under default/Fusion styles, media dependency protocol assertions,
  packaged-QML and application A/V scenarios passed in the full run.
* Rendering test capability skips remain honest: three VAAPI rows lacked
  Vulkan DRM PRIME import on this execution device; five Windows/macOS rows
  were platform-specific. No new native hardware evidence is claimed.
* `packaging/appimage/build.sh out/appimage-jellyfin-v1`: final unsigned
  Release AppImage build passed. Docker client configuration was isolated under
  `/tmp`; the build did not write the user's Docker configuration or use signing
  keys. The separate Weston/lavapipe/Pulse smoke runtime passed bundle resolution
  for QtKeychain, runtime-loaded libsecret dependency closure, Qt TLS plugin
  presence, desktop icon integration, packaged QML and production A/V playback
  (position 1509 ms, Pulse sink, exit zero).
* Output: `out/appimage-jellyfin-v1/SunPlayer-1.4.1-ubuntu26.04-x86_64.AppImage`
  and matching `.sha256`; `sha256sum --check` passed. No signing, publishing,
  committing or pushing was performed.
* Final independent correctness, simplicity/ownership and evidence/security/docs
  reviews report no remaining blocking findings. `git diff --check` passed.

Manual gates above remain intentionally open: user's own Jellyfin server,
native keychain persistence/failure, orderly quit against that server, and
Windows/macOS builds/runtime acceptance. No Windows/macOS build or test was
attempted here. Planning and unrelated native-media-controls plan edits were
preserved.


## Login polish after user testing (2026-10-18)

The remember-login checkbox now supplies both Qt text palette roles on the dark
page, matching existing HDR Lab controls across styles. Scheme-less server
addresses default to HTTPS; explicit HTTP(S), ports and reverse-proxy base
paths are retained. There is no automatic HTTPS → HTTP fallback. Redirect
policy remains unchanged: login/API/artwork reject redirects and require the
final server URL; native media redirects do not forward original credentials.
Regression checks cover actual checkbox text contrast under default/Fusion
styles and address normalization through the browser login boundary.


## Simplify original-file resolution after acceptance report (2026-10-18)

The user reported most real movies rejected before FFmpeg opened them. Review
of upstream server code found concrete defects in our posted profile: omitted
bitrate fields inherit 8 Mbps limits, and missing subtitle profiles can require
burn-in. V1 has no alternate playback format, so remove this unnecessary device
negotiation entirely rather than add caps, codec/subtitle tables or fallbacks.
Use basic GET playback info and the static original stream; native FFmpeg remains
the format authority. Source availability/authorization still belongs to the
server. Regression checks enforce the GET/empty-body/static-original contract.
See the [research correction](../../research/2026-10-18-jellyfin-client.md#correction-after-original-file-acceptance-failure-2026-10-18).

Follow-up verification: Linux Debug build and both QML lint targets passed.
The default/Fusion QML shell scenarios passed with the actual remember-checkbox
content color assertion; the Jellyfin integration suite passed with implicit
HTTPS/explicit HTTP coverage and the GET/zero Content-Length/static-original
request assertions. Independent correctness, simplicity and evidence reviewers
found no remaining blocking issue; the evidence review tightened the no-body
assertion to check wire Content-Length instead of an empty parsed JSON object.

A new unsigned AppImage at
`out/appimage-jellyfin-v1-fixes/SunPlayer-1.4.1-ubuntu26.04-x86_64.AppImage`
passed bundle/dependency/QML/A/V smoke (1518 ms, Pulse sink, exit zero) and its
SHA-256 check. A preceding attempt to overwrite the original output failed with
`Text file busy` because that package was running; the running application was
not stopped or modified. The new directory preserves the previous test build.
Real-server confirmation of the movie-playback correction remains pending.


The final no-body regression passed after checking actual Content-Length. One
run exposed a pre-existing test race: its starvation predicate advanced the
presentation clock as a side effect, allowing successive QTRY evaluations to
alternate buffering/recovery while decode continued. The test now waits for
ordinary playback monitoring to drain the withheld HTTP response naturally;
all integration scenarios pass (6.84 seconds), without a production change.

The user requested one AppImage output folder. Use `out/appimage/` for subsequent
builds; do not create feature-specific output directories. The earlier output
directories were absent when consolidating, so the final build is regenerated
at the standard path below. Historical paths above record earlier runs only.

Final standard-path rebuild succeeded:
`out/appimage/SunPlayer-1.4.1-ubuntu26.04-x86_64.AppImage` plus its `.sha256`.
The isolated packaged QML/dependency/A/V smoke passed (1517 ms, Pulse sink,
exit zero), and `sha256sum --check` passed. This is the current test artifact.


## CSD overlay placement and playback Back (2026-10-18)

User acceptance found active-player navigation/details underneath client-side
chrome. Keep full-window video stable but give the Back button and details
panel a shared chrome-derived top inset (and reduce details maximum height).
Browsing/empty pages retain their existing page inset; native decorations and
fullscreen use zero. No platform-specific UI fork is added.

Move Jellyfin entry to the empty Open video screen. Active playback uses Back,
closing the media and returning to its opener page. A single QML return-page
value suffices; direct launch defaults to Open video. Keep the browser model
and navigation alive across playback. Browser Back returns to Open video and
cancels pending media resolution. Regression coverage checks actual geometry
and local/Jellyfin/direct-launch routes in the existing shell harness.

## Browser presentation expansion (authorized 2026-10-18)

The user supplied Jellyfin-web reference screenshots and requested library
home tiles, top library tabs (Movies/Shows), poster galleries, and illustrated
movie/show/season pages with episode rows. They explicitly approved Continue
Watching and Recently Added. This supersedes the initial exclusion of home
feeds/detail screens, without adding search, favorites, queues or transcoding.

Implementation: retain the paged QAbstractListModel and navigation owner. Expose
bounded home shelves/library tabs as small QVariantList snapshots of typed
items, sharing the same role/value mapping; no extra list-model hierarchy.
Qt Network fetches resume and recent Movie/Series items through server queries.
Movie/show/season headers reuse metadata/artwork and ordinary commands; only
movies/episodes play, with no speculative play-series or auto-next behavior.
Split reusable poster, shelf and detail/episode presentation into small QML
components. Native media/auth ownership stays unchanged. Add Primary/Backdrop
image handling only for concrete poster/header consumers. Preserve CSD-safe
content and Back-to-origin behavior. Validate public HTTP/model/UI boundaries,
QML lint, visual snapshots and the one-folder AppImage build.

The four QML components share the existing model mapping. Home queries are
bounded to 12 entries per shelf; library tabs preserve server names.
Movie entry fetches current item metadata/user state. Primary and Backdrop are
explicit image-provider variants with existing request/decode bounds. No new
dependency, cache, navigation framework, synchronization timer, or playback path
was introduced.

Three independent read-only reviewers checked correctness, simplicity and
evidence. Two findings were fixed: fresh movie detail retrieval on entry, and
superseding an older post-stop refresh before a deferred watched mutation so
its response cannot prematurely clear the mutation's busy state. The latter
has a controlled delayed-mutation regression. Other review gaps are scoped
manual acceptance (real artwork/server behavior); recent-query assertions and
a 640-pixel compact capture were added. Missing poster artwork is represented
in the movie-gallery fixture.

Final Linux evidence for the presentation follow-up:
* Debug build and both `all_qmllint` targets passed.
* `jellyfin`, `qml-shell`, and `qml-shell-fusion` passed (3/3, 42.56 s).
  No QML warnings. Inspected populated home, missing-art movie gallery and
  800/640-pixel season captures under `build/linux-debug/tests/ui-snapshots/`.
* Local-media-drop, window-shortcut, active-video-source and packaged-qml-module
  regressions passed. Media-session initially failed the existing immediate
  `!seeking()` assertion in `interFrameSeekPublishesRequestedFrame`; the full
  unmodified media-session test passed on rerun (10.90 s). This is recorded as
  a timing-sensitive coverage issue, not a clean first-pass suite result.
* Rebuilt unsigned `out/appimage/SunPlayer-1.4.1-ubuntu26.04-x86_64.AppImage`;
  prior artifact retained with `-previous` in the same output folder. Container
  dependency, isolated packaged QML and A/V smoke passed (Pulse, position
  1546 ms, application exit 0); checksum verification passed.
* No Windows/macOS build/verification, personal-keyring operation, real-server
  test, new dependency, signing, commit or publication was performed.

## Touchpad/default-behavior cleanup and simplicity audit

The gallery rewrite unnecessarily set `Flickable.StopAtBounds` on Jellyfin
views. This removed Qt's normal overscroll gutter. Removed those overrides;
Qt owns scroll gestures, inertia and bounds behavior. Existing native wheel
forwarding passes the original event unchanged and was not modified. No wheel
handler, velocity calculation, animation or gesture-routing workaround was added.
Physical touchpad inertia still needs user acceptance; removing a bounds override
alone does not prove a reported inertia symptom is resolved.

The user requested an audit of speculative machinery. Three read-only reviews
covered UI, Jellyfin ownership, and media/packaging integration. Cleanup removes:
* Manual shelf-arrow/contentX paging, replaced with Qt's horizontal scrollbar.
* Description expansion state, arbitrary length/line thresholds and toggle UI;
  descriptions wrap fully within the existing scrolling page.
* Redundant image async/size declarations (the existing asynchronous provider
  owns bounded image sizes), empty poster background and inherited checkbox
  palette duplicates.
* Duplicate library data in the paged catalog, unused heading/overview browser
  properties, and redundant reply deletion. Catalog rows now contain only
  catalog entries; library selection uses the existing dedicated command.

The common API/image reply bounds share one local helper. FFmpeg logging rejects
messages above warning level before formatting messages it would discard.
Review retained actual security/lifetime/protocol boundaries: authenticated image
loading, original media input, stale-request rejection, bounded I/O, reporting
ordering and secure token storage. No new framework or dependency was introduced.

Cleanup verification: Linux Debug build and both QML lint targets passed.
`jellyfin`, `qml-shell`, `qml-shell-fusion` and `packaged-qml-module` passed
4/4 (42.77 s), including inherited checkbox contrast and populated browser
captures. Focused independent UI review found no material behavior regression.
The rebuilt executable is `build/linux-debug/sunplayer`; the existing AppImage
predates this cleanup and has not been rebuilt for this follow-up.


Scrolling investigation closed by user choice: the standalone stock-Qt window
felt equally stiff, and the user chose to retain defaults. No Qt patch or
sensitivity override was applied. Temporary comparison programs, downloaded Qt
source and diagnostic logs were removed; accepted implementation, tests, Debug
build and AppImage artifacts remain. Detailed upstream findings are in the
research note, not application-side workaround code.


## Final blocker review before commit

Three independent read-only reviews covered correctness/security/lifetimes,
architecture/simplicity, and tests/docs/packaging. One blocker was found and
fixed: Linux CI's apt list lacked the already-required `qtkeychain-qt6-dev`
package. Local system and AppImage dependency lists already included it. No
new dependency or sourcing strategy was introduced. Correctness and simplicity
reviews found no remaining substantial issue. Hosted CI execution and the
previously documented real-server/native-platform acceptance gaps remain open.
The unrelated native-media-controls plan changes are excluded from this commit.
