# 0037: Use package-local Windows settings

Status: accepted

Installed AppSilo reports QSettings registry AccessError even for the first
boolean write. Ordinary settings persist with the same unpackaged binaries;
PasswordVault writes succeed installed. The precise registry denial remains
unidentified; a historical Microsoft AppSilo bug is not proof of its cause.

Keep QSettings and store packaged Windows settings.ini in the SDK-provided
ApplicationData.Current.LocalFolder. Resolve this only in the Windows
ApplicationSettings constructor, using package identity rather than assuming
all Windows execution is packaged. Unpackaged Windows and Linux/macOS retain
their existing native storage. Unexpected identity/path failures are fatal;
ordinary QSettings I/O failures retain existing bounded diagnostics.

This reuses serialization and all preference consumers instead of introducing
a Windows LocalSettings adapter. No permission, dependency, registry fallback,
dual write, or migration is added. Tokens stay in PasswordVault; the INI stores
ordinary preferences and Jellyfin metadata only. Temporary test/smoke identity
is explicit and independent of format, preserving normal production login
restoration. Installed restart persistence remains the acceptance gate.

See the [delivery plan](../plans/cross-cutting/2026-10-18-windows-settings-localfolder.md)
for evidence, sources, and review.
