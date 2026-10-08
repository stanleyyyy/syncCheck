#!/bin/sh
# Builds a portable Sync-Analyzer-<arch>.AppImage that runs on most Linux distributions.
# Build on the oldest distribution you want to support (glibc is not bundled).
# Usage: packaging/appimage/build-appimage.sh [build-dir]
set -e

SRC_DIR=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR=$(realpath -m "${1:-$SRC_DIR/build-appimage}")
APPDIR="$BUILD_DIR/AppDir"
ARCH=$(uname -m)
LINUXDEPLOY="$BUILD_DIR/linuxdeploy-$ARCH.AppImage"

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DSYNCCHECK_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --component applications

if [ ! -x "$LINUXDEPLOY" ]; then
	curl -fL -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
	chmod +x "$LINUXDEPLOY"
fi

cd "$BUILD_DIR"
APPIMAGE_EXTRACT_AND_RUN=1 OUTPUT="Sync-Analyzer-$ARCH.AppImage" "$LINUXDEPLOY" \
	--appdir "$APPDIR" \
	--desktop-file "$APPDIR/usr/share/applications/sync-analyzer.desktop" \
	--icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/sync-analyzer.svg" \
	--output appimage
