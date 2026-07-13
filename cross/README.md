# Cross Files

`riscv64-linux-gnu.ini` is the tracked default for a normal distro RISC-V
cross toolchain.

SDK-specific cross files are intentionally not tracked. Keep a local file
outside this repository and select it explicitly:

```bash
UNMATCHED_LITE_CROSS_FILE=/path/to/toolchain.ini ./build.sh qemu
```

This keeps the Meson build independent from any particular KAS or Yocto
checkout while still allowing an SDK toolchain to be reused.
