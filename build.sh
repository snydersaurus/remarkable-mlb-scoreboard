#!/usr/bin/env bash
# Cross-compile the scoreboard backend for reMarkable from macOS.
#
#   ./build.sh ~/Downloads/remarkable-ferrari-image-<ver>-sdk.sh
#
# Grab the matching SDK for your device's OS version from
# https://developer.remarkable.com/documentation/sdk
set -euo pipefail

SDK_SH="${1:?usage: ./build.sh /path/to/remarkable-ferrari-...-sdk.sh}"
# The SDK has to MATCH THE DEVICE'S OS, not merely be newer. The tablet runs
# 5.7.x, whose Qt is 6.8.2; an SDK carrying Qt 6.10 builds a binary the device
# cannot load at all -- "version `Qt_6.10' not found" -- and through AppLoad
# that shows up as an app that simply never appears.
# Check with:  ssh root@<host> 'ls -l /lib/libQt6Core.so.6'
IMAGE="${IMAGE:-rmpp-sdk-5.7}"
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "==> staging SDK installer"
# Passing docker/rmpp-sdk.sh itself is the obvious thing to do once it is
# staged, and cp then fails with "are identical" -- which under set -e aborts
# the build after printing a line that looks like progress.
if [ "$(cd "$(dirname "$SDK_SH")" && pwd)/$(basename "$SDK_SH")" != "$HERE/docker/rmpp-sdk.sh" ]; then
    cp "$SDK_SH" "$HERE/docker/rmpp-sdk.sh"
else
    echo "    already staged"
fi

echo "==> building toolchain image (slow the first time, cached after)"
docker build --platform linux/amd64 -t "$IMAGE" "$HERE/docker"

echo "==> cross-compiling"
docker run --rm --platform linux/amd64 -v "$HERE":/src "$IMAGE" bash -lc '
  set -euo pipefail
  # shellcheck disable=SC1090
  source /opt/rmpp-sdk/environment-setup-*-remarkable-linux

  # Older SDKs export OE_CMAKE_TOOLCHAIN_FILE; 5.8.203 does not, but ships the
  # same file at a predictable path in the native sysroot.
  TOOLCHAIN="${OE_CMAKE_TOOLCHAIN_FILE:-${OECORE_NATIVE_SYSROOT}/usr/share/cmake/OEToolchainConfig.cmake}"
  [ -f "$TOOLCHAIN" ] || { echo "no cmake toolchain file at $TOOLCHAIN"; exit 1; }
  echo "==> toolchain: $TOOLCHAIN"

  cmake -S /src -B /src/build-rmpp -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
      -DQT_HOST_PATH="${OECORE_NATIVE_SYSROOT}/usr"

  cmake --build /src/build-rmpp
'

echo
echo "==> built: build-rmpp/scoreboard_backend"
file "$HERE/build-rmpp/scoreboard_backend" || true
