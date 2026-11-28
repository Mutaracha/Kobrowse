#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/settings-test.XXXXXX")
trap 'rm -rf "$build"' EXIT
moc "$root/src/native_keyboard.h" -o "$build/moc_native_keyboard.cpp"
moc "$root/src/sleep_lifecycle.h" -o "$build/moc_sleep_lifecycle.cpp"
c++ -std=gnu++11 -fPIC -Wall -Wextra -Wno-missing-field-initializers \
    -I"$root/NickelHook" "$root/tests/settings_ui_test.cc" "$root/src/zip_writer.cc" \
    "$root/src/zip_validator.cc" "$root/src/zip_extract.cc" "$root/src/native_keyboard.cc" "$build/moc_native_keyboard.cpp" "$root/src/sleep_lifecycle.cc" "$build/moc_sleep_lifecycle.cpp" \
    $(pkg-config --cflags --libs Qt5Widgets) -lz -ldl -o "$build/settings-test"
QT_QPA_PLATFORM=offscreen "$build/settings-test"
