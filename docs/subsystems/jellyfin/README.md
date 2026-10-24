# Jellyfin client

Application settings offers a saved “Hide Jellyfin” checkbox on all platforms,
default off. This is shell visibility policy: it hides entry/history navigation
and leaves an open browser page for Player. It preserves the browser, login and
active playback; Jellyfin services do not own or interpret this preference.

## Supported slice

One manually entered HTTP(S) server and account, username/password sign-in,
optional remembered token, and sign-out. HTTP is permitted for explicitly
entered servers; HTTPS verifies certificates and never ignores TLS errors.
Addresses without a scheme default to HTTPS; explicit HTTP is preserved, with
no automatic downgrade. Reverse-proxy URL base paths are retained. Login/API
and artwork redirects are rejected; enter the final server address. Original
media redirects are handled by FFmpeg without forwarding the original token
query or a custom authorization header to the redirect target. No server
extension is required.

The home screen offers library tiles/tabs, Continue Watching, and Recently Added
movie/show shelves. Poster galleries and illustrated detail headers provide
library → movie/show → season → episode navigation,
lazy artwork, descriptions, watched state, saved progress, Play/Resume, Play from
start, and explicit watched/unwatched actions. Pages contain 40 items and a
Load more action. Missing artwork keeps the title/description usable.
Backdrops use the item's own image when present, otherwise the server-provided
parent backdrop owner/tags, so season episode-page headers retain show artwork.
This follows Jellyfin's [backdrop selection](https://github.com/jellyfin/jellyfin-web/blob/master/src/utils/jellyfin-apiclient/backdropImage.ts)
for own and inherited backdrops without an extra metadata request.
Jellyfin is authoritative for library and user data; there is no local database.

Original-file Direct Play uses the ordinary player and embedded track controls.
Resolution uses basic `GET /Items/{id}/PlaybackInfo` for source/session identity,
then the authenticated `Videos/{id}/stream?Static=true` resource. There is no
posted device profile, client bitrate cap, codec allowlist or server subtitle
conversion. Native FFmpeg decides whether the original media can be decoded;
ordinary server authorization and source availability still apply.
A source requiring opening, remuxing, or transcoding is rejected explicitly.
Resume uses the server position unless the item is watched. A resource must
support native seeking to resume; an unsuccessful open/seek offers Retry from
start. There is no automatic next episode, HLS, external subtitle delivery,
search, account switching, or background sync.

## Ownership and interfaces

* `JellyfinClient` owns one authenticated Qt Network session, API construction,
  JSON validation, typed item/playback values, and HTTP timeouts. Password login
  establishes the same session used by every consumer. A later login method can
  establish that session without changing the player or browser.
* `TokenStore` exposes queued read/write/delete callbacks. CMake selects the
  independent Windows PasswordVault implementation or Linux/macOS QtKeychain
  implementation; native APIs never enter the browser/client header. It stores
  only the access token, with no insecure fallback. Native credentials use
  service `SunPlayer`, entry `jellyfin-token`; ordinary application settings
  hold server/user/device metadata and a remember flag. Failed save leaves a
  usable session-only login. Sign-out disables restoration before deletion;
  storage/settings errors are reported. No password is persisted.
* `JellyfinBrowser` borrows the application-owned SettingsStore for its metadata,
  with no backend/path/format reconstruction. It owns the item model, navigation, current request identity,
  loading/errors, remembered-session metadata, and commands. Browser history
  retains visited typed paths plus a cursor; toolbar Up follows ancestors, while
  mouse/keyboard Back/Forward reload visited locations; new
  visits discard the forward tail, and auth loss/sign-out clear it. The existing paged
  model owns catalog items only; three bounded home shelves and library tabs use
  value snapshots through the same item mapping. Home requests fetch at most
  12 items per shelf, with resumable Movies/Episodes and newest Movies/Series.
  Partial shelf failures leave libraries usable; authentication loss clears all
  snapshots. Entry/Refresh fetch current data without background timers.
  QML receives typed
  roles and commands, never JSON or credentials.
* `JellyfinImages` is a Qt asynchronous image provider using the client on its
  owning thread. Requests are bounded to 8 MiB/10 seconds; decoded image sizes
  are bounded and scaled. Qt owns ordinary image loading/cache policy.
* `JellyfinPlaybackAdapter` resolves an item to `MediaInput` plus initial
  position, retains item/source/play-session identity, and reports playback.
  `MediaSession` and FFmpeg contain no Jellyfin API logic. A later transcode
  resolver can change this adapter/client boundary without changing the player.
* `PresentationWindow` owns browser and adapter alongside MediaSession. Typed
  constructor injection supplies the browser to rebuilt QML scenes; graphics
  recovery does not reconstruct the authenticated session.

`MediaInput` carries an internal locator and safe display title. Jellyfin uses
an authenticated URL query for native FFmpeg, avoiding reusable custom headers
being forwarded to a different origin by redirects. Public player/native
metadata exposes only `media:remote`; the FFmpeg logging boundary redacts URLs
and authorization. API/artwork redirects are refused. See
[media input](../media-io/README.md) and [ADR 0036](../../decisions/0036-jellyfin-direct-play.md).

## Reporting and failure behavior

Send a start report when the item is ready and playing, then one final report
on close, stop/end, replacement, error, sign-out, or orderly app exit. Capture
the existing clock before resetting it. Seek, track changes, and graphics
recovery retain the logical item identity. Replay starts another occurrence.
Final reports use milliseconds → Jellyfin ticks conversion at the API boundary;
Jellyfin decides completion/watched policy. Refresh browser user data after a
successful report or explicit mutation. No periodic timer or durable outbox.

Start/stop requests are serialized for an occurrence. An explicit watched
mutation for the playing item closes it and waits for its older stop first.
Authentication loss closes old playback and keeps login unavailable until old
reports settle, so another account cannot receive them. Superseded resolutions
cannot replace a newer local file. Requests use a 10-second bound; media reads
use a 15-second native FFmpeg timeout plus cooperative cancellation.

Normal window close/application quit allows at most three seconds for pending
final reports, then warns and exits. Network failure, forced termination, and
crashes can lose progress; sync failure is shown without automatic retries.
Remote video-only starvation freezes its monotonic clock at the available
frame horizon and uses existing Buffering state; audio uses the existing sink
underrun behavior. There is no second reader, clock, cache, or retry engine.

## Dependencies and packaging

Linux uses system `qtkeychain-qt6-dev`, never vcpkg. macOS uses the approved
QtKeychain vcpkg overlay against the already installed Qt SDK, without vcpkg Qt.
QtKeychain uses macOS Keychain or Linux Secret Service/KWallet. Windows uses
PasswordVault through the existing Windows SDK/C++WinRT and adds no dependency
or capability. The Windows implementation owns local WinRT initialization,
queues operations on its QObject, and releases native objects before completion.
Missing deletion succeeds; other failures include a safe HRESULT. Tokens stored
by the former Windows Generic Credential backend are not migrated; sign in
again after this backend change.

The AppImage bundles QtKeychain and explicitly deploys runtime-loaded libsecret
and its dependencies. Its Qt TLS backend and native FFmpeg TLS libraries are
bundled with the existing Linux dependency deployment. A host session bus and
working host keyring service are required for remembering a login; unavailable
secure storage produces session-only login, never a plaintext fallback.
Windows uses FFmpeg's existing Schannel configuration. The native FFmpeg port
adds explicit Secure Transport on macOS because its configure disables
autodetection. No new OpenSSL dependency is introduced for those platforms.
Native builds/runtime checks for these two recipes remain unverified here.

## Verification and acceptance gaps

`jellyfin` exercises production Qt HTTP, browser, adapter, MediaSession and real
FFmpeg with controlled loopback HTTP/range/TLS edges: login/header/base path,
original-source GET/static stream, exact resume preroll, cancellation, replacement report
identity/position/order, watched mutation ordering, authentication loss before
new login, video-only stall/recovery, redirect credential isolation, untrusted
TLS rejection, and diagnostic redaction. Tests use temporary INI settings,
session-only login, and no personal keyring. QML shell tests exercise the login
page and absence of a video viewport there. Populated Basic/Fusion shell
captures cover home, poster galleries, show/season details, compact episode
rows, CSD-safe overlays and return navigation. Controlled HTTP cases cover
home queries, partial failure, movie detail retrieval and authentication loss. Existing playback tests cover
pre-reset final-position notifications for audio/presentation failures.

The user's real Jellyfin server acceptance remains pending: record its version;
verify hierarchy/artwork/pagination, original SDR/HDR playback and embedded
tracks, resume/close/reopen, automatic completion policy, watched/unwatched,
logout, and orderly quit reporting. Native secure-store save/restore/delete
and unavailable/failed-store behavior require explicit host acceptance.
Windows/macOS build, packaged HTTPS, native keychain and playback checks must
run on their own hosts. Linux/AppImage checks do not prove them. See the
[delivery plan](../../plans/cross-cutting/2026-10-18-jellyfin-client.md).

Linux delivery passed the Debug build, both QML lint targets, all 40 runnable
CTests in the full run and the remaining media-controls test on an isolated
private bus (its default sandbox run could not query AppArmor policy). The
unsigned AppImage passed dependency and packaged QML/A/V smoke checks plus its
checksum. See the delivery plan for exact commands, skips and evidence limits.

Home-shelf selections resolve `Items/{id}/Ancestors` before opening. Server roots
above the user library are omitted. Grouped UserView IDs may differ from physical
library ancestors; those selections retain the returned media hierarchy without
guessing a matching tab. Cancelling/replacing media invalidates a pending Home
selection as well as playback resolution.

## Windows integration and persistence

Windows uses SDK PasswordVault behind TokenStore; Linux/macOS retain QtKeychain.
The unused Windows QtKeychain build configuration remains explicitly deferred
until separately requested, avoiding dependency-cache churn. No legacy token
migration, insecure fallback or extra credential capability is needed.

Preferences, Jellyfin metadata and AppImage consent share one
application-owned SettingsStore. Packaged Windows uses SDK LocalSettings;
QSettings retains native storage elsewhere and temporary INI tests. Browser
consumers know neither backend paths nor formats. Temporary browsers skip
restore and use unique test token keys; production keeps its stable token key.

The user confirmed the installed Windows build works after this correction.
Hosted CI also builds/packages Windows and exercises PasswordVault through a
unique disposable desktop test entry. Detailed restart/logout steps are not
individually recorded by that general acceptance report; see
[testing coverage](../../TESTING.md) and the
[settings-store plan](../../plans/cross-cutting/2026-10-18-settings-store.md).
The prior registry/INI failures are recorded in the
[trace research](../../research/2026-10-18-appsilo-settings-rename.md).

Packages declare public-client and private-network access for Jellyfin. Network
failures without a response show the Qt error category rather than HTTP 0.
Credential and metadata errors are both preserved; native LocalSettings failures
log the operation and original HRESULT. Low-volume persistence-stage and vault
result logs contain no credentials, account/server values or settings contents.
About → Open logs opens the actual session file's folder.
