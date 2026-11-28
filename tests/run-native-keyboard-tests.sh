#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/native-keyboard-test.XXXXXX")
trap 'rm -rf "$build"' EXIT
moc "$root/src/native_keyboard.h" -o "$build/moc_native_keyboard.cpp"
moc "$root/tests/native_keyboard_test.cc" -o "$build/native_keyboard_test.moc"
c++ -std=gnu++11 -fPIC -Wall -Wextra -I"$build" \
    "$root/tests/native_keyboard_test.cc" "$root/src/native_keyboard.cc" "$build/moc_native_keyboard.cpp" \
    $(pkg-config --cflags --libs Qt5Widgets) -ldl -o "$build/native-keyboard-test"
QT_QPA_PLATFORM=offscreen "$build/native-keyboard-test"
