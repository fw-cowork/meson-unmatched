#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_ROOT="${SCRIPT_DIR}/toolchains/sifive"
SOURCES_DIR="${TOOLCHAIN_ROOT}/sources"
SDK_SOURCE="${SOURCES_DIR}/freedom-u-sdk"
SDK_INSTALL="${TOOLCHAIN_ROOT}/sdk"
BIN_DIR="${TOOLCHAIN_ROOT}/bin"
SYSROOT_LINK="${TOOLCHAIN_ROOT}/sysroot"
SDK_REPOSITORY="https://github.com/sifiveinc/freedom-u-sdk.git"
SDK_TAG="2026.01.00"

usage() {
    cat <<'EOF'
Usage: ./toolchain.sh <setup|status>

setup   Download SiFive Freedom-U SDK 2026.01.00, build its Linux SDK, and
        install a normalized RISC-V toolchain under toolchains/sifive/.
status  Report whether that toolchain is ready for Meson builds.

The initial SDK build downloads Yocto sources and requires substantial local
storage. SiFive recommends at least 140 GB of free disk space and 32 GB of RAM
for Freedom-U-SDK image builds.

EOF
}

fail() {
    echo "error: $*" >&2
    exit 1
}

toolchain_ready() {
    [[ -x "${BIN_DIR}/riscv64-sifive-linux-gcc" && -d "${SYSROOT_LINK}" ]]
}

normalize_toolchain() {
    local target_sysroot compiler upstream_prefix tool suffix

    target_sysroot="$(find "${SDK_INSTALL}/sysroots" -mindepth 1 -maxdepth 1 -type d \
        -name 'riscv64*-linux*' -print -quit)"
    [[ -n "${target_sysroot}" ]] || fail "could not locate the RISC-V target sysroot in ${SDK_INSTALL}"

    compiler="$(find "${SDK_INSTALL}/sysroots" \( -type f -o -type l \) \
        -name 'riscv64*-linux-gcc' -print -quit)"
    [[ -n "${compiler}" ]] || fail "could not locate the SiFive RISC-V GCC in ${SDK_INSTALL}"

    upstream_prefix="$(basename "${compiler}")"
    upstream_prefix="${upstream_prefix%gcc}"

    mkdir -p "${BIN_DIR}"
    while IFS= read -r tool; do
        [[ -x "${tool}" ]] || continue
        suffix="${tool##*/${upstream_prefix}}"
        ln -sfn "${tool}" "${BIN_DIR}/riscv64-sifive-linux-${suffix}"
    done < <(find "${SDK_INSTALL}/sysroots" \( -type f -o -type l \) \
        -path "*/usr/bin/${upstream_prefix}*" -print)
    ln -sfn "${target_sysroot}" "${SYSROOT_LINK}"

    [[ -x "${BIN_DIR}/riscv64-sifive-linux-gcc" ]] || fail "failed to normalize the SiFive GCC"
}

setup() {
    [[ "$(uname -s)" == Linux ]] || fail "SiFive's generated SDK is supported here only on Linux hosts"
    [[ "$(uname -m)" == x86_64 ]] || fail "SiFive's generated SDK is supported here only on x86_64 hosts"
    command -v git >/dev/null || fail "git is required"
    command -v kas >/dev/null || fail "kas is required; install it before running ./toolchain.sh setup"

    if toolchain_ready; then
        echo "SiFive toolchain is already ready: ${TOOLCHAIN_ROOT}"
        return
    fi
    [[ ! -e "${SDK_INSTALL}" ]] || fail "incomplete SDK installation at ${SDK_INSTALL}; remove it and retry"

    mkdir -p "${SOURCES_DIR}"
    if [[ ! -d "${SDK_SOURCE}/.git" ]]; then
        git clone --depth 1 --branch "${SDK_TAG}" "${SDK_REPOSITORY}" "${SDK_SOURCE}"
    fi
    git -C "${SDK_SOURCE}" fetch --depth 1 origin "refs/tags/${SDK_TAG}:refs/tags/${SDK_TAG}"
    git -C "${SDK_SOURCE}" checkout --detach "${SDK_TAG}"

    (
        cd "${SOURCES_DIR}"
        kas shell --update freedom-u-sdk/scripts/kas/unmatched.yml \
            -c "bitbake demo-coreip-cli -c populate_sdk"
    )

    local installer
    installer="$(find "${SOURCES_DIR}/build/tmp/deploy/sdk" -maxdepth 1 -type f \
        -name '*riscv64*toolchain-*.sh' -print -quit)"
    [[ -n "${installer}" ]] || fail "Freedom-U-SDK did not produce a RISC-V SDK installer"
    "${installer}" -y -d "${SDK_INSTALL}"
    normalize_toolchain
    echo "SiFive toolchain is ready: ${TOOLCHAIN_ROOT}"
}

status() {
    if toolchain_ready; then
        echo "SiFive toolchain: ${TOOLCHAIN_ROOT}"
        echo "compiler: ${BIN_DIR}/riscv64-sifive-linux-gcc"
        echo "sysroot:  ${SYSROOT_LINK}"
        return
    fi
    echo "SiFive toolchain is not installed. Run: ./toolchain.sh setup" >&2
    return 1
}

case "${1:-}" in
    setup) setup ;;
    status) status ;;
    -h|--help|help|'') usage ;;
    *) usage >&2; exit 2 ;;
esac
