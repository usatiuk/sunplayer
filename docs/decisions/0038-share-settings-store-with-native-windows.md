# 0038: Share one settings store with native packaged Windows persistence

Status: accepted; supersedes [0037](0037-use-package-local-windows-settings.md)

Installed AppSilo registry writes failed; the attempted LocalFolder INI store
also failed. A supplied Process Monitor trace proves its temp file and lock
create/write/flush succeed, then atomic rename is denied. Do not patch Qt,
disable integrity, add permissions or invent a rename fallback.

ApplicationSettings owns one SettingsStore; Jellyfin metadata and AppImage
consent borrow it. The interface exposes only current key/value/remove/sync,
sticky status and backend diagnostic operations. Consumers own keys, validation,
defaults and credential policy. There is no second path/format reconstruction.

Packaged Windows uses SDK ApplicationData.LocalSettings.Values, with typed
scalar boxing, immediate native writes and safe operation/HRESULT diagnostics.
Native object lifetime is enclosed by its runtime owner. QSettings retains
native Linux/macOS/unpackaged Windows behavior and temporary INI tests. Remove
the unsuccessful LocalFolder constructor and storage-path/format consumer API.

This is a real platform boundary, avoiding Qt's demonstrated failing commit
operation while reusing Qt where it works. No cache, schema, backend registry,
migration, dual write or runtime failure fallback is needed. PasswordVault and
QtKeychain remain the separate token stores. Test/smoke intent is explicit and
cannot accidentally restore production credentials.

Hosted CI compiles the Windows backend; unpackaged tests cannot establish
installed LocalSettings access. Preference restart, Remember me across two
restarts, and logout/restart remain installed acceptance. See the
[delivery plan](../plans/cross-cutting/2026-10-18-settings-store.md).
