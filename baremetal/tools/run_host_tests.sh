#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

for test_binary in "$@"; do
	echo "RUN $(basename "$test_binary")"
	"$test_binary"
done
