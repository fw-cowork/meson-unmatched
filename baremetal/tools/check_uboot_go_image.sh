#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail
export LC_ALL=C

readelf_bin="$1"
nm_bin="$2"
image="$3"
runtime="${4:-return}"

case "$runtime" in
    return|standalone)
        ;;
    *)
        echo "$image: unknown bare-metal runtime: $runtime" >&2
        exit 1
        ;;
esac

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

if [[ "$runtime" == standalone ]]; then
    stack_bottom="$("$nm_bin" -n "$image" | awk '$3 == "__stack_bottom" { print $1 }')"
    stack_top="$("$nm_bin" -n "$image" | awk '$3 == "__stack_top" { print $1 }')"
    if [[ -z "$stack_bottom" || -z "$stack_top" ]]; then
        echo "$image: standalone runtime is missing private stack symbols" >&2
        exit 1
    fi
    printf '%s: entry=%s _start=%s relocations=none runtime=standalone stack=[%s,%s)\n' \
        "$image" "$entry" "$start" "$stack_bottom" "$stack_top"
else
    printf '%s: entry=%s _start=%s relocations=none runtime=return\n' \
        "$image" "$entry" "$start"
fi
