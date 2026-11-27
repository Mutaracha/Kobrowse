#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/brightness-test.XXXXXX")
trap 'rm -rf "$build"' EXIT
c++ -std=gnu++11 -fPIC -Wall -Wextra "$root/tests/brightness_test.cc" \
    $(pkg-config --cflags --libs Qt5Widgets) -ldl -o "$build/brightness-test"
QT_QPA_PLATFORM=offscreen "$build/brightness-test"
