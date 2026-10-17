#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/mt7922-protocol.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
for kind in wpa caps ba; do
    "${CXX:-c++}" -std=c++17 -Wall -Wextra \
        -fsanitize=address,undefined -fno-omit-frame-pointer -g \
        -I"$test_root/src" "$test_root/tests/${kind}_test.cpp" \
        "$test_root/src/itl_${kind}.cpp" -o "$test_dir/${kind}_test"
    "$test_dir/${kind}_test"
done
