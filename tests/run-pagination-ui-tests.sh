#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/pagination-test.XXXXXX")
trap 'rm -rf "$build"' EXIT
c++ -std=c++11 -fPIC -Wall -Wextra "$root/tests/pagination_ui_test.cc" \
    $(pkg-config --cflags --libs Qt5Widgets) -o "$build/pagination-test"
QT_QPA_PLATFORM=offscreen "$build/pagination-test"
