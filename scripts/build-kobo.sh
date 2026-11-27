#!/bin/bash
# Build and verify the Qt 5 NickelHook test package.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}
# The digest was resolved from :1 in CI run 45996277964; update only after verification.
image=${KOBROWSE_SDK_IMAGE:-ghcr.io/pgaskin/nickeltc@sha256:3296eb400d270cfd26f5773ab09625d4469f85680b3a191951dfb78d8ade7840}
cd "$root"
if [[ ! -f NickelHook/NickelHook.mk ]]; then
    echo 'Initialize the pinned dependency: git submodule update --init --recursive' >&2
    exit 1
fi
# These are generated files only. Remove previous outputs so a failed/incomplete
# build cannot accidentally publish a package from an earlier invocation.
mkdir -p build
rm -f -- libkobrowse.so build/KoboRoot.tgz build/build-info.json
revision=$(git rev-parse --short=12 HEAD)
if [[ -n $(git status --porcelain=v1 --untracked-files=normal) ]] || [[ -n $(git -C NickelHook status --porcelain=v1 --untracked-files=normal) ]]; then revision+=-dirty; fi
number=${GITHUB_RUN_NUMBER:-local}
[[ $number == local || $number =~ ^[0-9]+$ ]] || { echo 'Invalid build number' >&2; exit 1; }
export KOBROWSE_BUILD_NUMBER=$number KOBROWSE_BUILD_COMMIT=$revision
"$engine" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e KOBROWSE_BUILD_NUMBER -e KOBROWSE_BUILD_COMMIT \
    -v "$root:/work" -w /work --entrypoint bash "$image" -euc 'make -B all && make package'
python3 scripts/check-package.py build/KoboRoot.tgz libkobrowse.so
sdk_inspection=$("$engine" image inspect "$image")
python3 scripts/write-build-info.py "$image" "$sdk_inspection"
