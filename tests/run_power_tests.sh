#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/mt7922-power.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/tests/test_power_codec.cpp" -o "$test_dir/test_power_codec"
"$test_dir/test_power_codec" "$test_root/fw/WIFI_RAM_CODE_MT7922_1.bin"
