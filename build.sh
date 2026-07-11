#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${UNMATCHED_LITE_BUILD_DIR:-${SCRIPT_DIR}/builddir}"
CROSS_FILE="${UNMATCHED_LITE_CROSS_FILE:-${SCRIPT_DIR}/cross/riscv64-freedomusdk-linux.ini}"
MESON_BIN="${MESON:-meson}"
NINJA_BIN="${NINJA:-ninja}"

usage() {
    cat <<'EOF'
Usage: ./build.sh [qemu] [target ...]

Configure the Meson Unmatched build if necessary, then run Ninja.
Without arguments, builds the Unmatched physical-board GPT SD image.
Pass qemu to build the separate QEMU OpenSBI/U-Boot/Linux image.

Common targets:
  check, fetch-sources, opensbi-fw, u-boot, linux, busybox, rootfs,
  bootchain, sd-image, qemu-image, clean-lite

Examples:
  ./build.sh          Build deploy/unmatched-lite.img for the FU740 board
  ./build.sh qemu     Build deploy/qemu/qemu-lite.img for QEMU virt

Environment overrides:
  UNMATCHED_LITE_BUILD_DIR   Meson build directory
  UNMATCHED_LITE_CROSS_FILE  Meson cross file
  MESON, NINJA               Tool commands
  JOBS or NINJAJOBS          Parallelism passed to component builds
EOF
}

case "${1:-}" in
    -h|--help|help)
        usage
        exit 0
        ;;
esac

PROFILE=unmatched
if [[ "${1:-}" == qemu ]]; then
    PROFILE=qemu
    shift
fi

command -v "$MESON_BIN" >/dev/null
command -v "$NINJA_BIN" >/dev/null

if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    "$MESON_BIN" setup "$BUILD_DIR" "$SCRIPT_DIR" --cross-file "$CROSS_FILE"
fi

if [[ "$PROFILE" == qemu ]]; then
    if [[ "$#" -eq 0 || "${1:-}" == image || "${1:-}" == qemu-image ]]; then
        set -- qemu-image
    else
        echo "QEMU profile only supports the complete image: ./build.sh qemu" >&2
        exit 2
    fi
elif [[ "$#" -eq 0 ]]; then
    set -- sd-image
fi

exec "$NINJA_BIN" -C "$BUILD_DIR" "$@"
