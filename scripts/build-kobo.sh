#!/bin/bash
# Build and verify the Qt 5 NickelHook test package.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}
image=${KOBROWSE_SDK_IMAGE:-ghcr.io/pgaskin/nickeltc:1}
cd "$root"
if [[ ! -f NickelHook/NickelHook.mk ]]; then
    echo 'Initialize the pinned dependency: git submodule update --init --recursive' >&2
    exit 1
fi
"$engine" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$root:/work" -w /work --entrypoint bash "$image" -euc 'make -B all && make package'
python3 scripts/check-package.py kobrowse_KoboRoot.tgz
