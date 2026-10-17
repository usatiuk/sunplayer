#!/usr/bin/env bash
set -euo pipefail

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
output_dir=${1:-"$source_dir/out/appimage"}
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)

# Container root maps to the invoking user under rootless Docker.
container_user="$(id -u):$(id -g)"
if [[ $(docker info --format '{{json .SecurityOptions}}') == *name=rootless* ]]; then
    container_user=0:0
fi

docker build --platform linux/amd64 --target builder --tag sunplayer-appimage-builder \
    "$source_dir/packaging/appimage"
signing_args=()
if [[ -n ${APPIMAGE_SIGNING_KEY_FILE:-} ]]; then
    : "${APPIMAGE_SIGNING_PASSPHRASE_FILE:?Missing signing passphrase file}"
    : "${APPIMAGE_SIGNING_FINGERPRINT:?Missing signing fingerprint}"
    signing_args=(
        --mount "type=bind,source=$APPIMAGE_SIGNING_KEY_FILE,target=/run/signing-key.asc,readonly"
        --mount "type=bind,source=$APPIMAGE_SIGNING_PASSPHRASE_FILE,target=/run/signing-passphrase,readonly"
        --env APPIMAGE_SIGNING_KEY_FILE=/run/signing-key.asc
        --env APPIMAGE_SIGNING_PASSPHRASE_FILE=/run/signing-passphrase
        --env "APPIMAGE_SIGNING_FINGERPRINT=$APPIMAGE_SIGNING_FINGERPRINT"
    )
fi
docker run --rm --platform linux/amd64 \
    "${signing_args[@]}" \
    --tmpfs /source/.local-secrets:mode=0700 \
    --tmpfs /tmp:mode=1777 \
    --user "$container_user" \
    --mount "type=bind,source=$source_dir,target=/source,readonly" \
    --mount "type=bind,source=$output_dir,target=/output" \
    --mount type=volume,source=sunplayer-appimage-build,target=/work \
    sunplayer-appimage-builder

docker build --platform linux/amd64 --target smoke --tag sunplayer-appimage-smoke \
    "$source_dir/packaging/appimage"
docker run --rm --platform linux/amd64 \
    --mount "type=bind,source=$output_dir,target=/artifacts,readonly" \
    --mount "type=bind,source=$source_dir/tests/fixtures/media,target=/fixtures,readonly" \
    --mount type=volume,source=sunplayer-appimage-build,target=/build,readonly \
    sunplayer-appimage-smoke \
    "/artifacts/SunPlayer-$(cat "$source_dir/VERSION.txt")-ubuntu26.04-x86_64.AppImage"
