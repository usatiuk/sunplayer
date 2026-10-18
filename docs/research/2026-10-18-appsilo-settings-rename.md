# AppSilo settings persistence: denied atomic rename

## Installed evidence

The user tested commit `457c921` (LocalFolder QSettings INI). Session logs show
`backend=package_localfolder`, INI format 1, and AccessError 1 during save;
PasswordVault write succeeds. LocalFolder selection alone did not fix installed
persistence. Prior standalone testing demonstrated registry persistence outside
the package, not INI persistence inside it.

The user supplied a Process Monitor PML trace on 2026-10-18. Read-only analysis
used temporary Python tools procmon-parser 0.4.0 and procmon-reader 0.1.1,
without a project dependency or Windows execution on this Linux host. Two
parsers independently agree on the four denied rename events. The second
parser also decodes their destination and ReplaceIfExists flag. Do not commit
the full trace: it contains unrelated machine activity and personal paths.

SunPlayer PID 11352 produces 21,818 captured events. Of its 251 events involving
settings.ini or LocalState, the only ACCESS DENIED results are these four:

| Event index (zero based) | UTC time | Operation | Result |
| --- | --- | --- | --- |
| 807279 | 17:40:51.705764 | SetRenameInformationFile | 0xC0000022 ACCESS DENIED |
| 822852 | 17:40:52.187704 | SetRenameInformationFile | 0xC0000022 ACCESS DENIED |
| 842061 | 17:40:52.513626 | SetRenameInformationFile | 0xC0000022 ACCESS DENIED |
| 842650 | 17:40:52.540668 | SetRenameInformationFile | 0xC0000022 ACCESS DENIED |

Each renames a settings.ini.<random> temporary file to the same package
LocalState/settings.ini with ReplaceIfExists=True. Target queries return NAME
NOT FOUND before each attempt. The first attempt demonstrates:

- LocalState exists and attribute queries succeed.
- Writable-directory temporary file creation and deletion succeed.
- settings.ini.lock creation, writes, and flush succeed.
- Save temporary file creation with Generic Read/Write and Delete succeeds.
- Settings payload write and FlushBuffersFile succeed.
- Rename to settings.ini is denied.
- Temporary-file deletion and lock cleanup succeed.

Thus missing directory creation, inability to create/write files, inability to
create the lock, and token storage are not the observed failure in this trace.
The denial is at atomic rename/commit. The underlying Windows policy/filter
that rejects it is not yet established; do not claim all AppSilo renames or all
LocalFolder writes are unsupported.

## Pinned Qt path

Qt 6.11.1 QSettings syncConfFile uses QSaveFile, then commit. QSaveFile opens a
Win32NonShared temporary engine; its commit calls renameOverwrite, which routes
to QFSFileEnginePrivate::nativeRenameOverwrite and SetFileInformationByHandle
with FileRenameInfo and an absolute destination. The trace's rename stack
passes through Qt6Core.dll, KernelBase.dll and ntdll.dll.

QSettings already calls QDir::mkpath for a missing parent when checking a new
INI file's writability. Adding another mkdir is not a fix for this trace.

setAtomicSyncRequired(false) sets QSaveFile's direct-write-fallback flag; it
does not force direct writing. In this pinned Windows implementation, an
ordinary file still opens a temporary file successfully and commit still tries
the same rename; there is no commit-time direct-write fallback. Do not apply
this as an unverified workaround or weaken integrity guarantees globally.

## Sources and remaining work

- [Qt directory creation](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qsettings.cpp)
- [Qt settings save/commit](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qsettings.cpp)
- [QSaveFile open](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qsavefile.cpp)
- [QSaveFile commit](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qsavefile.cpp)
- [Windows native rename](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qfsfileengine_win.cpp)

Investigate a documented supported persistence path or a minimal isolated
rename reproduction before selecting a further implementation. No Qt patch,
new capability, custom rename fallback, or storage replacement is justified
solely by the generic QSettings status. Current source behavior is documented;
installed acceptance failed and remains an open blocker.


The user selected SDK ApplicationData.LocalSettings behind one shared storage
interface rather than a custom rename workaround. Implementation is tracked
in the [settings-store plan](../plans/cross-cutting/2026-10-18-settings-store.md).
