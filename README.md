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

Fetch sources:

```bash
ninja -C builddir fetch-sources
```

BusyBox is fetched from its upstream release tarball when it is not already in
`downloads/`. Set `UNMATCHED_LITE_BUSYBOX_TARBALL` to use a
pre-downloaded tarball.

Build OpenSBI:

```bash
ninja -C builddir opensbi-fw
```

Build U-Boot. This also builds OpenSBI first if needed:

```bash
ninja -C builddir u-boot
```

Build Linux:

```bash
ninja -C builddir linux
```

Build the BusyBox rootfs:

```bash
ninja -C builddir rootfs
```

Build the whole boot chain:

```bash
ninja -C builddir bootchain
```

Build the complete GPT SD image:

```bash
ninja -C builddir sd-image
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
UNMATCHED_LITE_KEEP_CONFIG=1 ninja -C builddir linux
```

## Clean

Remove generated build output and deploy artifacts while keeping source clones:

```bash
ninja -C builddir clean-lite
```

To remove sources too:

```bash
rm -rf src
```

The BusyBox download cache is disposable as well:

```bash
rm -rf downloads
```
