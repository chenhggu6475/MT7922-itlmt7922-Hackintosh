#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$test_root/build/tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/tests/security_test.cpp" "$test_root/codec/mt7922_data_codec.cpp" \
    "$test_root/codec/mt7922_tx_codec.cpp" -o "$test_root/build/tests/security_test"
"$test_root/build/tests/security_test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/tests/session_test.cpp" -o "$test_root/build/tests/session_test"
"$test_root/build/tests/session_test"
python3 "$test_root/tests/test_teardown.py"
