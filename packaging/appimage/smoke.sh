#!/usr/bin/env bash
set -euo pipefail

runtime=$(mktemp -d)
export XDG_RUNTIME_DIR="$runtime"
export WAYLAND_DISPLAY=wayland-sunplayer
export PULSE_SERVER="unix:$runtime/pulse/native"
export PULSE_SINK=sunplayer_test
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
weston_pid=
pulse_pid=
cleanup() {
    status=$?
    trap - EXIT
    if (( status != 0 )); then
        cat "$runtime"/*.log >&2
    fi
    if [[ -n $weston_pid ]]; then kill "$weston_pid"; fi
    if [[ -n $pulse_pid ]]; then kill "$pulse_pid"; fi
}
trap cleanup EXIT

weston --backend=headless --renderer=pixman --no-config \
    --socket="$WAYLAND_DISPLAY" --idle-time=0 >"$runtime/weston.log" 2>&1 &
weston_pid=$!
mkdir "$runtime/pulse"
pulseaudio --daemonize=no --exit-idle-time=-1 -n \
    -L "module-native-protocol-unix socket=$runtime/pulse/native auth-anonymous=1" \
    -L "module-null-sink sink_name=$PULSE_SINK" >"$runtime/pulse.log" 2>&1 &
pulse_pid=$!
for _attempt in {1..100}; do
    kill -0 "$weston_pid"
    kill -0 "$pulse_pid"
    if [[ -S $runtime/$WAYLAND_DISPLAY ]] && pactl info >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
test -S "$runtime/$WAYLAND_DISPLAY"
pactl info >/dev/null

# Extracting tests the same payload without requiring FUSE inside Docker.
cd "$runtime"
"$1" --appimage-extract > /dev/null
# The update helper and its GnuPG engine must work without host command lookup.
PATH=/nonexistent ./squashfs-root/usr/libexec/sunplayer/appimageupdatetool --help
./squashfs-root/usr/libexec/sunplayer/updater/gpg/usr/bin/gpg --version
# PulseAudio pulls some FFmpeg libraries through its ALSA plugin dependency.
# Verify that application libraries actually resolve inside the bundle.
LD_LIBRARY_PATH="$runtime/squashfs-root/usr/lib" \
    ldd ./squashfs-root/usr/bin/sunplayer > "$runtime/dependencies.log"
for library in libQt6Core libQt6Quick libqt6keychain libavcodec libavformat libavutil libswresample libplacebo libcubeb libass; do
    grep -F "=> $runtime/squashfs-root/usr/lib/$library.so" "$runtime/dependencies.log"
done
# QtKeychain loads libsecret at runtime, so ldd on the player cannot prove it.
test -f ./squashfs-root/usr/lib/libsecret-1.so.0
LD_LIBRARY_PATH="$runtime/squashfs-root/usr/lib" \
    ldd ./squashfs-root/usr/lib/libsecret-1.so.0 > "$runtime/secret-dependencies.log"
! grep -F "not found" "$runtime/secret-dependencies.log"
test -f ./squashfs-root/usr/plugins/tls/libqopensslbackend.so
# The system provides icon data; the bundle must provide the Qt engine/theme plugins.
LD_LIBRARY_PATH="$runtime/squashfs-root/usr/lib" \
    QT_PLUGIN_PATH="$runtime/squashfs-root/usr/plugins" \
    XDG_CURRENT_DESKTOP=GNOME /build/desktop-integration-check
./squashfs-root/AppRun --no-log-file --verify-qml
./squashfs-root/AppRun --no-log-file --playback-smoke \
    /fixtures/sdr-bt709-ffv1-audio-late-long-flac.mkv
