# SunPlayer

SunPlayer is a desktop HDR video player for Windows, macOS, and native Wayland
Linux. It uses FFmpeg for demuxing and decoding, libplacebo for video color
processing, and a Qt Quick interface presented through each platform's native
HDR output path.

<a href="https://apps.microsoft.com/detail/9N31J3HSHR0C?mode=direct">
  <img src="https://get.microsoft.com/images/en-us%20dark.svg" width="200" alt="Get SunPlayer from Microsoft Store">
</a>
<a href="https://github.com/usatiuk/sunplayer/releases/latest">
  <img src="https://docs.appimage.org/_images/download-appimage-banner.svg" height="55" alt="Download as an AppImage">
</a>

## Features

### Video

* HDR10 (PQ), HDR10+, Dolby Vision Profile 8.1, HLG, and SDR, tone-mapped by
  libplacebo from the stream's own metadata.
* Native HDR output: Windows HDR, Apple Silicon EDR, and managed HDR10 over
  Wayland's color-management protocol. Compositors without managed color fall
  back to a labeled assumed-sRGB SDR mode.
* Hardware decoding through D3D11VA, VideoToolbox, and VA-API, with decoded
  frames imported directly into the GPU renderer. Streams the hardware path
  cannot handle reopen in software.
* Source reference-white control from 100 to 203 nits for PQ, HDR10+, and
  Dolby Vision sources.
* Optional Absolute PQ output on Windows HDR. Adaptive playback is the default.

### Playback

* Open files or drag and drop them. Windows builds register common video file
  associations.
* Timeline seeking, jump backward/forward, fullscreen, volume and mute,
  keyboard shortcuts.
* Audio and subtitle track selection, or subtitles disabled.
* Subtitles rendered with libass, with configurable appearance.
* Media keys and desktop integration: MPRIS on Linux, SMTC on Windows, Now
  Playing on macOS.
* Loading, buffering, and error states.

### Jellyfin

* Sign in to your server, with optional secure storage for remembered login.
* Browse movies, shows, seasons, and episodes with artwork and descriptions,
  Continue Watching, and Recently Added.
* Stream original files, resume playback, sync watch progress, and mark items
  watched or unwatched. Transcoding is not supported.

### Diagnostics

* In-app view of the active decode, rendering, and display paths, including
  HDR mapping decisions and any active fallbacks.
* HDR Lab test patterns: grayscale, color spectrum, and stepped patches.

## Downloads

### Windows

Install from the [Microsoft Store](https://apps.microsoft.com/detail/9N31J3HSHR0C?mode=direct).

### Linux

Download the x86-64 AppImage from the
[latest release](https://github.com/usatiuk/sunplayer/releases/latest), mark it
executable, and run it. Dependencies are bundled and the AppImage supports
automatic updates. The runtime baseline is Ubuntu 26.04; older distributions
are not supported.

Linux support targets native Wayland. X11 and XWayland are not supported.

### macOS

Apple Silicon builds are not yet distributed. Build from source as described
in [the build subsystem](docs/subsystems/build/README.md).

## Building from source

Platform requirements, validated dependency versions, build commands, and
packaging behavior are documented in
[the build subsystem](docs/subsystems/build/README.md).

## Help and diagnostics

Use **Report a bug…** or **About SunPlayer** from the player's `…` menu. See
[the privacy policy](PRIVACY.md) for how local diagnostics are handled.

## License

SunPlayer is licensed under [GPL-3.0-or-later](LICENSE).
