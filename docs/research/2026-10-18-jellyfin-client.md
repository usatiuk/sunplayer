# Jellyfin client: login, browsing, streaming, and credentials

Date: 2026-10-18. Status: initial exploratory research and brainstorm.
Implementation was subsequently authorized; current behavior and evidence live
in the [Jellyfin subsystem](../subsystems/jellyfin/README.md) and
[delivery plan](../plans/cross-cutting/2026-10-18-jellyfin-client.md).
The investigation limits below describe this research phase, not later tests.

## Question and evidence limits

Can SunPlayer connect to an ordinary Jellyfin server, browse its library, and
play content with the existing native HDR pipeline? Yes: the public APIs cover
the required operations; a server extension is unnecessary.

This investigation inspected project docs/code and official Jellyfin SDK/server
sources plus QtKeychain documentation. No live-server interoperability,
network-playback experiment, credential-store experiment, or packaged HTTPS
test was performed. Upstream documentation describes current upstream behavior,
not a verified SunPlayer server-compatibility matrix. Select and test the
supported Jellyfin versions before implementation acceptance.

## 1. Login

Jellyfin supports password authentication and Quick Connect. Password login
through `POST /Users/AuthenticateByName` returns an access token and user/session
information. Subsequent requests use that token with the client/device identity.
Keep the password only for the login request, rather than persisting it.

Quick Connect requests a code, lets an already signed-in client approve it,
then exchanges a private session secret for an access token. The displayed code
and private secret are different values. Servers can disable the feature.
Password login is the smaller first slice; Quick Connect can follow independently.

Provide a persistent client-generated device ID, application name/version, and
device name following the official SDK authorization format. Rejected/revoked
credentials require login again. Do not assume OAuth refresh-token behavior.
Logout includes server logout and local secret removal; offline local removal
cannot guarantee server revocation.

Sources: [authentication guide](https://kotlin-sdk.jellyfin.org/guide/authentication.html),
[user controller](https://github.com/jellyfin/jellyfin/blob/master/Jellyfin.Api/Controllers/UserController.cs),
[SDK authorization implementation](https://github.com/jellyfin/jellyfin-sdk-typescript/blob/master/src/api.ts),
[session controller](https://github.com/jellyfin/jellyfin/blob/master/Jellyfin.Api/Controllers/SessionController.cs).

## 2. API and library browsing

Existing APIs provide user libraries, item queries, search, sorting, pagination,
show/season/episode navigation, metadata, artwork, latest items, and resume
items. Continue watching also requires the client to report real playback
progress. Jellyfin remains the library and watched-state authority; SunPlayer
does not need a second library database or metadata scraper.

Use a small typed Qt Network/JSON client for the endpoints actually consumed.
Official Kotlin/TypeScript SDKs and OpenAPI are references. A generated C++
client is an alternative only if its generated/build surface proves smaller
and easier to maintain; importing another language runtime is unnecessary.
SDK/server compatibility is versioned, so unqualified use of the latest schema
is not a compatibility policy.

The browser's real work is native navigation, paginated models, lazy artwork,
and loading/error states. Start with libraries, movies, shows, item details,
search, and resume. Reuse Qt's image/network facilities and bound any cache;
do not start with offline synchronization or a custom cache framework.

Sources: [official SDK and compatibility table](https://github.com/jellyfin/jellyfin-sdk-typescript),
[user views](https://kotlin-sdk.jellyfin.org/dokka/jellyfin-api/org.jellyfin.sdk.api.operations/-user-view-api/index.html),
[item query fields](https://kotlin-sdk.jellyfin.org/dokka/jellyfin-model/org.jellyfin.sdk.model.api.request/-get-items-request/index.html),
[image API](https://typescript-sdk.jellyfin.org/classes/generated-client.ImageApi.html).

## 3. Streaming

Initial approach (superseded by the correction below): request
`POST /Items/{id}/PlaybackInfo` with an honest device profile, selected
source/tracks, resume target, and relevant bitrate limits. Resolve the response
into an authenticated HTTP resource or server-generated stream. Report playback
start/progress/stop, retaining the server's play-session identity across ordinary
local decoder restarts. Stop server transcoding resources when those exist.

Jellyfin distinguishes original-file Direct Play, container-only remux, audio
conversion with video copying (Direct Stream), and video transcoding. Current
documentation says HDR video transcoding requires HDR-to-SDR tone mapping and
does not preserve HDR metadata. Original-file Direct Play is therefore the best
first fit for SunPlayer. Remuxed output still needs metadata verification;
unchanged video packets alone do not establish every dynamic-HDR outcome.

Sources: [playback negotiation](https://typescript-sdk.jellyfin.org/classes/generated-client.MediaInfoApi.html),
[device profile](https://typescript-sdk.jellyfin.org/interfaces/generated-client.DeviceProfile.html),
[media source response](https://typescript-sdk.jellyfin.org/interfaces/generated-client.MediaSourceInfo.html),
[session reporting](https://typescript-sdk.jellyfin.org/classes/generated-client.SessionApi.html),
[playback modes and HDR limits](https://jellyfin.org/docs/general/post-install/transcoding/).

### Fit to the current player

* `MediaSession` rejects non-local URLs. Decoder requests carry filesystem paths
  and FFmpeg open currently supplies no authentication/timeouts dictionary.
* One generation already owns one demux operation for audio/video; embedded
  video, audio, and subtitle selection exists. External/server subtitles do not.
* Seek, track changes, and graphics/software recovery reopen/reprobe the source.
  Preserve this ownership model initially and measure remote latency.
* The 8,192-packet/512-MiB encoded router is bounded post-demux read-ahead, not
  a byte cache. Three-frame decoded-video buffering must not grow to hide I/O.
* General network stalls, bounded open/read failure, video-only buffering, and
  seekability need explicit coverage. Verify packaged FFmpeg HTTP/HTTPS/TLS
  support on all three platforms; system Linux success is insufficient.
* Jellyfin source/track indexes must not be assumed equal to FFmpeg indexes,
  especially after remux/transcode.

Use the already documented [media-input direction](../subsystems/media-io/README.md):
a small locator/options/safe-label value and native FFmpeg protocols first.
No decoder, renderer, audio clock, or compositor rewrite is indicated. Custom
AVIO, byte caching, persistent demux seeking, and automatic reconnection require
measured problems before adding complexity.

Local evidence: `src/playback/MediaSession.cpp`,
`src/media/FfmpegMediaDecoder.cpp`, `src/media/FfmpegVideoDecoder.h`,
`src/app/ApplicationSettings.h`, `vcpkg.json`, and the media, media-io,
application, UI, and subtitle subsystem READMEs. Existing seek ownership is
defined in [ADR 0009](../decisions/0009-generation-scoped-seek-restart.md).

## 4. Cross-platform credential storage

QtKeychain offers a Qt 6 API over Windows Credential Store, macOS Keychain,
and Linux libsecret/GNOME Keyring/KWallet integrations. Its asynchronous
read/write/delete jobs and distinct errors fit Qt without custom native
implementations. Keep insecure fallback disabled.

Store only access tokens in secure storage. Existing `QSettings` can retain
server URL, user/device IDs, and a credential lookup key. Scope entries to the
server/account, preserving a reverse proxy's base path. Avoid token-bearing
URLs, headers, or passwords in logs, QML models, persisted media state, and
user-visible error strings.

Linux Wayland does not imply a running or unlocked keyring. If secure storage
is unavailable or access is denied, offer explicit session-only login or a
user-triggered retry. A failed secret write must not claim successful remembered
login; a failed deletion must not claim successful credential removal.
Packaging must include library dependencies while using the host's keyring.

Sources: [QtKeychain backends and fallback policy](https://github.com/frankosterfeld/qtkeychain),
[asynchronous API and errors](https://github.com/frankosterfeld/qtkeychain/blob/main/qtkeychain/keychain.h),
[Secret Service locking](https://specifications.freedesktop.org/secret-service/latest/unlocking.html).

## Effort, alternatives, and open questions

Scope update, 2026-10-18: the current companion plan supersedes the temporary
text-only scope. V1 includes artwork, movie/show and episode descriptions,
watched/progress display, explicit mark watched/unwatched, resume, and final
position sync on close. One account and Direct Play remain the limits. Search,
home feeds, periodic sync, Quick Connect, and transcoding remain deferred.

Jellyfin already exposes explicit played/unplayed mutations and playback-stop
reports with position; no separate watched-state service/database is needed.
Validate close-only sync, resume/completion semantics, and server policy against
the chosen server version. A normal app exit must allow bounded report completion;
crashes or unavailable networking can lose progress. Refresh server user data
after successful mutations rather than inventing client completion thresholds.
Source: [server playstate controller](https://github.com/jellyfin/jellyfin/blob/master/Jellyfin.Api/Controllers/PlaystateController.cs).

Quick Connect is the device-code-style future option (not an assumption of
standard OAuth device flow). Both password login and Quick Connect yield a
session token, so isolate login acquisition from the current session/browser.
Resolve playable inputs separately from playback so a future transcode choice
can be implemented in that adapter, with its required lifecycle work. Neither
extension needs a provider registry or unused implementations today.

Engineering judgment: a LAN Direct Play proof of concept is days once login is
available. A useful first cross-platform version is roughly 3–6 engineer-weeks,
including native browsing, persistence, network failures, and packaging checks.
This is not measured delivery evidence; broader HLS/transcoding/subtitle support
and polished large-library browsing can add several more weeks.

The original proposal was one active server/account, password login, native
browsing, original-file playback with embedded tracks, and resume/progress.
The current V1 keeps that useful browsing/resume experience while limiting sync
to final-position reporting and avoiding multi-account or transcode machinery.
Avoid a server plugin, embedded full web client, generic service-provider
framework, custom crypto, or duplicate local library. See the companion plan
for boundaries, sequence, validation, and decisions still needed.


## Correction after original-file acceptance failure (2026-10-18)

The initial device-profile POST was unnecessary for a client that only opens
original files. Jellyfin's DeviceProfile defaults both MaxStreamingBitrate and
MaxStaticBitrate to 8,000,000; leaving them unspecified imposed that limit.
StreamBuilder also treats a selected subtitle with no compatible subtitle
profile as requiring encoding, which disqualifies Direct Play. These explain
concrete rejection paths in our request; the user's exact server response was
not captured, so neither is claimed as the proven cause for every tested film.

The simpler V1 resolution is basic GET playback info for source/session IDs,
followed by the static original stream. MediaInfoController's GET path returns
basic source info without SetDeviceSpecificData/profile negotiation. It adds no
client bitrate or subtitle-conversion policy. Native FFmpeg handles the file;
no transcoding/remux fallback, profile registry or codec matrix is added.

Primary sources reviewed:

* [DeviceProfile defaults](https://github.com/jellyfin/jellyfin/blob/master/MediaBrowser.Model/Dlna/DeviceProfile.cs).
* [StreamBuilder subtitle/direct-play decisions](https://github.com/jellyfin/jellyfin/blob/master/MediaBrowser.Model/Dlna/StreamBuilder.cs).
* [MediaInfoController GET versus POST](https://github.com/jellyfin/jellyfin/blob/master/Jellyfin.Api/Controllers/MediaInfoController.cs).

Regression evidence checks the outgoing GET, empty request body, UserId query,
and static stream parameter at the real Qt HTTP boundary. It does not simulate
all Jellyfin policy or replace the user's real-server acceptance.

## Home/gallery follow-up

The subsequently authorized home screen uses existing APIs, without a server
extension or SDK dependency. `GET UserItems/Resume` accepts `UserId`,
`IncludeItemTypes=Movie,Episode`, `MediaTypes=Video`, `Limit`, `Fields` and user
data options. Recently Added uses ordinary `GET Items` with `Recursive=true`,
`IncludeItemTypes=Movie` or `Series`, and `SortBy=DateCreated`,
`SortOrder=Descending`. The UI requests at most 12 entries per shelf. Individual
movie details use `GET Users/{userId}/Items/{itemId}`.

Primary evidence checked for this follow-up:
* [Jellyfin 10.10.7 ItemsController](https://github.com/jellyfin/jellyfin/blob/v10.10.7/Jellyfin.Api/Controllers/ItemsController.cs)
* [Jellyfin 10.10.7 UserLibraryController](https://github.com/jellyfin/jellyfin/blob/v10.10.7/Jellyfin.Api/Controllers/UserLibraryController.cs)
* [Official resume request contract](https://typescript-sdk.jellyfin.org/interfaces/generated-client.LibraryApiGetResumeItemsRequest.html)
* [Official items request contract](https://typescript-sdk.jellyfin.org/interfaces/generated-client.LibraryApiGetItemsRequest.html)

The fixture tests establish request construction, mapping, failure handling and
presentation; the user's own server remains the end-to-end acceptance source.
Current accepted behavior is maintained in the Jellyfin subsystem README.

## Scrolling follow-up: defaults versus reported inertia

After removing Jellyfin's `StopAtBounds` overrides, the user confirmed overscroll
returned but described touchpad and mouse scrolling as stiff/slow with missing
inertia. Three independent read-only investigations checked Qt defaults,
production desktop practice, and SunPlayer's render-control integration.

Qt 6.10.2 Flickable distinguishes phased synthesized pixel gestures from
ordinary/no-phase wheel input. The private wheel deceleration defaults to 15000;
Qt's 6.6 release notes document that acceleration was disabled by default and
that `QT_QUICK_FLICKABLE_WHEEL_DECELERATION` below 15000 restores older wheel
behavior. This is a possible comparison setting, not a selected product policy.
The public `flickDeceleration` property does not tune this wheel path.

The Qt 6.10.2 Wayland finger-axis path emits Begin/Update/End without generating
ScrollMomentum. Flickable's phased ScrollEnd ends dragging/returns to bounds;
it does not synthesize release inertia there. That is an upstream limitation,
not proof that it caused a regression against the user's earlier build.
ScrollView wraps the same Flickable; changing to it would not create a separate
inertia engine. Bounds behavior is view-wide, with no public device selector
for touchpad-only overscroll.

KDE Kirigami's ScrollablePage uses ScrollView, while its WheelHandler implements
framework-specific animations and release inertia. Reimplementing that policy
would conflict with the user's request to preserve standard Qt behavior.

SunPlayer forwards the original QWheelEvent unchanged, preserving timestamp,
pixel/angle deltas, phase, source and pointing device. The dirty rendering path
uses QQuickRenderControl renderRequested/sceneChanged; inspection found no
confirmed animation-pump defect. Existing ordinary-QQuickWindow UI tests do not
prove physical-input behavior through production render control. A meaningful
next diagnostic is built-in `qt.quick.flickable.wheel`/`.velocity` logging for
the actual device, or comparing identical input against a stock QQuickWindow.
No velocity override, custom wheel handler, continuous timer, device-specific
bounds policy or environment tuning was added during this investigation.

Primary sources:
* [Qt 6.10.2 Flickable implementation](https://github.com/qt/qtdeclarative/blob/v6.10.2/src/quick/items/qquickflickable.cpp)
* [Qt 6.10.2 Wayland input implementation](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandinputdevice.cpp)
* [Qt 6.6 changes](https://doc.qt.io/qt-6.8/whatsnew66.html)
* [Qt 6.10.2 ScrollView implementation](https://github.com/qt/qtdeclarative/blob/v6.10.2/src/quicktemplates/qquickscrollview.cpp)
* [Qt render-control contract](https://doc.qt.io/qt-6/qquickrendercontrol.html)
* [Kirigami ScrollablePage](https://github.com/KDE/kirigami/blob/master/src/controls/ScrollablePage.qml)
* [Kirigami WheelHandler](https://github.com/KDE/kirigami/blob/master/src/wheelhandler.cpp)


### Actual-device comparison and final decision

Built-in Qt wheel/velocity diagnostics preserved phased, synthesized touchpad
pixel updates and ordinary no-phase mouse-wheel input. Animation/velocity
updates continued after input at roughly 16 ms intervals. There was no evidence
for lost event fields or a stalled animation pump. Zero-elapsed-time messages
occurred only during touchpad updates; repeated handler invocations include
nested-view propagation and are not independent lost gestures. They do not
establish the cause of mouse-wheel stiffness.

The trace also included a phased zero-pixel/nonzero-angle touchpad update,
matching [QTBUG-149567](https://bugreports.qt.io/browse/QTBUG-149567). Its
[upstream fix](https://github.com/qt/qtdeclarative/commit/281a769a15ad73fad04058f50c11e85063eeb071)
keeps that update on the pixel path instead of starting mouse-wheel animation
mid-gesture. This is separate from release inertia and the zero-time warning.
Tracker fix-version metadata and tagged source differed during investigation;
any future dependency update should verify the specific fix rather than assume
a particular version includes it.

A temporary standalone native-Wayland QQuickView using the same installed Qt
and an untouched Flickable felt equally stiff to the user. They also noticed
missing inertia in Chrome and explicitly chose to stop investigation and keep
Qt defaults. No Qt patch, dependency change, sensitivity/environment override,
custom scroll handler or new product setting was added. The temporary probe,
source download and diagnostic logs were removed after recording these results.
