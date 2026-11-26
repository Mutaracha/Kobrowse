#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/zip-test.XXXXXX")
export TMPDIR="$build"
trap 'rm -rf "$build"' EXIT
c++ -std=c++11 -fPIC -Wall -Wextra "$root/tests/zip_driver.cc" "$root/src/zip_writer.cc" \
    $(pkg-config --cflags --libs Qt5Core) -lz -o "$build/zip-driver"
python3 "$root/tests/check_zip.py" "$build/zip-driver" -v
