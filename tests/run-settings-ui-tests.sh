#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/settings-test.XXXXXX")
trap 'rm -rf "$build"' EXIT
c++ -std=gnu++11 -fPIC -Wall -Wextra -Wno-missing-field-initializers \
    -I"$root/NickelHook" "$root/tests/settings_ui_test.cc" "$root/src/zip_writer.cc" \
    $(pkg-config --cflags --libs Qt5Widgets) -lz -ldl -o "$build/settings-test"
QT_QPA_PLATFORM=offscreen "$build/settings-test"
