# Meson Lite Build for HiFive Unmatched

This directory is a lightweight build wrapper for studying the Unmatched boot
chain without rebuilding Yocto recipes during normal firmware/kernel iteration.

The current top-level framework builds:

```text
OpenSBI FW_DYNAMIC -> U-Boot SPL/proper -> Linux Image.gz + DTB -> raw SD image
```

It does not rebuild a root filesystem. Instead, it uses the already-built
Freedom-U-SDK WIC image as the rootfs/GPT template and replaces the boot-chain
partitions with artifacts built by this framework.

The maintained entry point is:

```bash
./scripts/unmatched build --jobs 16
./scripts/unmatched verify
```

## Versions

The pinned revisions match the local Freedom-U-SDK 2026.01 recipes:

```text
OpenSBI v1.8.1  74434f255873d74e56cc50aa762d1caf24c099f8
U-Boot 2026.01  127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3
Linux 6.18.3    a607c8f744340ad2c2486d46e96b66df47caffba
```

The wrapper applies the same local Unmatched patches used by the SDK for
U-Boot and Linux.

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

Build the whole boot chain:

```bash
ninja -C meson-unmatched/builddir bootchain
```

Build the full lightweight chain from the repository root:

```bash
./scripts/unmatched build --jobs 16
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
unmatched-lite.img
```

## Source and Build Trees

Generated paths:

```text
meson-unmatched/src/      cloned source trees
meson-unmatched/out/      per-project build directories
meson-unmatched/deploy/   copied artifacts
```

These are ignored by git.

By default the Linux build resets `out/linux/.config` from the SDK defconfig.
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
