# Meson Lite Build for HiFive Unmatched

This directory is a lightweight build wrapper for studying the Unmatched boot
chain without rebuilding Yocto recipes during normal firmware/kernel iteration.

The Meson framework builds:

```text
OpenSBI FW_DYNAMIC -> U-Boot SPL/proper -> Linux Image.gz + DTB
                                             + BusyBox static rootfs -> raw SD image
```

The rootfs is built from BusyBox 1.37.0 and a small tracked overlay. It does
not use a Yocto WIC image or a rootfs copied from the Freedom-U-SDK.

The maintained entry point is Meson/Ninja:

```bash
./toolchain.sh setup
./build.sh
```

Convenience wrappers provide the same build and QEMU rootfs smoke test:

```bash
cd meson-unmatched
./build.sh toolchain
./build.sh
./build.sh qemu
./qemu.sh
```

`build.sh` without arguments builds the FU740 board image. `build.sh qemu`
creates a separate QEMU profile with OpenSBI, QEMU S-mode U-Boot, generic
RISC-V Linux, BusyBox, and a FIT image under `deploy/qemu/`. The FIT embeds
the kernel, QEMU virt DTB, and gzip CPIO rootfs; the virtio disk only carries
that FIT and its U-Boot boot script.
`qemu.sh --build` builds and starts this profile. It does not emulate the
FU740 ROM/SPL path.

For reset-time debugging, start the paused GDB server and attach another
terminal with `gdb-multiarch`:

```bash
./qemu-gdb.sh --build
gdb-multiarch deploy/qemu/fw_dynamic.elf
(gdb) target remote 127.0.0.1:1234
(gdb) continue
```

## Versions

The pinned revisions match SiFive Freedom-U-SDK 2026.01 recipes:

```text
OpenSBI v1.8.1  74434f255873d74e56cc50aa762d1caf24c099f8
U-Boot 2026.01  127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3
Linux 6.18.3    a607c8f744340ad2c2486d46e96b66df47caffba
```

The wrapper applies the same local Unmatched patches used by the SDK for
U-Boot and Linux.

The Linux defconfig and both patches are stored in this repository. The default
SiFive SDK is generated locally under `toolchains/sifive/`; no sibling SDK
checkout is used.

## Dependencies

The project downloads its own source dependencies. On Debian or Ubuntu, install
the required host tools once:

```bash
sudo apt-get update
sudo apt-get install -y \
  bc bison build-essential chrpath cpio debianutils diffstat e2fsprogs file \
  flex gawk gdisk git iputils-ping libacl1 locales m4 make meson ninja-build \
  openssl python3 python3-git python3-jinja2 python3-pexpect python3-pip \
  python3-subunit qemu-system-misc socat texinfo unzip wget xz-utils zstd
python3 -m pip install --user kas
export PATH="$HOME/.local/bin:$PATH"
```

For GDB debugging, install the optional host debugger:

```bash
sudo apt-get install -y gdb-multiarch
```

`./build.sh toolchain` performs the SiFive toolchain bootstrap. It clones the
pinned `sifiveinc/freedom-u-sdk` `2026.01.00` source release into
`toolchains/sifive/sources/`, uses KAS to download its Yocto layers and build
the `populate_sdk` target, then installs the generated Linux SDK under
`toolchains/sifive/sdk/`. KAS is needed only for this initialization step; the
normal Meson/QEMU build does not use a parent KAS checkout.

The initial SDK build is large. SiFive recommends at least 140 GB of free disk
space and 32 GB of RAM for Freedom-U-SDK image builds.

After the toolchain is ready, `./build.sh` downloads the pinned OpenSBI,
U-Boot, and Linux git revisions into `src/`; it downloads BusyBox 1.37.0 into
`downloads/`. To reuse pre-downloaded content, set
`UNMATCHED_LITE_GIT_CACHE`, `UNMATCHED_LITE_BUSYBOX_TARBALL`, or
`UNMATCHED_LITE_DOWNLOAD_DIR` before building.

## Setup

The default is a Linux SDK generated from SiFive's official
`freedom-u-sdk` `2026.01.00` release. Initialize it once:

```bash
./build.sh toolchain
./build.sh check
```

Toolchain selection is owned by the repository's Meson cross files:

```text
cross/sifive-freedom-u-sdk.ini
```

`toolchain.sh setup` downloads SiFive's pinned SDK source, uses its
`populate_sdk` target, and installs the result under `toolchains/sifive/`.
This requires `kas`, a Linux x86_64 host, and substantial initial resources.
SiFive recommends at least 140 GB of free disk space and 32 GB of RAM for
Freedom-U-SDK image builds.
It does not use a distro `riscv64-linux-gnu-*` compiler or a sibling KAS tree.

An alternate toolchain is supported only as an explicit override with
`UNMATCHED_LITE_CROSS_FILE`.

When using the Yocto-built cross compiler outside BitBake, the wrapper creates
a private `out/toolchain-shim/` directory so GCC can find the matching RISC-V
binutils without modifying the system or the Yocto tree.

To force a toolchain:

```bash
UNMATCHED_LITE_CROSS_FILE=/path/to/toolchain.ini ./build.sh qemu
```

## Build

### Unmatched (FU740 physical board)

Fetch sources:

```bash
./build.sh fetch-sources
```

BusyBox is fetched from its upstream release tarball when it is not already in
`downloads/`. Set `UNMATCHED_LITE_BUSYBOX_TARBALL` to use a
pre-downloaded tarball.

Build individual components:

```bash
./build.sh opensbi-fw      # OpenSBI firmware
./build.sh u-boot           # U-Boot SPL + proper (also builds OpenSBI)
./build.sh linux            # Linux kernel
./build.sh rootfs           # BusyBox rootfs
```

For iterative driver work, dev targets preserve local source edits:

```bash
./build.sh dev-linux         # Incremental Linux build; edits in src/linux/ survive
./build.sh dev-uboot         # Incremental U-Boot build; edits in src/u-boot/ survive
```

**WARNING:** A plain `./build.sh` (or `./build.sh linux` / `./build.sh u-boot`)
runs `git reset --hard` + `git clean`, discarding uncommitted work. Export
experiments with `git -C src/linux diff` (or `src/u-boot`) before returning
to reproducible mode.

Build the whole boot chain:

```bash
./build.sh bootchain
```

Build the complete GPT SD image (the default when no target is given):

```bash
./build.sh                  # same as: ./build.sh sd-image
```

### Linux PCIe development

Use the development target while editing Linux PCIe drivers or the host
controller. It preserves `src/linux/` and `out/linux/.config` between builds:

```bash
./build.sh dev-linux
# edit src/linux/
./build.sh dev-linux
```

Do not run `./build.sh`, `./build.sh linux`, or `./build.sh qemu` while keeping
experimental source changes: those reproducible targets reset their source
checkout. Export a completed change by limiting the diff to files you modified:

```bash
git -C src/linux diff -- drivers/pci/ > patches/linux/0002-pcie-learning.patch
```

### QEMU (virt machine)

The QEMU profile builds a separate artifact tree under `deploy/qemu/`. It uses
OpenSBI, QEMU S-mode U-Boot, generic RISC-V Linux, and a FIT image. The profile
only supports the complete image — individual component targets are not available:

```bash
./build.sh qemu
```

## Outputs

Artifacts are copied to:

```text
deploy/
```

Expected files:

```text
fw_dynamic.bin
fw_dynamic.elf
fw_jump.bin
fw_jump.elf
fw_payload.bin
fw_payload.elf
u-boot-spl.bin
u-boot.itb
Image.gz
hifive-unmatched-a00.dtb
busybox
rootfs.ext4
unmatched-lite.img
```

The QEMU profile writes its separate artifacts under `deploy/qemu/`:

```text
fw_dynamic.elf
u-boot.bin
Image.gz
rootfs.cpio.gz
fit.itb
boot.scr
qemu-lite.img
```

## Source and Build Trees

Generated paths:

```text
src/         cloned source trees, including BusyBox
out/         per-project build directories
deploy/      copied artifacts
toolchains/  SiFive SDK source, build tree, and installation
```

These are ignored by git.

By default the Linux build resets `out/linux/.config` from
`configs/linux/unmatched_defconfig`.
For config experiments, edit `out/linux/.config` and run:

```bash
UNMATCHED_LITE_KEEP_CONFIG=1 ./build.sh linux
```

## Clean

Remove generated build output and deploy artifacts while keeping source clones:

```bash
./build.sh clean-lite
```

To remove sources too:

```bash
rm -rf src
```

The BusyBox download cache is disposable as well:

```bash
rm -rf downloads
```
