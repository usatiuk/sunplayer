# Native TLS in the pinned FFmpeg port

Snapshot of the official vcpkg FFmpeg port at baseline
`9d7f79f56ae1a9b4704d6a7fb8237e347a974133`.

The sole build change adds `--enable-securetransport` to the macOS flags.
The official port disables autodetection and otherwise enables native TLS only
on Windows (Schannel). This uses Apple system frameworks, not OpenSSL.

Linux does not consume this port; it uses system FFmpeg. Native Windows/macOS
builds and runtime acceptance must be performed on those hosts, not on Linux.
