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
Usage: ./build.sh [test|qemu] [target ...]

Configure the Meson Unmatched build if necessary, then run Ninja.
Without arguments, builds the Unmatched physical-board GPT SD image.
Pass qemu to build the separate QEMU OpenSBI/U-Boot/Linux image.
Pass test to run host tests and cross-build artifacts with Freedom-U-SDK.

Common targets:
  check, fetch-sources, opensbi-fw, u-boot, u-boot-mmode, u-boot-lwip, u-boot-lwip-port,
  linux, fit, firmware-fit,
  dev-linux, dev-uboot,
  baremetal, baremetal-test, unmatched-led-bin, unmatched-led-artifacts,
  unmatched-tests-bin, unmatched-tests-artifacts,
  unmatched-mmode-check-bin, unmatched-mmode-check-artifacts,
  unmatched-standalone-bin, unmatched-standalone-artifacts,
  busybox, rootfs, bootchain, sd-image, qemu-image, clean-lite

Dev targets (preserve source edits + .config for iteration):
  dev-linux    Incremental Linux build keeping src/linux/ modifications
  dev-uboot    Incremental U-Boot build keeping src/u-boot/ modifications

Examples:
  ./build.sh toolchain  Build and install the pinned SiFive Linux SDK
  ./build.sh toolchain /path/to/sdk.sh  Install a shared SDK installer
  ./build.sh test       Test with riscv64-freedomusdk-linux tools
  ./build.sh u-boot-mmode  Build SPL + M-mode U-Boot without OpenSBI
  ./build.sh u-boot-lwip  Build the isolated Unmatched lwIP variant
  ./build.sh u-boot-lwip-port  Rebuild U-Boot with the standalone latest-lwIP port
  ./build.sh baremetal  Build every U-Boot go bare-metal program
  ./build.sh baremetal-test  Run host-side bare-metal algorithm tests
  ./build.sh unmatched-tests-artifacts  Build board-side test artifacts
  ./build.sh unmatched-mmode-check-artifacts  Build the M-mode CSR probe
  ./build.sh unmatched-standalone-artifacts  Build the private-stack non-returning payload
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
    shift
    if [[ "$#" -eq 0 ]]; then
        exec "${SCRIPT_DIR}/toolchain.sh" setup
    elif [[ "$#" -eq 1 ]]; then
        exec "${SCRIPT_DIR}/toolchain.sh" install "$1"
    fi
    echo "toolchain accepts at most one SDK installer path" >&2
    exit 2
fi

if [[ "${1:-}" == test ]]; then
    shift
    if [[ "$#" -ne 0 ]]; then
        echo "test does not accept additional targets" >&2
        exit 2
    fi
    BUILD_DIR="${UNMATCHED_LITE_BUILD_DIR:-${SCRIPT_DIR}/builddir-test}"
    CROSS_FILE="${UNMATCHED_LITE_CROSS_FILE:-${DEFAULT_CROSS_FILE}}"
    set -- baremetal-test unmatched-tests-artifacts unmatched-standalone-artifacts
fi

PROFILE=unmatched
if [[ "${1:-}" == qemu ]]; then
    PROFILE=qemu
    shift
fi

command -v "$MESON_BIN" >/dev/null
command -v "$NINJA_BIN" >/dev/null

if [[ "${CROSS_FILE}" == "${DEFAULT_CROSS_FILE}" ]] &&
   ! "${SCRIPT_DIR}/toolchain.sh" status >/dev/null 2>&1; then
    echo "Freedom-U-SDK toolchain is missing or incomplete." >&2
    echo "Run ./build.sh toolchain, or install a shared SDK with ./build.sh toolchain /path/to/sdk.sh." >&2
    exit 1
fi

cross_stamp="${BUILD_DIR}/.unmatched-cross-file"
cross_digest="$(sha256sum "${CROSS_FILE}" | awk '{print $1}')"
cross_identity="${CROSS_FILE}:${cross_digest}"
if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    "$MESON_BIN" setup "$BUILD_DIR" "$SCRIPT_DIR" --cross-file "$CROSS_FILE"
elif [[ ! -f "${cross_stamp}" || "$(<"${cross_stamp}")" != "${cross_identity}" ]]; then
    "$MESON_BIN" setup --wipe "$BUILD_DIR" "$SCRIPT_DIR" --cross-file "$CROSS_FILE"
fi
printf '%s\n' "$cross_identity" > "$cross_stamp"

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
