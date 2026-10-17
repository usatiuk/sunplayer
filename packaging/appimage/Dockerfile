FROM ubuntu:26.04@sha256:c098726dd9b9733f3e885f8de0ef375206a311b0a317eadc40d347b1115fb944 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install --yes --no-install-recommends \
    build-essential cmake ninja-build pkg-config git ca-certificates curl \
    desktop-file-utils file dbus-bin \
    qt6-base-dev qt6-base-private-dev \
    qt6-declarative-dev qt6-declarative-private-dev qt6-declarative-dev-tools \
    qt6-wayland qt6-wayland-dev qt6-wayland-private-dev \
    qt6-shadertools-dev qt6-shader-baker qt6-svg-plugins spirv-tools \
    qt6-gtk-platformtheme qt6-xdgdesktopportal-platformtheme \
    qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-dialogs qml6-module-qtquick-layouts \
    libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
    libplacebo-dev libcubeb-dev libass-dev libvulkan-dev \
    libwayland-dev libwayland-bin wayland-protocols libva-dev libdrm-dev \
    libpulse0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/tools
COPY tools.sha256 .
# Upstream continuous tags move. Hashes deliberately fail closed on replacement.
RUN curl -fL --retry 3 -O https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    && curl -fL --retry 3 -O https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
    && curl -fL --retry 3 -O https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
    && curl -fL --retry 3 -O https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 \
    && sha256sum --check tools.sha256 \
    && chmod +x *.AppImage \
    && for tool in linuxdeploy linuxdeploy-plugin-qt appimagetool; do \
        mkdir "$tool"; \
        (cd "$tool" && "../$tool-x86_64.AppImage" --appimage-extract >/dev/null); \
        ln -s "/opt/tools/$tool/squashfs-root/AppRun" "/usr/local/bin/$tool"; \
    done

COPY package.sh /usr/local/bin/package-sunplayer
RUN install -d -m1777 /work
WORKDIR /work
ENTRYPOINT ["/usr/local/bin/package-sunplayer"]

# Test the package without the builder's Qt/QML installation.
FROM ubuntu:26.04@sha256:c098726dd9b9733f3e885f8de0ef375206a311b0a317eadc40d347b1115fb944 AS smoke
RUN apt-get update && apt-get install --yes --no-install-recommends \
    weston pulseaudio pulseaudio-utils mesa-vulkan-drivers \
    libvulkan1 libva2 libva-drm2 libva-x11-2 libdrm2 libgbm1 \
    libwayland-client0 libwayland-cursor0 libwayland-egl1 \
    libegl1 libgl1 libopengl0 fonts-dejavu-core adwaita-icon-theme \
    && rm -rf /var/lib/apt/lists/*
COPY smoke.sh /usr/local/bin/smoke-sunplayer
USER ubuntu
ENTRYPOINT ["/usr/local/bin/smoke-sunplayer"]
