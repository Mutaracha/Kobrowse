#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/sleep-test.XXXXXX")
trap 'rm -rf "$build"' EXIT
moc "$root/src/sleep_lifecycle.h" -o "$build/moc_sleep_lifecycle.cpp"
moc "$root/tests/sleep_lifecycle_test.cc" -o "$build/sleep_lifecycle_test.moc"
c++ -std=gnu++11 -fPIC -Wall -Wextra -I"$build" \
    "$root/tests/sleep_lifecycle_test.cc" "$root/src/sleep_lifecycle.cc" "$build/moc_sleep_lifecycle.cpp" \
    $(pkg-config --cflags --libs Qt5Widgets) -ldl -o "$build/sleep-test"
QT_QPA_PLATFORM=offscreen "$build/sleep-test"
