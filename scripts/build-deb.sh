#!/usr/bin/env bash

# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2024-2025 <Nitrux Latinoamericana S.C. <hello@nxos.org>>


# -- Exit on errors.

set -e


# -- Prepare source.

SRC_DIR="$(pwd)"

BUILD_WORK_DIR="$(mktemp -d)"

cp -r "$SRC_DIR"/* "$BUILD_WORK_DIR/"

cd "$BUILD_WORK_DIR"


# -- Configure build.

MESON_ARGS=(--prefix=/usr --buildtype=release)

if [ "$(uname -m)" = "x86_64" ]; then
    MESON_ARGS+=("-Dcpp_args=-march=x86-64-v3")
fi

meson setup .build "${MESON_ARGS[@]}"


# -- Compile source.

ninja -C .build -k 0 -j "$(nproc)"


# -- Create a temporary DESTDIR for packaging.

DESTDIR="$(pwd)/pkg"
rm -rf "$DESTDIR"


# -- Install binary to DESTDIR.

DESTDIR="$DESTDIR" ninja -C .build install


# -- Install configuration file.

install -Dm644 "qmlgreet.conf" "$DESTDIR/etc/qmlgreet/qmlgreet.conf"


# -- Install a default color scheme.

install -Dm644 "QMLGreetDefault.colors" "$DESTDIR/usr/share/color-schemes/QMLGreetDefault.colors"


# -- Create DEBIAN control file.

mkdir -p "$DESTDIR/DEBIAN"

PKGNAME="greetd-qmlgreet"
VERSION="${PACKAGE_VERSION:-0.0.1}"
MAINTAINER="uri_herrera@nxos.org"
ARCHITECTURE="$(dpkg --print-architecture)"
DESCRIPTION="QML-based greeter for greetd and wlr-based compositors."

cat > "$DESTDIR/DEBIAN/control" <<EOF
Package: $PKGNAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCHITECTURE
Maintainer: $MAINTAINER
Description: $DESCRIPTION
Depends: greetd, libqt6core5compat6, libqt6core6t64, libqt6dbus6, libqt6gui6, libqt6opengl6, libqt6openglwidgets6, libqt6qml6, libqt6waylandclient6, libwayland-client0, libwayland-cursor0, libwayland-egl1, libwayland-server0, mauikit, qml6-module-qt5compat-graphicaleffects, qt6-wayland, wayland-protocols, wayland-scanner++
EOF


echo "/etc/qmlgreet/qmlgreet.conf" > "$DESTDIR/DEBIAN/conffiles"

cd "$(dirname "$DESTDIR")"

dpkg-deb --build "$(basename "$DESTDIR")" "${PKGNAME}_${VERSION}_${ARCHITECTURE}.deb"


# -- Move .deb to ./build/ for CI consistency.

TARGET_DIR="${GITHUB_WORKSPACE:-$SRC_DIR}/build"

mkdir -p "$TARGET_DIR"

mv "${PKGNAME}_${VERSION}_${ARCHITECTURE}.deb" "$TARGET_DIR/"

echo "Debian package created: $(pwd)/build/${PKGNAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"
