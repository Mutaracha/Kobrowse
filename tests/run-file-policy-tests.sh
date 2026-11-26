#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build"
build=$(mktemp -d "$root/build/file-policy.XXXXXX")
export TMPDIR="$build"
trap 'rm -rf "$build"' EXIT
c++ -std=c++11 -fPIC -Wall -Wextra "$root/tests/file_policy_test.cc" \
    $(pkg-config --cflags --libs Qt5Core) -o "$build/file-policy-test"
"$build/file-policy-test"
