#!/bin/sh
set -eu
umask 022

version="$1"
architecture="$(dpkg --print-architecture)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
package="$stage/package"
mkdir -p "$package/DEBIAN"
make PREFIX=/usr DESTDIR="$package" install
strip --strip-unneeded "$package/usr/bin/jatayu-calculator"
cat > "$package/DEBIAN/control" <<CONTROL
Package: jatayu-calculator
Version: $version-2
Architecture: $architecture
Maintainer: Anup Kumar Yadav
Section: utils
Priority: optional
Depends: libc6 (>= 2.38), libgcc-s1, libstdc++6 (>= 12), libqt6core6t64 (>= 6.8), libqt6gui6 (>= 6.8), libqt6widgets6 (>= 6.8)
Recommends: fonts-noto-core, qt6-wayland
Homepage: https://jatayuos.in
Description: Native desktop calculator for JatayuOS
 Standard and scientific calculator with unit conversion, local calculation
 history, memory controls, and English and Hindi interface options.
CONTROL
(
    cd "$package"
    find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
dpkg-deb --root-owner-group --build "$package" "build/jatayu-calculator_${version}-2_${architecture}.deb"
