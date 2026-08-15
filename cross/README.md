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

On another x86_64 Linux machine, install a shared `populate_sdk` installer and
run the tests with the same `riscv64-freedomusdk-linux-*` compiler:

```bash
./build.sh toolchain /path/to/freedom-u-sdk-toolchain.sh
./build.sh test
```

If no shared installer is available, `./build.sh toolchain` builds it from the
pinned Freedom-U-SDK source. The setup command prints the generated installer
path so it can be published for other machines.

An alternate cross file may point to another installation of the same SDK:

```bash
UNMATCHED_LITE_CROSS_FILE=/path/to/toolchain.ini ./build.sh qemu
```
