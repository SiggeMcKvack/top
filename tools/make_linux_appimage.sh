#!/usr/bin/env bash
# Assembles the Linux AppImage of a release from a build of the launcher, the-oracles-project-X.Y.Z-linux-<arch>.AppImage:
# one file that runs where it is, made executable, without installation nor extraction, on a system with FUSE (a
# desktop's fusermount); elsewhere, it runs with --appimage-extract-and-run, or the Linux archive does
# (tools/make_linux_tarball.sh).  X.Y.Z is config/version.json's, <arch> x86_64 or arm64, the build machine's.
#
#   tools/make_linux_appimage.sh BUILD_DIR [OUT_DIR]
#
#   AppDir/
#     the-oracles-project, libSDL3.so.0, the licences, README.txt   as in the Linux archive (tools/linux_release_files.sh)
#     AppImage-runtime-LICENSE.txt, libfuse-, squashfuse-, zstd-, zlib-, musl- and mimalloc-LICENSE.txt
#                                                                   the licences of the runtime and of what it links
#                                                                   statically, launcher/linux/licences/
#     AppRun -> the-oracles-project                                 what the AppImage runs, with its arguments
#     org.theoraclesproject.oracles.desktop                         launcher/linux's desktop entry, its version added,
#                                                                   also in usr/share/applications/
#     the-oracles-project.png, .DirIcon                             -> the 512 px icon in usr/share/icons/hicolor/
#     usr/share/icons/hicolor/{256x256,512x512}/apps/the-oracles-project.png   launcher/icon's PNGs
#     usr/share/metainfo/org.theoraclesproject.oracles.appdata.xml  launcher/linux's AppStream metadata
#
# BUILD_DIR is a build configured with -DORACLES_SDL3_FROM_SOURCE=ON (the SDL 3 pinned in config/sdl3.json, built
# with the engine, rather than the system's) and whose target oracles is built.  The run path is set with patchelf
# (apt install patchelf).  appimagetool, and the runtime it puts at the head of the AppImage, are the releases pinned
# in config/appimage.json for the build machine (uname -m, x86_64 or aarch64): downloaded once into
# BUILD_DIR/appimage-tools, their SHA-256 checked at every run, and appimagetool, an AppImage itself, run extracted,
# which needs no FUSE.  It validates the desktop entry, and the AppStream metadata with appstreamcli when the machine
# has it (apt install appstream).  The AppImage carries no update information.  The runtime's licences are copies of
# the upstream texts at the versions the pinned runtime was built with, the same for both machines, whose origin and
# SHA-256 config/appimage.json records and which are checked against it.  It is written to OUT_DIR, the current
# directory by default.
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: tools/make_linux_appimage.sh BUILD_DIR [OUT_DIR]" >&2
    exit 2
fi
. "$(dirname "$0")/linux_release_files.sh"
root="$linux_release_root"
build="$(cd "$1" && pwd)"
mkdir -p "${2:-.}"
out="$(cd "${2:-.}" && pwd)"
linux_release_check "$build"
machine="$(uname -m)"
name="the-oracles-project-${version}-linux-${arch}"

# The pinned file of config/appimage.json's $1 for this machine, downloaded into BUILD_DIR/appimage-tools if missing:
# its path, once its SHA-256 matches.
pinned() {
    local url sha file
    read -r url sha < <(python3 -c 'import json, sys; e = json.load(open(sys.argv[1]))[sys.argv[2]].get(sys.argv[3])
print(*(e and (e["url"], e["sha256"]) or ("", "")))' "$root/config/appimage.json" "$1" "$machine")
    if [ -z "$url" ]; then
        echo "config/appimage.json pins no $1 for $machine" >&2
        exit 1
    fi
    file="$build/appimage-tools/$(basename "$url")"
    if [ ! -f "$file" ]; then
        mkdir -p "$build/appimage-tools"
        curl -fsSL --retry 3 -o "$file.part" "$url" >&2
        mv "$file.part" "$file"
    fi
    if ! echo "$sha  $file" | sha256sum -c --quiet - >&2; then
        echo "$file is not the $1 config/appimage.json pins (SHA-256 $sha): remove it to download it again" >&2
        exit 1
    fi
    echo "$file"
}
tool="$(pinned appimagetool)"
runtime="$(pinned runtime)"
chmod +x "$tool"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
dir="$stage/AppDir"
mkdir -p "$dir/usr/share/applications" "$dir/usr/share/metainfo"
linux_release_files "$build" "$dir"
python3 - "$root/config/appimage.json" "$root/launcher/linux/licences" "$dir" <<'LICENCES'
import hashlib, json, pathlib, shutil, sys
config, source, target = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
for licence in json.load(open(config))["runtime"]["licences"]:
    path = source / licence["file"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != licence["sha256"]:
        sys.exit(f"{path} is not the text config/appimage.json pins for {licence['of']} ({licence['url']})")
    shutil.copyfile(path, target / licence["file"])
LICENCES
ln -s the-oracles-project "$dir/AppRun"
# appimagetool writes the version into the desktop entry at the root, which the one in usr/share/applications/ is.
cp "$root/launcher/linux/org.theoraclesproject.oracles.desktop" "$dir/"
ln -s ../../../org.theoraclesproject.oracles.desktop "$dir/usr/share/applications/org.theoraclesproject.oracles.desktop"
cp "$root/launcher/linux/org.theoraclesproject.oracles.appdata.xml" "$dir/usr/share/metainfo/"
for size in 256 512; do
    mkdir -p "$dir/usr/share/icons/hicolor/${size}x${size}/apps"
    cp "$root/launcher/icon/the-oracles-project-${size}.png" "$dir/usr/share/icons/hicolor/${size}x${size}/apps/the-oracles-project.png"
done
ln -s usr/share/icons/hicolor/512x512/apps/the-oracles-project.png "$dir/the-oracles-project.png"
ln -s the-oracles-project.png "$dir/.DirIcon"

# appimagetool's own runtime extracts it into TMPDIR, here the staging folder; ARCH is uname's name, aarch64 on ARM64.
TMPDIR="$stage" APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$machine" VERSION="$version" \
    "$tool" --runtime-file "$runtime" "$dir" "$out/$name.AppImage" >&2
echo "$out/$name.AppImage"
