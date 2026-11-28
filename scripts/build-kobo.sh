#!/bin/bash
# Build and verify Kobrowse with the pinned kobuild Qt 5/GCC 11 SDK.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}
image=${KOBROWSE_SDK_IMAGE:-ghcr.io/nicoverbruggen/kobuild@sha256:c5c2c0e4f1ebd5268ea631c89354976ef090fc571e162c294644677c5aedad05}
cd "$root"
if [[ ! -f NickelHook/NickelHook.mk ]]; then
    echo 'Initialize the pinned dependency: git submodule update --init --recursive' >&2
    exit 1
fi
mkdir -p build
rm -f -- libkobrowse.so build/KoboRoot.tgz build/build-info.json
work=$(mktemp -d "$root/build/kobuild-work.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
cp -a Makefile src NickelHook res "$work/"
revision=$(git rev-parse --short=12 HEAD)
if [[ -n $(git status --porcelain=v1 --untracked-files=normal) ]] || [[ -n $(git -C NickelHook status --porcelain=v1 --untracked-files=normal) ]]; then revision+=-dirty; fi
number=${GITHUB_RUN_NUMBER:-local}
[[ $number == local || $number =~ ^[0-9]+$ ]] || { echo 'Invalid build number' >&2; exit 1; }
export KOBROWSE_BUILD_NUMBER=$number KOBROWSE_BUILD_COMMIT=$revision
"$engine" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e KOBROWSE_BUILD_NUMBER -e KOBROWSE_BUILD_COMMIT \
    -e "PKG_CONFIG_LIBDIR=/tc/qt5/sysroot/usr/lib/pkgconfig:/tc/qt5/sysroot/usr/share/pkgconfig" \
    -v "$work:/work" -w /work --entrypoint bash "$image" -euc \
    'make -B all CROSS_COMPILE=/tc/qt5/bin/ PKG_CONFIG=pkg-config MOC=/tc/qt5/sysroot/usr/bin/moc RCC=/tc/qt5/sysroot/usr/bin/rcc VERSION="$KOBROWSE_BUILD_COMMIT" && make package'
python3 scripts/check-package.py "$work/build/KoboRoot.tgz" "$work/libkobrowse.so"
sdk_inspection=$("$engine" image inspect "$image")
python3 scripts/write-build-info.py "$image" "$sdk_inspection" "$work/build/KoboRoot.tgz" "$work/libkobrowse.so" "$work/build/build-info.json"
cp -- "$work/build/KoboRoot.tgz" build/KoboRoot.tgz
cp -- "$work/libkobrowse.so" libkobrowse.so
cp -- "$work/build/build-info.json" build/build-info.json
