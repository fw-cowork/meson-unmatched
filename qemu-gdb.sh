#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GDB_PORT="${UNMATCHED_LITE_QEMU_GDB_PORT:-1234}"
QEMU_ARGS=()

usage() {
    cat <<'EOF'
Usage: ./qemu-gdb.sh [--build] [--timeout SECONDS] [--port PORT]

Start the QEMU boot chain paused at reset with a GDB server listening only on
127.0.0.1. Remaining QEMU options are the same as qemu.sh.

Examples:
  ./qemu-gdb.sh --build
  ./qemu-gdb.sh --port 1235
  gdb-multiarch deploy/qemu/fw_dynamic.elf
  (gdb) target remote 127.0.0.1:1234
  (gdb) continue

Environment:
  UNMATCHED_LITE_QEMU_GDB_PORT  Default GDB port (1234)
EOF
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --port)
            GDB_PORT="${2:?missing GDB port}"
            shift 2
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            QEMU_ARGS+=("$1")
            shift
            ;;
    esac
done

exec "${SCRIPT_DIR}/qemu.sh" --gdb "$GDB_PORT" "${QEMU_ARGS[@]}"
