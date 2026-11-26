#!/bin/bash
# Test package for the existing Qt 5 NickelHook plugin, not a Kobalt release.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}
image=${KOFM_SDK_IMAGE:-ghcr.io/pgaskin/nickeltc:1}
cd "$root"
if [[ ! -f NickelHook/NickelHook.mk ]]; then
    echo 'Initialize the pinned dependency: git submodule update --init --recursive' >&2
    exit 1
fi
"$engine" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$root:/work" -w /work --entrypoint bash "$image" -euc 'make -B all && make package'
python3 scripts/check-package.py kofm_KoboRoot.tgz
