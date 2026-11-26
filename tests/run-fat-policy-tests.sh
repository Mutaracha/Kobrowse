#!/bin/bash
# A disposable FAT image: never mount or modify an existing device or user data.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/fat-test.XXXXXX")
mounted=0
cleanup() {
    if (( mounted )); then
        if ! sudo -n umount "$build/mount"; then
            echo "Could not unmount test image; retained $build" >&2
            return 1
        fi
    fi
    rm -rf -- "$build"
}
trap cleanup EXIT
c++ -std=c++11 -fPIC -Wall -Wextra "$root/tests/fat_policy_test.cc" \
    $(pkg-config --cflags --libs Qt5Core) -o "$build/fat-policy-test"
truncate -s 64M "$build/fixture.img"
mkfs.vfat "$build/fixture.img" >/dev/null
mkdir "$build/mount"
sudo -n mount -t vfat -o "loop,uid=$(id -u),gid=$(id -g),utf8=1" "$build/fixture.img" "$build/mount"
mounted=1
[[ $(stat -f -c %T "$build/mount") == msdos ]] || { echo "Not a FAT filesystem" >&2; exit 1; }
"$build/fat-policy-test" "$build/mount"
