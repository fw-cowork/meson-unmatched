#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail
export LC_ALL=C

readelf_bin="$1"
nm_bin="$2"
image="$3"

entry="$("$readelf_bin" -h "$image" | awk '/Entry point address:/ { print $4 }')"
if "$readelf_bin" -r "$image" | grep -E 'R_RISCV_|\.rela?\.' >/dev/null; then
	echo "$image: runtime relocations are not supported by U-Boot go" >&2
	exit 1
fi

start="$("$nm_bin" -n "$image" | awk '$3 == "_start" { print $1 }')"
entry="$(printf '%016x' "$entry")"
if [[ "$start" != "$entry" ]]; then
	echo "$image: entry point $entry does not match _start at $start" >&2
	exit 1
fi

printf '%s: entry=%s _start=%s relocations=none\n' "$image" "$entry" "$start"
