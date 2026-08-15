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
TOOLCHAIN_PREFIX="riscv64-freedomusdk-linux-"
EXPECTED_GCC_VERSION="15.2.0"

usage() {
    cat <<'EOF'
Usage: ./toolchain.sh <setup|install SDK_INSTALLER|status>

setup   Download SiFive Freedom-U SDK 2026.01.00, build its Linux SDK, and
        install the RISC-V toolchain under toolchains/sifive/.
install Install a previously generated Freedom-U-SDK populate_sdk installer.
status  Report whether that toolchain is ready for Meson builds.

The initial SDK build downloads Yocto sources and requires substantial local
storage. SiFive recommends at least 140 GB of free disk space and 32 GB of RAM
for Freedom-U-SDK image builds.
The install command reuses an existing SDK installer and does not run Yocto.

EOF
}

fail() {
    echo "error: $*" >&2
    exit 1
}

toolchain_ready() {
    local compiler libgcc

    compiler="${BIN_DIR}/${TOOLCHAIN_PREFIX}gcc"
    [[ -x "${compiler}" && -d "${SYSROOT_LINK}" ]] || return 1
    libgcc="$("${compiler}" -print-libgcc-file-name 2>/dev/null)" || return 1
    [[ "${libgcc}" = /* && -f "${libgcc}" ]]
}

normalize_toolchain() {
    local target_sysroot compiler tool gcc_version libgcc

    target_sysroot="$(find "${SDK_INSTALL}/sysroots" -mindepth 1 -maxdepth 1 \
        \( -type d -o -type l \) \
        -name 'riscv64*-linux*' -print -quit)"
    [[ -n "${target_sysroot}" ]] || fail "could not locate the RISC-V target sysroot in ${SDK_INSTALL}"

    compiler="$(find "${SDK_INSTALL}/sysroots" \( -type f -o -type l \) \
        -name "${TOOLCHAIN_PREFIX}gcc" -print -quit)"
    [[ -n "${compiler}" ]] || fail "could not locate ${TOOLCHAIN_PREFIX}gcc in ${SDK_INSTALL}"

    mkdir -p "${BIN_DIR}"
    while IFS= read -r tool; do
        [[ -x "${tool}" ]] || continue
        ln -sfn "${tool}" "${BIN_DIR}/$(basename "${tool}")"
    done < <(find "${SDK_INSTALL}/sysroots" \( -type f -o -type l \) \
        -name "${TOOLCHAIN_PREFIX}*" -print)
    [[ -x "${BIN_DIR}/${TOOLCHAIN_PREFIX}as" ]] || fail "failed to locate the SiFive assembler"
    [[ -x "${BIN_DIR}/${TOOLCHAIN_PREFIX}ld" ]] || fail "failed to locate the SiFive linker"
    ln -sfn "${TOOLCHAIN_PREFIX}as" "${BIN_DIR}/as"
    ln -sfn "${TOOLCHAIN_PREFIX}ld" "${BIN_DIR}/ld"
    ln -sfn "${target_sysroot}" "${SYSROOT_LINK}"

    [[ -x "${BIN_DIR}/${TOOLCHAIN_PREFIX}gcc" ]] || fail "failed to install the SiFive GCC"
    gcc_version="$("${BIN_DIR}/${TOOLCHAIN_PREFIX}gcc" -dumpfullversion)"
    [[ "${gcc_version}" == "${EXPECTED_GCC_VERSION}" ]] ||
        fail "expected GCC ${EXPECTED_GCC_VERSION}, found ${gcc_version}"
    libgcc="$("${BIN_DIR}/${TOOLCHAIN_PREFIX}gcc" -print-libgcc-file-name)"
    [[ "${libgcc}" = /* && -f "${libgcc}" ]] ||
        fail "Freedom-U-SDK compiler cannot locate libgcc.a; reinstall the complete populate_sdk output"
}

check_host() {
    [[ "$(uname -s)" == Linux ]] || fail "SiFive's generated SDK is supported here only on Linux hosts"
    [[ "$(uname -m)" == x86_64 ]] || fail "SiFive's generated SDK is supported here only on x86_64 hosts"
}

install_sdk() {
    local installer="$1"

    check_host
    [[ -f "${installer}" ]] || fail "SDK installer not found: ${installer}"
    if toolchain_ready || [[ -d "${SDK_INSTALL}/sysroots" ]]; then
        normalize_toolchain
        echo "SiFive toolchain is already ready: ${TOOLCHAIN_ROOT}"
        return
    fi
    [[ ! -e "${SDK_INSTALL}" ]] || fail "incomplete SDK installation at ${SDK_INSTALL}; remove it and retry"

    /bin/sh "${installer}" -y -d "${SDK_INSTALL}"
    normalize_toolchain
    echo "SiFive toolchain is ready: ${TOOLCHAIN_ROOT}"
}

setup() {
    check_host

    if toolchain_ready || [[ -d "${SDK_INSTALL}/sysroots" ]]; then
        normalize_toolchain
        echo "SiFive toolchain is already ready: ${TOOLCHAIN_ROOT}"
        return
    fi
    [[ ! -e "${SDK_INSTALL}" ]] || fail "incomplete SDK installation at ${SDK_INSTALL}; remove it and retry"
    command -v git >/dev/null || fail "git is required"
    command -v kas >/dev/null || fail "kas is required; install it before running ./toolchain.sh setup"

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
    install_sdk "${installer}"
    echo "Reusable SDK installer: ${installer}"
    echo "Installer SHA256: $(sha256sum "${installer}" | awk '{print $1}')"
}

status() {
    if toolchain_ready; then
        echo "SiFive toolchain: ${TOOLCHAIN_ROOT}"
        echo "compiler: ${BIN_DIR}/${TOOLCHAIN_PREFIX}gcc"
        echo "sysroot:  ${SYSROOT_LINK}"
        return
    fi
    if [[ -e "${SDK_INSTALL}" || -e "${BIN_DIR}/${TOOLCHAIN_PREFIX}gcc" ]]; then
        echo "SiFive toolchain installation is incomplete: ${TOOLCHAIN_ROOT}" >&2
        echo "A complete populate_sdk installation, including libgcc.a, is required." >&2
        return 1
    fi
    echo "SiFive toolchain is not installed." >&2
    echo "Run ./toolchain.sh setup, or ./toolchain.sh install /path/to/sdk.sh." >&2
    return 1
}

case "${1:-}" in
    setup) setup ;;
    install)
        [[ "$#" -eq 2 ]] || fail "install requires exactly one SDK installer path"
        install_sdk "$2"
        ;;
    status) status ;;
    -h|--help|help|'') usage ;;
    *) usage >&2; exit 2 ;;
esac
