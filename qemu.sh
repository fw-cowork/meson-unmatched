#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_DIR="${UNMATCHED_LITE_QEMU_DEPLOY:-${SCRIPT_DIR}/deploy/qemu}"
IMAGE="${UNMATCHED_LITE_QEMU_IMAGE:-${DEPLOY_DIR}/qemu-lite.img}"
FIRMWARE="${UNMATCHED_LITE_QEMU_FIRMWARE:-${DEPLOY_DIR}/fw_dynamic.elf}"
UBOOT="${UNMATCHED_LITE_QEMU_UBOOT:-${DEPLOY_DIR}/u-boot.bin}"
QEMU_BIN="${QEMU_BIN:-qemu-system-riscv64}"
MEMORY=2G
CPUS=8
TIMEOUT="${UNMATCHED_LITE_QEMU_TIMEOUT:-0}"
SNAPSHOT_TMPDIR="${UNMATCHED_LITE_QEMU_TMPDIR:-/tmp}"
BUILD=0
GDB_PORT=""

usage() {
    cat <<'EOF'
Usage: ./qemu.sh [--build] [--timeout SECONDS] [--gdb PORT]

Start the QEMU profile:
  OpenSBI FW_DYNAMIC -> QEMU S-mode U-Boot -> boot.scr -> FIT -> Linux -> BusyBox

The QEMU profile has separate output under deploy/qemu/. It uses QEMU's virt
machine and does not emulate the FU740 Boot ROM or the Unmatched SPL. The FIT
contains the kernel, QEMU virt DTB, and BusyBox CPIO rootfs, so it requires the
fixed virt configuration of eight CPUs and 2 GiB memory used here.

Options:
  --build              Run ./build.sh qemu before starting QEMU
  --timeout SECONDS    Stop QEMU after this many seconds
  --gdb PORT           Pause at reset and listen for GDB on 127.0.0.1:PORT
  -h, --help           Show this message

Environment overrides:
  QEMU_BIN
  UNMATCHED_LITE_QEMU_DEPLOY
  UNMATCHED_LITE_QEMU_IMAGE
  UNMATCHED_LITE_QEMU_FIRMWARE
  UNMATCHED_LITE_QEMU_UBOOT
  UNMATCHED_LITE_QEMU_TIMEOUT
  UNMATCHED_LITE_QEMU_TMPDIR
EOF
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --build)
            BUILD=1
            shift
            ;;
        --timeout)
            TIMEOUT="${2:?missing timeout value}"
            shift 2
            ;;
        --gdb)
            GDB_PORT="${2:?missing GDB port}"
            shift 2
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -n "$GDB_PORT" ]]; then
    [[ "$GDB_PORT" =~ ^[1-9][0-9]{0,4}$ ]] && (( GDB_PORT <= 65535 )) || {
        echo "invalid GDB port: $GDB_PORT" >&2
        exit 2
    }
fi

if [[ "$BUILD" -eq 1 ]]; then
    QEMU_SYSTEM_RISCV64="$QEMU_BIN" "${SCRIPT_DIR}/build.sh" qemu
fi

command -v "$QEMU_BIN" >/dev/null || {
    echo "missing qemu-system-riscv64: ${QEMU_BIN}" >&2
    exit 1
}

for path in "$FIRMWARE" "$UBOOT" "$IMAGE"; do
    [[ -f "$path" ]] || {
        echo "missing: $path" >&2
        echo "run: ${SCRIPT_DIR}/build.sh qemu" >&2
        exit 1
    }
done

[[ -d "$SNAPSHOT_TMPDIR" && -w "$SNAPSHOT_TMPDIR" ]] || {
    echo "QEMU snapshot directory is not writable: $SNAPSHOT_TMPDIR" >&2
    exit 1
}
export TMPDIR="$SNAPSHOT_TMPDIR"

qemu_args=(
    "$QEMU_BIN"
    -M virt
    -smp "$CPUS"
    -m "$MEMORY"
    -nographic
    -snapshot
    -bios "$FIRMWARE"
    -kernel "$UBOOT"
    -drive "id=rootdisk,file=${IMAGE},if=none,format=raw"
    -device virtio-blk-device,drive=rootdisk
)

if [[ -n "$GDB_PORT" ]]; then
    qemu_args+=(
        -S
        -gdb "tcp:127.0.0.1:${GDB_PORT}"
    )
fi

if [[ "$TIMEOUT" != 0 ]]; then
    exec timeout "$TIMEOUT" "${qemu_args[@]}"
fi

exec "${qemu_args[@]}"
