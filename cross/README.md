# Cross Files

`sifive-freedom-u-sdk.ini` is the tracked default. It uses the normalized
toolchain installed by `../toolchain.sh setup` from SiFive's official
Freedom-U-SDK `2026.01.00` source release.

Install the default toolchain before configuring Meson:

```bash
./toolchain.sh setup
./build.sh qemu
```

The generated SDK is ignored by git and has no dependency on a sibling KAS
checkout. Its source, build tree, installed SDK, compiler links, and sysroot
live under `toolchains/sifive/` by default.

An alternate cross file remains available only as an explicit override:

```bash
UNMATCHED_LITE_CROSS_FILE=/path/to/toolchain.ini ./build.sh qemu
```
