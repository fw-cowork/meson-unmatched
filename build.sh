#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${UNMATCHED_LITE_BUILD_DIR:-${SCRIPT_DIR}/builddir}"
DEFAULT_CROSS_FILE="${SCRIPT_DIR}/cross/sifive-freedom-u-sdk.ini"
CROSS_FILE="${UNMATCHED_LITE_CROSS_FILE:-${DEFAULT_CROSS_FILE}}"
MESON_BIN="${MESON:-meson}"
NINJA_BIN="${NINJA:-ninja}"

usage() {
    cat <<'EOF'
Usage: ./build.sh [qemu] [target ...]

Configure the Meson Unmatched build if necessary, then run Ninja.
Without arguments, builds the Unmatched physical-board GPT SD image.
Pass qemu to build the separate QEMU OpenSBI/U-Boot/Linux image.

Common targets:
  check, fetch-sources, opensbi-fw, u-boot, linux, fit, firmware-fit,
  dev-linux, dev-uboot,
  busybox, rootfs, bootchain, sd-image, qemu-image, clean-lite

Dev targets (preserve source edits + .config for iteration):
  dev-linux    Incremental Linux build keeping src/linux/ modifications
  dev-uboot    Incremental U-Boot build keeping src/u-boot/ modifications

Examples:
  ./build.sh toolchain  Download and install the pinned SiFive Linux SDK
  ./build.sh          Build deploy/unmatched-lite.img for the FU740 board
  ./build.sh qemu     Build deploy/qemu/qemu-lite.img for QEMU virt

Environment overrides:
  UNMATCHED_LITE_BUILD_DIR   Meson build directory
  UNMATCHED_LITE_CROSS_FILE  Explicit alternate Meson cross file
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

if [[ "${1:-}" == toolchain ]]; then
    exec "${SCRIPT_DIR}/toolchain.sh" setup
fi

if [[ "${CROSS_FILE}" == "${DEFAULT_CROSS_FILE}" && ! -x "${SCRIPT_DIR}/toolchains/sifive/bin/riscv64-sifive-linux-gcc" ]]; then
    echo "SiFive Freedom-U SDK is not installed. Run: ./build.sh toolchain" >&2
    echo "Use UNMATCHED_LITE_CROSS_FILE only to select an explicit alternate toolchain." >&2
    exit 1
fi

PROFILE=unmatched
if [[ "${1:-}" == qemu ]]; then
    PROFILE=qemu
    shift
fi

command -v "$MESON_BIN" >/dev/null
command -v "$NINJA_BIN" >/dev/null

cross_stamp="${BUILD_DIR}/.unmatched-cross-file"
if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    "$MESON_BIN" setup "$BUILD_DIR" "$SCRIPT_DIR" --cross-file "$CROSS_FILE"
elif [[ ! -f "${cross_stamp}" || "$(<"${cross_stamp}")" != "${CROSS_FILE}" ]]; then
    "$MESON_BIN" setup --wipe "$BUILD_DIR" "$SCRIPT_DIR" --cross-file "$CROSS_FILE"
fi
printf '%s\n' "$CROSS_FILE" > "$cross_stamp"

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
