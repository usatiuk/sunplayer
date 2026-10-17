#!/usr/bin/env bash
set -euo pipefail

appdir=/work/AppDir
version=$(cat /source/VERSION.txt)
build_id=$(git -C /source rev-parse --short=12 HEAD)

cmake -S /source -B /work/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DSUNPLAYER_DISPLAY_NAME=SunPlayer \
    "-DSUNPLAYER_BUILD_ID=$build_id"
cmake --build /work/build --parallel "$(nproc)"
# Keep the desktop-integration probe outside the shipped AppDir.
read -r -a qt_flags <<< "$(pkg-config --cflags --libs Qt6Widgets Qt6Quick)"
c++ -std=c++20 /source/packaging/appimage/desktop-integration-check.cpp \
    -o /work/desktop-integration-check "${qt_flags[@]}"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install /work/build

install -Dm644 /source/packaging/appimage/sunplayer.desktop \
    "$appdir/usr/share/applications/sunplayer.desktop"
install -Dm644 /source/packaging/windows/Assets/AppList.targetsize-256.png \
    "$appdir/usr/share/icons/hicolor/256x256/apps/sunplayer.png"
install -Dm755 /usr/bin/dbus-send "$appdir/usr/libexec/sunplayer/dbus-send"

export QMAKE=/usr/bin/qmake6
export QML_SOURCES_PATHS=/source/src/app
export EXTRA_PLATFORM_PLUGINS=libqwayland.so
qt_plugins=$(qmake6 -query QT_INSTALL_PLUGINS)
# Deploy runtime-loaded integrations the Qt plugin does not discover itself.
mkdir -p "$appdir/usr/plugins"
for type in wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client iconengines platformthemes; do
    cp -a "$qt_plugins/$type" "$appdir/usr/plugins/"
done

# These libraries load host drivers in-process and must stay with the host stack.
host_libraries=(
    'libvulkan.so*' 'libva.so*' 'libva-*.so*' 'libdrm*.so*' 'libgbm.so*'
    'libwayland-*.so*' 'libEGL*.so*' 'libGL*.so*' 'libOpenGL.so*'
)
exclusions=()
for library in "${host_libraries[@]}"; do
    exclusions+=("--exclude-library=$library")
done
linuxdeploy --appdir "$appdir" --plugin qt \
    --executable "$appdir/usr/libexec/sunplayer/dbus-send" "${exclusions[@]}"
# The nested Qt plugin does not honor all linuxdeploy exclusions (upstream #205).
for library in "${host_libraries[@]}"; do
    find "$appdir/usr/lib" -name "$library" -delete
done

# linuxdeploy's Qt plugin deploys xcb by default; Linux presentation is Wayland-only.
rm -f "$appdir/usr/plugins/platforms/libqxcb.so"
rm -rf "$appdir/usr/plugins/xcbglintegrations"
install -m755 /source/packaging/appimage/AppRun "$appdir/AppRun"
test -f "$appdir/usr/plugins/platforms/libqwayland.so"
test -f "$appdir/usr/plugins/iconengines/libqsvgicon.so"
test -f "$appdir/usr/plugins/platformthemes/libqgtk3.so"
test -f "$appdir/usr/plugins/platformthemes/libqxdgdesktopportal.so"

# Include distro-provided license texts and package versions, including transitive
# dependencies. Build-only package notices are harmless and avoid a second resolver.
mkdir -p "$appdir/usr/share/doc"
cp -a /usr/share/common-licenses "$appdir/usr/share/"
find /usr/share/doc -name copyright -exec cp --parents -L '{}' "$appdir" \;
dpkg-query -W -f='${binary:Package}\t${Version}\n' \
    > "$appdir/usr/share/sunplayer/build-packages.tsv"

export ARCH=x86_64
artifact="SunPlayer-$version-ubuntu26.04-x86_64.AppImage"
appimagetool --runtime-file /opt/tools/runtime-x86_64 "$appdir" "/output/$artifact"
cd /output
sha256sum "$artifact" > "$artifact.sha256"
