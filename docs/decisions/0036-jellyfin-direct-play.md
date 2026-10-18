# 0036: Keep Jellyfin outside the shared Direct Play pipeline

* Status: Accepted
* Date: 2026-10-18

## Decision

One concrete Jellyfin client owns authentication/API validation; a Qt model
owns navigation, TokenStore owns native token persistence, and a playback adapter owns
resolution/reporting. The application owns their lifetime outside rebuilt QML
scenes. Use existing Qt Network/JSON/models/images and native FFmpeg HTTP(S).
No server extension, generic provider registry, custom HTTP media reader or
second playback engine is needed.

A source-neutral `MediaInput` retains an internal locator and safe title through
every decoder restart. Initial resume is a normal seek after shared timeline
origin discovery. Jellyfin keeps item/source/session identities outside playback.
Its token-bearing URL is internal; native/public metadata uses `media:remote`,
API/artwork redirects are refused, and FFmpeg logging redacts credentials.
Native media uses URL authentication rather than a reusable custom header that
FFmpeg could forward across origins. Require certificate verification explicitly.

Use one start/final report per playback occurrence, sampling the existing clock
before reset. Serialize older final reports before newer explicit watched
mutations. Authentication loss closes the old occurrence before accepting a
new login. Orderly quit permits three seconds for outstanding reports. Jellyfin
owns watched thresholds and persisted progress; no local user-data database,
periodic reporting timer, durable queue or retry framework.

## Alternatives and limits

Qt has no native cross-platform credential-store API; QSettings is not secure
storage. Use PasswordVault through the existing SDK on Windows and the approved
QtKeychain dependency on Linux/macOS, with insecure fallback disabled. The
Windows Generic Credential backend used by QtKeychain has reported access
restrictions in isolated MSIX; PasswordVault is documented for AppContainer and
desktop use. Keep one shared interface with build-selected implementations;
no runtime fallback or extra credential capability.
Linux uses distro packages; Windows/macOS vcpkg builds against the existing Qt
SDK. AppImage bundles runtime libraries while the host owns its keyring service.

Direct Play preserves original HDR/media and existing embedded track controls.
Use basic GET playback info plus the static original stream. Device-profile
POST negotiation is unnecessary for V1: omitted bitrate fields inherit an
8 Mbps cap and omitted subtitle profiles can demand conversion. Removing that
negotiation avoids imposing server-client capability restrictions on the native
FFmpeg player. The server still authorizes the original stream request.
Remux/transcode negotiation belongs at the resolver boundary if requested later;
it gets no V1 implementation. Quick Connect can establish the existing session
later without an auth hierarchy. Forced termination/network failure can lose
progress. Real-server and native-store acceptance remains required; Linux
fixtures do not establish Windows/macOS evidence. See the
[delivery plan](../plans/cross-cutting/2026-10-18-jellyfin-client.md).
