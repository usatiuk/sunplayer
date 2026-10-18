# SunPlayer privacy policy

SunPlayer does not collect telemetry, create an account, or send playback,
diagnostic, or usage data to the SunPlayer project.

## Access to your files

SunPlayer accesses user files that you choose to open, such as videos, to
provide media playback. Their contents are processed on your device.

SunPlayer does not upload your files or their contents, share them with the
SunPlayer project or third parties, or use them for advertising or profiling.
Playing a file does not modify or delete it.

## Local preferences and diagnostics

The application stores preferences locally and may write bounded session logs
to the local temporary-data directory. Those logs can contain technical
playback and graphics details. SunPlayer does not upload them.

## Optional Jellyfin connection

When you connect to a Jellyfin server, SunPlayer sends authentication requests,
library queries, artwork requests, media-stream requests, playback start/final
position reports, and explicit watched/unwatched changes to that server. Your
server operator controls its data handling. These requests do not go to the
SunPlayer project.

Remembered login stores the access token in the operating system's credential
store; passwords are not persisted. Server/account metadata is stored with local
preferences. If secure storage is unavailable, login can remain session-only.
There is no background progress synchronization or telemetry. A crash or failed
connection can prevent the final playback position from reaching your server.
