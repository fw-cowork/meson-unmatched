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
meson setup meson-unmatched/builddir meson-unmatched \
  --cross-file meson-unmatched/cross/riscv64-freedomusdk-linux.ini
ninja -C meson-unmatched/builddir sd-image
```

Convenience wrappers provide the same build and QEMU rootfs smoke test:

```bash
cd meson-unmatched
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

## Versions

The pinned revisions match the local Freedom-U-SDK 2026.01 recipes:

```text
OpenSBI v1.8.1  74434f255873d74e56cc50aa762d1caf24c099f8
U-Boot 2026.01  127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3
Linux 6.18.3    a607c8f744340ad2c2486d46e96b66df47caffba
```

The wrapper applies the same local Unmatched patches used by the SDK for
U-Boot and Linux.

The Linux defconfig and both patches are stored in this repository. The
Freedom-U-SDK checkout is only needed when selecting its cross-toolchain file.

## Setup

From the workspace root:

```bash
meson setup meson-unmatched/builddir meson-unmatched \
  --cross-file meson-unmatched/cross/riscv64-freedomusdk-linux.ini
ninja -C meson-unmatched/builddir info
ninja -C meson-unmatched/builddir check
```

Toolchain selection is owned by Meson cross files. The shared framework files
live under top-level `cross/`:

```text
cross/riscv64-linux-gnu.ini
cross/riscv64-freedomusdk-linux.ini
```

Compatibility copies may remain under `meson-unmatched/cross/` while the Meson
targets are migrated to the shared framework.

Use `riscv64-linux-gnu.ini` when a normal distro cross toolchain is installed.
Use `riscv64-freedomusdk-linux.ini` to reuse the local Freedom-U-SDK cross
compiler and native helper tools.

When using the Yocto-built cross compiler outside BitBake, the wrapper creates
a private `out/toolchain-shim/` directory so GCC can find the matching RISC-V
binutils without modifying the system or the Yocto tree.

To force a toolchain:

```bash
meson setup --wipe meson-unmatched/builddir meson-unmatched \
  --cross-file meson-unmatched/cross/riscv64-linux-gnu.ini
```

## Build

Fetch sources:

```bash
ninja -C meson-unmatched/builddir fetch-sources
```

BusyBox is fetched from its upstream release tarball when it is not already in
`meson-unmatched/downloads/`. Set `UNMATCHED_LITE_BUSYBOX_TARBALL` to use a
pre-downloaded tarball.

Build OpenSBI:

```bash
ninja -C meson-unmatched/builddir opensbi-fw
```

Build U-Boot. This also builds OpenSBI first if needed:

```bash
ninja -C meson-unmatched/builddir u-boot
```

Build Linux:

```bash
ninja -C meson-unmatched/builddir linux
```

Build the BusyBox rootfs:

```bash
ninja -C meson-unmatched/builddir rootfs
```

Build the whole boot chain:

```bash
ninja -C meson-unmatched/builddir bootchain
```

Build the complete GPT SD image:

```bash
ninja -C meson-unmatched/builddir sd-image
```

## Outputs

Artifacts are copied to:

```text
meson-unmatched/deploy/
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
meson-unmatched/src/      cloned source trees, including BusyBox
meson-unmatched/out/      per-project build directories
meson-unmatched/deploy/   copied artifacts
```

These are ignored by git.

By default the Linux build resets `out/linux/.config` from
`configs/linux/unmatched_defconfig`.
For config experiments, edit `out/linux/.config` and run:

```bash
UNMATCHED_LITE_KEEP_CONFIG=1 ninja -C meson-unmatched/builddir linux
```

## Clean

Remove generated build output and deploy artifacts while keeping source clones:

```bash
ninja -C meson-unmatched/builddir clean-lite
```

To remove sources too:

```bash
rm -rf meson-unmatched/src
```

The BusyBox download cache is disposable as well:

```bash
rm -rf meson-unmatched/downloads
```
