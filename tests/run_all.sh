#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause-Clear
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$test_root/tests/run_codec_tests.sh"
sh "$test_root/tests/run_power_tests.sh"
sh "$test_root/tests/run_protocol_tests.sh"
python3 "$test_root/tests/test_release_check.py"
