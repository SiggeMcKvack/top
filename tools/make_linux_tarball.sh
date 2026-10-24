#!/usr/bin/env bash
# Assembles the Linux archive of a release from a build of the launcher, the-oracles-project-X.Y.Z-linux-<arch>.tar.gz
# (<arch> x86_64 or arm64, the build machine's), which runs without installation: a folder with the-oracles-project and
# the libSDL3.so.0 it loads, found beside it (its run path is $ORIGIN), the licences of what it embeds, a README.txt,
# and the icon with a .desktop file a player may install by hand.  X.Y.Z is config/version.json's.
#
#   tools/make_linux_tarball.sh BUILD_DIR [OUT_DIR]
#
# BUILD_DIR is a build configured with -DORACLES_SDL3_FROM_SOURCE=ON (the SDL 3 pinned in config/sdl3.json, built
# with the engine, rather than the system's) and whose target oracles is built.  The run path is set with patchelf
# (apt install patchelf).  The archive is written to OUT_DIR, the current directory by default.  What it holds but the
# icon and the .desktop file, the AppImage holds too (tools/linux_release_files.sh, tools/make_linux_appimage.sh).
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: tools/make_linux_tarball.sh BUILD_DIR [OUT_DIR]" >&2
    exit 2
fi
. "$(dirname "$0")/linux_release_files.sh"
build="$(cd "$1" && pwd)"
mkdir -p "${2:-.}"
out="$(cd "${2:-.}" && pwd)"
linux_release_check "$build"
name="the-oracles-project-${version}-linux-${arch}"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
dir="$stage/$name"
mkdir "$dir"
linux_release_files "$build" "$dir"
cp "$linux_release_root/launcher/icon/the-oracles-project-512.png" "$dir/"
cat > "$dir/the-oracles-project.desktop" <<'DESKTOP'
[Desktop Entry]
# To add the launcher to the applications menu: set Exec and Icon to the full paths of the-oracles-project and
# the-oracles-project-512.png in this folder, then copy this file to ~/.local/share/applications/.
Type=Application
Name=The Oracles Project
Comment=A port of Oracle of Ages and Oracle of Seasons
Exec=the-oracles-project
Icon=the-oracles-project-512
Terminal=false
Categories=Game;
DESKTOP

tar -C "$stage" --owner=0 --group=0 --sort=name -czf "$out/$name.tar.gz" "$name"
echo "$out/$name.tar.gz"
